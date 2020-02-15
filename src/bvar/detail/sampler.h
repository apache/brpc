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
#include "butil/containers/linked_list.h"// LinkNode
#include "butil/scoped_lock.h"           // BAIDU_SCOPED_LOCK
#include "butil/logging.h"               // LOG()
#include "butil/containers/bounded_queue.h"// BoundedQueue
#include "butil/type_traits.h"           // is_same
#include "butil/time.h"                  // gettimeofday_us
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
        
protected:
    virtual ~Sampler();
    
friend class SamplerCollector;
    bool _used;
    // Sync destroy() and take_sample().
    butil::Mutex _mutex;
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

// The sampler for reducer-alike variables.
// The R should have following methods:
//  - T reset();
//  - T get_value();
//  - Op op();
//  - InvOp inv_op();
template <typename R, typename T, typename Op, typename InvOp>
class ReducerSampler : public Sampler {
public:
    static const time_t MAX_SECONDS_LIMIT = 3600;

    explicit ReducerSampler(R* reducer)
        : _reducer(reducer)
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
            if (NULL == mem) {
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
            latest.data = _reducer->reset();
        } else {
            // The operator can be inversed.
            // We save the result as a sample.
            // Inversed operation between latest and oldest sample within a
            // window gives result.
            // get_value() of _reducer can still be called.
            latest.data = _reducer->get_value();
        }
        latest.time_us = butil::gettimeofday_us();
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
        if (NULL == oldest) {
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
                _reducer->op()(result->data, e->data);
            }
        } else {
            // Diff the latest and oldest sample within the window.
            result->data = latest->data;
            _reducer->inv_op()(result->data, oldest->data);
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
        if (NULL == oldest) {
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
    R* _reducer;
    time_t _window_size;
    butil::BoundedQueue<Sample<T> > _q;
};

}  // namespace detail
}  // namespace bvar

#endif  // BVAR_DETAIL_SAMPLER_H
