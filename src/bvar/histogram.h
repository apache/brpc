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

#ifndef  BVAR_HISTOGRAM_H
#define  BVAR_HISTOGRAM_H

#include <stdint.h>                     // int64_t, uint64_t
#include <algorithm>                    // std::max
#include <initializer_list>             // std::initializer_list
#include <memory>                       // std::shared_ptr
#include <string>                       // std::string
#include <vector>                       // std::vector
#include "butil/strings/string_piece.h" // butil::StringPiece
#include "bvar/variable.h"              // Variable
#include "bvar/detail/sampler.h"        // ReducerSampler
#include "bvar/detail/series.h"         // HasPlottableSeries
#if WITH_BABYLON_COUNTER
#include "babylon/concurrent/thread_local.h" // EnumerableThreadLocal
#include "butil/atomicops.h"                 // butil::atomic
#include "butil/synchronization/seqlock.h"   // butil::Seqlock
#else
#include "bvar/detail/combiner.h"       // AgentCombiner
#endif // WITH_BABYLON_COUNTER

namespace bvar {

// Maximum number of buckets of a Histogram, the implicit +Inf one included.
// Namely a Histogram::BucketSchema takes at most MAX_HISTOGRAM_BUCKETS - 1
// bounds.
static const size_t MAX_HISTOGRAM_BUCKETS = 32;

#if WITH_BABYLON_COUNTER
namespace detail {
// Defined below, once Histogram::Value is complete.
class HistogramStorage;
}  // namespace detail
#endif // WITH_BABYLON_COUNTER

// Bucketed distribution of the recorded values.
//
// Each bucket count accumulates since construction and never decreases. The
// counts are made cumulative across buckets when exported, which is what
// prometheus expects of a histogram and what makes rate() work:
//
//   bvar::Histogram g_lat("foo_latency", {10, 50, 100, 500, 1000, 5000});
//   ...
//   g_lat << latency_us;
//
// Choose the bounds for the values being recorded. The caller supplies them
// as an initializer_list or generates a vector for a custom bucketing policy:
//
//   bvar::Histogram g_size("foo_size", {128, 1024, 8192, 65536});
//
// To read a recent distribution in process, wrap it in a Window and read the
// bucket counts off its value:
//
//   bvar::Window<bvar::Histogram> g_lat_1m(&g_lat, 60);
//   ...
//   bvar::Histogram::Value v = g_lat_1m.get_value();
//
// Unlike Percentile, the op of a Histogram has an inverse, so the Window above
// is computed by subtracting two samples rather than by resetting the Histogram,
// and get_value() keeps returning the whole history meanwhile.
//
// Do NOT expose such a Window for prometheus to scrape: its value is a
// distribution, which describe() can only write as json and the prometheus
// service therefore skips. Expose the Histogram itself and let the monitoring
// system window it with rate(foo_latency_bucket[1m]).
class Histogram : public Variable {
public:
    // Immutable and ascending upper bounds of the buckets of a Histogram.
    // The semantics follow the prometheus `le` label: bucket i counts values v
    // satisfying bound_at(i-1) < v <= bound_at(i), followed by a +Inf bucket.
    class BucketSchema {
    public:
        // `bounds` must be finite, non-empty, strictly ascending, and hold no
        // more than MAX_HISTOGRAM_BUCKETS - 1 elements. Invalid bounds are
        // logged and dropped rather than aborting the process.
        BucketSchema(std::initializer_list<double> bounds);
        explicit BucketSchema(const std::vector<double>& bounds);

        // Linear rather than a std::lower_bound: a schema holds at most
        // MAX_HISTOGRAM_BUCKETS - 1 bounds, which is a couple of cache lines
        // scanned straight through instead of jumped around in, and the
        // branch of the scan predicts far better than the one of a binary
        // search, whose direction is a coin flip at every step.
        size_t index_of(double value) const {
            size_t index = 0;
            while (index < _bounds.size() && _bounds[index] < value) {
                ++index;
            }
            return index;
        }

        size_t num_buckets() const { return _bounds.size() + 1; }
        size_t num_bounds() const { return _bounds.size(); }
        double bound_at(size_t index) const { return _bounds[index]; }
        const std::vector<double>& bounds() const { return _bounds; }
        bool is_inf_bucket(size_t index) const {
            return index == _bounds.size();
        }

    private:
        void validate_bounds();

        std::vector<double> _bounds;
    };

