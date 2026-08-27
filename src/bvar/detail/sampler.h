// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// Date: Tue Jul 28 18:15:57 CST 2015

#ifndef  BVAR_DETAIL_SAMPLER_H
#define  BVAR_DETAIL_SAMPLER_H

#include <vector>
#include <string>                        // std::string
#include <type_traits>                   // std::true_type
#include <utility>                       // std::declval
#include "butil/containers/linked_list.h"// LinkNode
#include "butil/scoped_lock.h"           // BAIDU_SCOPED_LOCK
#include "butil/logging.h"               // LOG()
#include "butil/containers/bounded_queue.h"// BoundedQueue
#include "butil/type_traits.h"           // is_same
#include "butil/time.h"                  // cpuwide_time_us
#include "butil/class_name.h"

namespace bvar {
namespace detail {

template <typename T>
struct Sample {
    T data;
    int64_t time_us;

    Sample() : data(), time_us(0) {}
    Sample(const T& data2, int64_t time2) : data(data2), time_us(time2) {}  
};

// The base class for all samplers whose take_sample() are called periodically.
class Sampler : public butil::LinkNode<Sampler> {
public:
    Sampler();
        
    // This function will be called every second(approximately) in a
    // dedicated thread if schedule() is called.
    virtual void take_sample() = 0;

    // Register this sampler globally so that take_sample() will be called
    // periodically.
    void schedule();

    // Call this function instead of delete to destroy the sampler. Deletion
    // of the sampler may be delayed for seconds.
    void destroy();

    // Declare/undeclare that an external object borrows this sampler which is
    // owned by another bvar. Window/PerSecond does this because it samples
    // through the sampler of the bvar it references.
    // If the owner is destructed while borrowers remain (namely a Window
    // outlives the bvar it references, which violates the contract documented
    // in bvar/window.h), destroy() reports the misuse and the sampler is
    // deliberately leaked so that borrowers are not left with a dangling
    // pointer.
    void add_borrower();
    void remove_borrower();

    // Name of the owning bvar, purely for diagnostics.
    void set_debug_name(const std::string& name) {
        BAIDU_SCOPED_LOCK(_mutex);
        _debug_name = name;
    }
    std::string debug_name() const {
        BAIDU_SCOPED_LOCK(_mutex);
        return _debug_name;
    }

protected:
    virtual ~Sampler();
    
friend class SamplerCollector;
    bool _used;
    // Number of external borrowers, guarded by _mutex.
    int _nborrow;
    // Set by destroy() when _nborrow > 0, telling the sampling thread to leak
    // this sampler instead of deleting it. Guarded by _mutex.
    bool _leaked;
    mutable butil::Mutex _mutex;
    // For diagnostics only, see set_debug_name().
    std::string _debug_name;
};

// Representing a non-existing operator so that we can test
// is_same<Op, VoidOp>::value to write code for different branches.
// The false branch should be removed by compiler at compile-time.
struct VoidOp {
    template <typename T>
    T operator()(const T&, const T&) const {
        CHECK(false) << "This function should never be called, abort";
        abort();
    }
};

// Detects whether the host R exposes share_combiner(), namely whether its
// sampling data lives in a shared_ptr-managed carrier (an AgentCombiner) that
// the sampler is able to hold on its own. Hosts keeping the data elsewhere --
// a user callback in PassiveStatus, or a value-type babylon counter -- do NOT
// provide it and are sampled through the host pointer as before.
template <typename R>
class HasShareCombiner {
    template <typename U>
    static auto probe(U* p) -> decltype(p->share_combiner(), std::true_type());
    static std::false_type probe(...);
public:
    static const bool value = decltype(probe(std::declval<R*>()))::value;
};

// Samples through the host pointer, for hosts keeping their data outside a
// shared carrier (a user callback in PassiveStatus, a value-type babylon
// counter, ...).
template <typename R, typename T, typename Op, typename InvOp>
class HostSampleSource {
public:
    explicit HostSampleSource(R* host)
        : _host(host), _op(host->op()), _inv_op(host->inv_op()) {}

    // Only reached from take_sample(), namely from the sampling thread, which is
    // mutually exclusive with the host's destroy(). The host is therefore always
    // alive here.
    T reset() { return _host->reset(); }
    T get_value() const { return _host->get_value(); }

    // Never touch the host, see the ctor.
    const Op& op() const { return _op; }
    const InvOp& inv_op() const { return _inv_op; }

private:
    R* _host;
    Op _op;
    InvOp _inv_op;
};

// Samples directly from the shared data carrier, so that sampling still reads
// valid memory even if the host is destructed before the sampler is recycled.
// `Op'/`InvOp' are stateless functors, thus copied by value at construction and
// the host is never touched afterwards.
template <typename R, typename T, typename Op, typename InvOp>
class CombinerSampleSource {
public:
    explicit CombinerSampleSource(R* host)
        : _combiner(host->share_combiner())
        , _op(host->op())
        , _inv_op(host->inv_op()) {}