    // How many values fell into each bucket, plus their sum and total count.
    struct Value {
        Value() : Value(0) {}
        explicit Value(size_t nbuckets)
            : counts{}, sum(0), num(0), num_buckets(nbuckets) {}

        void add(size_t bucket_index, double value) {
            ++counts[bucket_index];
            sum += value;
            ++num;
        }

        void operator+=(const Value& rhs) {
            for (size_t i = 0; i < MAX_HISTOGRAM_BUCKETS; ++i) {
                counts[i] += rhs.counts[i];
            }
            sum += rhs.sum;
            num += rhs.num;
            num_buckets = std::max(num_buckets, rhs.num_buckets);
        }

        void operator-=(const Value& rhs) {
            for (size_t i = 0; i < MAX_HISTOGRAM_BUCKETS; ++i) {
                counts[i] -= rhs.counts[i];
            }
            sum -= rhs.sum;
            num -= rhs.num;
            num_buckets = std::max(num_buckets, rhs.num_buckets);
        }

        double get_average_double() const {
            return num == 0 ? 0.0 : sum / (double)num;
        }

        uint64_t counts[MAX_HISTOGRAM_BUCKETS];
        double sum;
        int64_t num;
        size_t num_buckets;
    };

    struct Op {
        void operator()(Value& lhs, const Value& rhs) const { lhs += rhs; }
    };

    struct InvOp {
        void operator()(Value& lhs, const Value& rhs) const { lhs -= rhs; }
    };

    typedef Value value_type;
    typedef detail::ReducerSampler<Histogram, value_type, Op, InvOp> sampler_type;
#if WITH_BABYLON_COUNTER
    typedef std::shared_ptr<detail::HistogramStorage> shared_combiner_type;
#else
    typedef detail::AgentCombiner<value_type, value_type, Op> combiner_type;
    typedef combiner_type::self_shared_type shared_combiner_type;
    typedef combiner_type::Agent agent_type;
#endif // WITH_BABYLON_COUNTER

    explicit Histogram(const BucketSchema& schema);
    Histogram(const butil::StringPiece& name, const BucketSchema& schema);
    Histogram(const butil::StringPiece& prefix, const butil::StringPiece& name,
              const BucketSchema& schema);
    ~Histogram() override;

    Histogram& operator<<(double value);

    // Number and approximate floating-point sum of the values recorded so far.
    int64_t count() const { return get_value().num; }
    double sum() const { return get_value().sum; }
    double average() const { return get_value().get_average_double(); }

    const BucketSchema& schema() const { return _schema; }

    bool valid() const {
#if WITH_BABYLON_COUNTER
        return _storage != nullptr;
#else
        return _combiner != nullptr && _combiner->valid();
#endif // WITH_BABYLON_COUNTER
    }

    void describe(std::ostream& os, bool quote_string) const override;

    // Emits the prometheus histogram family.
    bool dump(Dumper* dumper, const DumpOptions& options,
              const std::string& name) const override;

    // The composite metric contract, see bvar/variable.h
    // `_bucket`, `_sum` and `_count` are the series of one family rather
    // than three families of their own, hence the single entry with an
    // empty suffix.
    static const std::vector<MetricFamily>& list_metric_families();
    bool dump_samples(Dumper* dumper, size_t family_index,
                      const std::string& name,
                      butil::StringPiece labels) const;

    // The contract of Window<>/ReducerSampler
    Op op() const { return Op(); }
    InvOp inv_op() const { return InvOp(); }
    // Expose the shared data carrier, so that ReducerSampler holds it
    // instead of `this`. Sampling then keeps reading valid memory even
    // if this Histogram is destructed before the sampler is recycled.
    shared_combiner_type share_combiner() const {
#if WITH_BABYLON_COUNTER
        return _storage;
#else
        return _combiner;
#endif // WITH_BABYLON_COUNTER
    }
    sampler_type* get_sampler();

private:
    int expose_impl(const butil::StringPiece& prefix,
                    const butil::StringPiece& name,
                    DisplayFilter display_filter) override;

    // Snapshot of all the values recorded so far. Walks through every thread
    // that ever recorded into this Histogram.
    value_type get_value() const;

    BucketSchema _schema;
#if WITH_BABYLON_COUNTER
    shared_combiner_type _storage;
#else
    shared_combiner_type _combiner;
#endif // WITH_BABYLON_COUNTER
    sampler_type* _sampler;
};

std::ostream& operator<<(std::ostream& os, const Histogram::Value& value);

namespace detail {

// A distribution has no plot to draw, same reason as the one spelled out in
// Histogram::expose_impl(). Without this an exposed Window<Histogram> would
// keep 174 samples of a value this wide and feed flot.js json objects.
template <>
struct HasPlottableSeries<Histogram::Value> : butil::false_type {};

#if WITH_BABYLON_COUNTER

// One thread's slice of a Histogram.
//
// Only the thread owning the slot writes it, so the counters are updated with
// a relaxed load plus a relaxed store rather than an atomic read-modify-write.
// They are atomic all the same because the sampling thread reads them while
// they are being written, which the seqlock allows but does not by itself make
// race free. The seqlock is what keeps the buckets, the sum and the count of one
// slot mutually consistent.
class HistogramSlot {
public:
    HistogramSlot() {
        for (size_t i = 0; i < MAX_HISTOGRAM_BUCKETS; ++i) {
            _counts[i].store(0, butil::memory_order_relaxed);
        }
    }

    DISALLOW_COPY_AND_ASSIGN(HistogramSlot);

    void add(size_t bucket_index, double value) {
        _seqlock.store([&] {
            relaxed_add(&_counts[bucket_index], (uint64_t)1);
            relaxed_add(&_sum, value);
            relaxed_add(&_num, (int64_t)1);
        });
    }

    Histogram::Value load(size_t num_buckets) const {
        return _seqlock.load([&] {
            Histogram::Value v(num_buckets);
            for (size_t i = 0; i < num_buckets; ++i) {
                v.counts[i] = _counts[i].load(butil::memory_order_relaxed);
            }
            v.sum = _sum.load(butil::memory_order_relaxed);
            v.num = _num.load(butil::memory_order_relaxed);
            return v;
        });
    }

private:
    template <typename T, typename U>
    static void relaxed_add(butil::atomic<T>* target, U delta) {
        target->store(target->load(butil::memory_order_relaxed) + delta,
                      butil::memory_order_relaxed);
    }

    butil::Seqlock<> _seqlock;
    butil::atomic<uint64_t> _counts[MAX_HISTOGRAM_BUCKETS];
    butil::atomic<double> _sum{0.0};
    butil::atomic<int64_t> _num{0};
};

// The per thread slices of one Histogram and their aggregation.
//
// babylon's EnumerableThreadLocal hands out one slot per thread and walks every
// slot any thread ever took, which is how a thread that has exited keeps
// contributing what it recorded, the way AgentCombiner commits a dying agent
// into its global result. babylon recycles the thread id of an exited thread,
// so a later thread inherits the slot and accumulates on top of it: correct
// here because a Histogram only ever adds to its buckets and never clears one.
class HistogramStorage {
public:
    explicit HistogramStorage(size_t num_buckets) : _num_buckets(num_buckets) {}

    DISALLOW_COPY_AND_ASSIGN(HistogramStorage);

    // Records one value into the slot of the calling thread.
    void add(size_t bucket_index, double value) {
        _slots.local().add(bucket_index, value);
    }

    // [Threadsafe] Everything recorded so far by every thread that ever
    // recorded into this Histogram. Named after AgentCombiner::combine_agents()
    // so that detail::CombinerSampleSource fits either backend.
    Histogram::Value combine_agents() const {
        Histogram::Value result(_num_buckets);
        _slots.for_each([&](const HistogramSlot* iter, const HistogramSlot* end) {
            for (; iter != end; ++iter) {
                result += iter->load(_num_buckets);
            }
        });
        return result;
    }

private:
    // Leaky: the id allocator behind the thread ids is never destroyed, which
    // is what a Histogram of static storage duration needs. The thread ids
    // themselves are still recycled when a thread exits.
    babylon::EnumerableThreadLocal<HistogramSlot, true> _slots;
    size_t _num_buckets;
};

#else

// The op of the writing path takes a recorded value rather than another
// Histogram::Value, and needs the schema to find its bucket.
struct AddSampleToHistogram {
    explicit AddSampleToHistogram(const Histogram::BucketSchema* s)
        : schema(s) {}
    void operator()(Histogram::Value& lhs, double value) const {
        lhs.add(schema->index_of(value), value);
    }

    const Histogram::BucketSchema* schema;
};

#endif // WITH_BABYLON_COUNTER

}  // namespace detail

}  // namespace bvar

#endif  // BVAR_HISTOGRAM_H