    T reset() { return _combiner->reset_all_agents(); }
    T get_value() const { return _combiner->combine_agents(); }
    const Op& op() const { return _op; }
    const InvOp& inv_op() const { return _inv_op; }

private:
    typename R::shared_combiner_type _combiner;
    Op _op;
    InvOp _inv_op;
};

// The sampler for reducer-alike variables.
// The R should have following methods:
//  - T reset();
//  - T get_value();
//  - Op op();
//  - InvOp inv_op();
// Additionally, if R exposes
//  - shared_combiner_type share_combiner();
// the sampler holds that shared carrier instead of R itself, which makes
// sampling immune to R being destructed first.
template <typename R, typename T, typename Op, typename InvOp>
class ReducerSampler : public Sampler {
    typedef typename butil::conditional<
        HasShareCombiner<R>::value,
        CombinerSampleSource<R, T, Op, InvOp>,
        HostSampleSource<R, T, Op, InvOp> >::type source_type;

public:
    static const time_t MAX_SECONDS_LIMIT = 3600;

    explicit ReducerSampler(R* reducer)
        : _source(reducer)
        , _window_size(1) {
        
        // Invoked take_sample at begining so the value of the first second
        // would not be ignored
        take_sample();
    }
    ~ReducerSampler() {}

    void take_sample() override {
        // Make _q ready.
        // If _window_size is larger than what _q can hold, e.g. a larger
        // Window<> is created after running of sampler, make _q larger.
        if ((size_t)_window_size + 1 > _q.capacity()) {
            const size_t new_cap =
                std::max(_q.capacity() * 2, (size_t)_window_size + 1);
            const size_t memsize = sizeof(Sample<T>) * new_cap;
            void* mem = malloc(memsize);
            if (nullptr == mem) {
                return;
            }
            butil::BoundedQueue<Sample<T> > new_q(
                mem, memsize, butil::OWNS_STORAGE);
            Sample<T> tmp;
            while (_q.pop(&tmp)) {
                new_q.push(tmp);
            }
            new_q.swap(_q);
        }

        Sample<T> latest;
        if (butil::is_same<InvOp, VoidOp>::value) {
            // The operator can't be inversed.
            // We reset the reducer and save the result as a sample.
            // Suming up samples gives the result within a window.
            // In this case, get_value() of _reducer gives wrong answer and
            // should not be called.
            latest.data = _source.reset();
        } else {
            // The operator can be inversed.
            // We save the result as a sample.
            // Inversed operation between latest and oldest sample within a
            // window gives result.
            // get_value() of _reducer can still be called.
            latest.data = _source.get_value();
        }
        latest.time_us = butil::cpuwide_time_us();
        _q.elim_push(latest);
    }

    bool get_value(time_t window_size, Sample<T>* result) {
        if (window_size <= 0) {
            LOG(FATAL) << "Invalid window_size=" << window_size;
            return false;
        }
        BAIDU_SCOPED_LOCK(_mutex);
        if (_q.size() <= 1UL) {
            // We need more samples to get reasonable result.
            return false;
        }
        Sample<T>* oldest = _q.bottom(window_size);
        if (nullptr == oldest) {
            oldest = _q.top();
        }
        Sample<T>* latest = _q.bottom();
        DCHECK(latest != oldest);
        if (butil::is_same<InvOp, VoidOp>::value) {
            // No inverse op. Sum up all samples within the window.
            result->data = latest->data;
            for (int i = 1; true; ++i) {
                Sample<T>* e = _q.bottom(i);
                if (e == oldest) {
                    break;
                }
                _source.op()(result->data, e->data);
            }
        } else {
            // Diff the latest and oldest sample within the window.
            result->data = latest->data;
            _source.inv_op()(result->data, oldest->data);
        }
        result->time_us = latest->time_us - oldest->time_us;
        return true;
    }

    // Change the time window which can only go larger.
    int set_window_size(time_t window_size) {
        if (window_size <= 0 || window_size > MAX_SECONDS_LIMIT) {
            LOG(ERROR) << "Invalid window_size=" << window_size;
            return -1;
        }
        BAIDU_SCOPED_LOCK(_mutex);
        if (window_size > _window_size) {
            _window_size = window_size;
        }
        return 0;
    }

    void get_samples(std::vector<T> *samples, time_t window_size) {
        if (window_size <= 0) {
            LOG(FATAL) << "Invalid window_size=" << window_size;
            return;
        }
        BAIDU_SCOPED_LOCK(_mutex);
        if (_q.size() <= 1) {
            // We need more samples to get reasonable result.
            return;
        }
        Sample<T>* oldest = _q.bottom(window_size);
        if (nullptr == oldest) {
            oldest = _q.top();
        }
        for (int i = 1; true; ++i) {
            Sample<T>* e = _q.bottom(i);
            if (e == oldest) {
                break;
            }
            samples->push_back(e->data);
        }
    }

private:
    source_type _source;
    time_t _window_size;
    butil::BoundedQueue<Sample<T> > _q;
};

}  // namespace detail
}  // namespace bvar

#endif  // BVAR_DETAIL_SAMPLER_H
