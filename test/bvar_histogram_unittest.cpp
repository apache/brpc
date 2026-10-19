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

#include <pthread.h>
#include <unistd.h>                     // usleep
#include <sched.h>                      // sched_yield
#include <stdio.h>                      // snprintf
#include <string.h>                     // memset
#include <algorithm>                    // std::max
#include <iomanip>                      // std::setprecision
#include <limits>
#include <map>
#include <memory>                       // std::make_shared
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <butil/atomicops.h>
#include <butil/float_util.h>
#include <butil/logging.h>
#include <butil/strings/string_number_conversions.h>
#include <butil/time.h>
#include "bvar/bvar.h"
#include "bvar/detail/combiner.h"
#include "bvar/histogram.h"
#include "bvar/multi_dimension.h"
#include "bvar/window.h"

namespace {

// Test builds use -fno-access-control (see test/CMakeLists.txt), so the tests
// below call private members such as Histogram::get_value() directly.
class HistogramTest : public testing::Test {};

#if WITH_BABYLON_COUNTER

// One thread's slice of a Histogram, the babylon backed replacement of the
// ElementContainer tested below.
TEST_F(HistogramTest, histogram_slot) {
    bvar::Histogram::BucketSchema schema({10, 20, 30});
    bvar::detail::HistogramSlot slot;
    // Freshly constructed, before any add().
    bvar::Histogram::Value v = slot.load(schema.num_buckets());
    ASSERT_EQ(0, v.num);
    ASSERT_DOUBLE_EQ(0.0, v.sum);
    ASSERT_EQ(0u, v.counts[0]);

    slot.add(schema.index_of(5.25), 5.25);
    slot.add(schema.index_of(25.5), 25.5);
    slot.add(schema.index_of(1000.75), 1000.75);
    v = slot.load(schema.num_buckets());
    ASSERT_EQ(3, v.num);
    ASSERT_DOUBLE_EQ(1031.5, v.sum);
    ASSERT_EQ(4u, v.num_buckets);
    ASSERT_EQ(1u, v.counts[0]);   // 5.25    -> (-inf, 10]
    ASSERT_EQ(0u, v.counts[1]);
    ASSERT_EQ(1u, v.counts[2]);   // 25.5    -> (20, 30]
    ASSERT_EQ(1u, v.counts[3]);   // 1000.75 -> +Inf
}

// A slot is taken lazily, on the first record of a thread, and the storage
// aggregates every slot ever taken.
TEST_F(HistogramTest, histogram_storage) {
    bvar::Histogram::BucketSchema schema({10, 20, 30});
    bvar::detail::HistogramStorage storage(schema.num_buckets());
    // No thread has recorded anything, yet the combined value already knows
    // how wide the histogram is.
    bvar::Histogram::Value v = storage.combine_agents();
    ASSERT_EQ(0, v.num);
    ASSERT_EQ(4u, v.num_buckets);

    storage.add(schema.index_of(5.25), 5.25);
    storage.add(schema.index_of(25.5), 25.5);
    v = storage.combine_agents();
    ASSERT_EQ(2, v.num);
    ASSERT_DOUBLE_EQ(30.75, v.sum);
    ASSERT_EQ(1u, v.counts[0]);
    ASSERT_EQ(1u, v.counts[2]);
}

#else

// The element container of a Histogram::Value, which is the generic mutex one:
// the value is far too wide to be atomical.
TEST_F(HistogramTest, element_container) {
    ASSERT_FALSE(bvar::detail::is_atomical<bvar::Histogram::Value>::value);
    bvar::Histogram::BucketSchema schema({10, 20, 30});
    bvar::detail::ElementContainer<bvar::Histogram::Value> c;
    bvar::Histogram::Value v;
    // Freshly constructed, before any store().
    c.load(&v);
    ASSERT_EQ(0, v.num);
    ASSERT_DOUBLE_EQ(0.0, v.sum);

    c.store(bvar::Histogram::Value(schema.num_buckets()));
    c.modify(bvar::detail::AddSampleToHistogram(&schema), 5.25);
    c.modify(bvar::detail::AddSampleToHistogram(&schema), 25.5);
    c.modify(bvar::detail::AddSampleToHistogram(&schema), 1000.75);
    c.load(&v);
    ASSERT_EQ(3, v.num);
    ASSERT_DOUBLE_EQ(1031.5, v.sum);
    ASSERT_EQ(4u, v.num_buckets);
    ASSERT_EQ(1u, v.counts[0]);   // 5.25    -> (-inf, 10]
    ASSERT_EQ(0u, v.counts[1]);
    ASSERT_EQ(1u, v.counts[2]);   // 25.5    -> (20, 30]
    ASSERT_EQ(1u, v.counts[3]);   // 1000.75 -> +Inf

    // store() publishes a whole value, overwriting everything.
    c.store(bvar::Histogram::Value(schema.num_buckets()));
    c.load(&v);
    ASSERT_EQ(0, v.num);
    ASSERT_EQ(0u, v.counts[3]);
}

#endif // WITH_BABYLON_COUNTER

// Fixed workload for export and performance tests, not a library default.
static bvar::Histogram::BucketSchema test_latency_schema() {
    return {10, 20, 40, 80, 160, 320, 640, 1280, 2560, 5120,
            10240, 20480, 40960, 81920, 163840, 327680, 655360,
            1310720, 2621440, 5242880};
}

TEST_F(HistogramTest, schema_fractional_bounds) {
    bvar::Histogram::BucketSchema schema = {-0.5, 0.25, 1.5};
    ASSERT_EQ(std::vector<double>({-0.5, 0.25, 1.5}), schema.bounds());
    ASSERT_EQ(0u, schema.index_of(-0.5));
    ASSERT_EQ(1u, schema.index_of(-0.25));
    ASSERT_EQ(1u, schema.index_of(0.25));
    ASSERT_EQ(2u, schema.index_of(1.5));
    ASSERT_EQ(3u, schema.index_of(1.5001));
}

// Bounds the caller spells out, through either entry point.
TEST_F(HistogramTest, schema_custom_bounds) {
    double expected[] = {0.125, 1.25, 8.5, 64.5};

    // As an initializer_list.
    bvar::Histogram::BucketSchema from_list = {0.125, 1.25, 8.5, 64.5};
    ASSERT_EQ(4u, from_list.num_bounds());
    ASSERT_EQ(5u, from_list.num_buckets());
    for (size_t i = 0; i < from_list.num_bounds(); ++i) {
        ASSERT_EQ(expected[i], from_list.bound_at(i)) << "i=" << i;
    }

    // As a vector.
    std::vector<double> v(expected, expected + arraysize(expected));
    bvar::Histogram::BucketSchema from_vector(v);
    ASSERT_EQ(v, from_vector.bounds());

    // The initializer_list ctor is implicit, so the bounds can be written at
    // the declaration of the histogram itself.
    bvar::Histogram h("hist_custom_bounds_test", {0.125, 1.25, 8.5, 64.5});
    ASSERT_EQ(v, h.schema().bounds());
    h << 0.1 << 0.5 << 100.25;
    bvar::Histogram::Value hv = h.get_value();
    ASSERT_EQ(3, hv.num);
    ASSERT_EQ(5u, hv.num_buckets);
    ASSERT_EQ(1u, hv.counts[0]);   // 0.1 <= 0.125
    ASSERT_EQ(1u, hv.counts[1]);   // 0.125 < 0.5 <= 1.25
    ASSERT_EQ(0u, hv.counts[2]);
    ASSERT_EQ(0u, hv.counts[3]);
    ASSERT_EQ(1u, hv.counts[4]);   // 100.25 > 64.5, the +Inf bucket
}

// The repairs apply no matter which entry point the bounds came through.
TEST_F(HistogramTest, schema_custom_bounds_are_validated) {
    bvar::Histogram::BucketSchema unsorted = {10, 5, 10, 20};
    ASSERT_EQ(2u, unsorted.num_bounds());
    ASSERT_EQ(10, unsorted.bound_at(0));
    ASSERT_EQ(20, unsorted.bound_at(1));

    bvar::Histogram::BucketSchema empty_list = {};
    ASSERT_EQ(1u, empty_list.num_bounds());

    bvar::Histogram::BucketSchema empty_vector{std::vector<double>()};
    ASSERT_EQ(1u, empty_vector.num_bounds());

    std::vector<double> too_many;
    for (size_t i = 0; i < bvar::MAX_HISTOGRAM_BUCKETS + 10; ++i) {
        too_many.push_back((double)i + 1);
    }
    ASSERT_EQ(bvar::MAX_HISTOGRAM_BUCKETS - 1,
              bvar::Histogram::BucketSchema(too_many).num_bounds());

    std::vector<double> non_finite = {
        -std::numeric_limits<double>::infinity(), 1.5,
        std::numeric_limits<double>::quiet_NaN(), 2.5,
        std::numeric_limits<double>::infinity()};
    bvar::Histogram::BucketSchema finite_only(non_finite);
    ASSERT_EQ(std::vector<double>({1.5, 2.5}), finite_only.bounds());
}

TEST_F(HistogramTest, index_of_is_le) {
    // Buckets are (-inf,0.5], (0.5,1.5], (1.5,3], (3,+inf)
    bvar::Histogram::BucketSchema s = bvar::Histogram::BucketSchema({0.5, 1.5, 3.0});
    ASSERT_EQ(0u, s.index_of(-100));
    ASSERT_EQ(0u, s.index_of(0));
    ASSERT_EQ(0u, s.index_of(0.5));   // le, so 0.5 is in the first bucket
    ASSERT_EQ(1u, s.index_of(0.5001));
    ASSERT_EQ(1u, s.index_of(1.5));
    ASSERT_EQ(2u, s.index_of(1.5001));
    ASSERT_EQ(2u, s.index_of(3.0));
    ASSERT_EQ(3u, s.index_of(3.0001));   // the +Inf bucket
    ASSERT_EQ(3u, s.index_of(1000000));
}

TEST_F(HistogramTest, add_and_get_value) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({0.5, 1.5, 3.0}));
    ASSERT_TRUE(h.valid());
    ASSERT_EQ(0, h.count());
    ASSERT_DOUBLE_EQ(0.0, h.sum());
    ASSERT_DOUBLE_EQ(0.0, h.average());

    h << 0.25 << 1.25 << 2.5 << 3.5 << 1.25;

    bvar::Histogram::Value v = h.get_value();
    ASSERT_EQ(5, v.num);
    ASSERT_DOUBLE_EQ(8.75, v.sum);
    ASSERT_EQ(4u, v.num_buckets);
    ASSERT_EQ(1u, v.counts[0]);   // 0.25
    ASSERT_EQ(2u, v.counts[1]);   // 1.25, 1.25
    ASSERT_EQ(1u, v.counts[2]);   // 2.5
    ASSERT_EQ(1u, v.counts[3]);   // 3.5 -> +Inf
    ASSERT_EQ(5, h.count());
    ASSERT_DOUBLE_EQ(8.75, h.sum());
    ASSERT_DOUBLE_EQ(1.75, h.average());
}

TEST_F(HistogramTest, value_arithmetic) {
    bvar::Histogram::Value a(4);
    a.add(0, 0.5);
    a.add(1, 1.25);
    bvar::Histogram::Value b(4);
    b.add(1, 0.75);
    b.add(3, 2.5);

    bvar::Histogram::Value c = a;
    c += b;
    ASSERT_EQ(4, c.num);
    ASSERT_DOUBLE_EQ(5.0, c.sum);
    ASSERT_EQ(1u, c.counts[0]);
    ASSERT_EQ(2u, c.counts[1]);
    ASSERT_EQ(0u, c.counts[2]);
    ASSERT_EQ(1u, c.counts[3]);

    // These values are exactly representable, so -= restores the original.
    // General Window sums may still have normal floating-point rounding error.
    c -= b;
    ASSERT_EQ(a.num, c.num);
    ASSERT_DOUBLE_EQ(a.sum, c.sum);
    for (size_t i = 0; i < bvar::MAX_HISTOGRAM_BUCKETS; ++i) {
        ASSERT_EQ(a.counts[i], c.counts[i]) << "i=" << i;
    }
}

TEST_F(HistogramTest, floating_point_sum_and_window_difference) {
    ASSERT_TRUE(std::is_trivially_copyable<bvar::Histogram::Value>::value);
    bvar::Histogram h({0});
    h << -0.5 << 0.25;
    bvar::Histogram::Value before = h.get_value();
    h << 1.125;
    bvar::Histogram::Value delta = h.get_value();
    delta -= before;
    ASSERT_EQ(1, delta.num);
    ASSERT_DOUBLE_EQ(1.125, delta.sum);
    ASSERT_DOUBLE_EQ(1.125, delta.get_average_double());
    ASSERT_DOUBLE_EQ(0.875, h.sum());
}

TEST_F(HistogramTest, ignores_non_finite_observations) {
    bvar::Histogram h({0});
    h << std::numeric_limits<double>::quiet_NaN()
      << std::numeric_limits<double>::infinity()
      << -std::numeric_limits<double>::infinity()
      << 0.25;
    ASSERT_EQ(1, h.count());
    ASSERT_DOUBLE_EQ(0.25, h.sum());
}

TEST_F(HistogramTest, describe) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({0.5, 1.5, 3.25}));
    h << 0.25 << 1.25 << 1.25 << 3.5;
    std::ostringstream os;
    h.describe(os, false);
    ASSERT_EQ("{\"count\":4,\"sum\":6.25,\"bounds\":[0.5,1.5,3.25],"
              "\"counts\":[1,2,0,1]}", os.str());

    // A Histogram::Value on its own has no bounds to print.
    std::ostringstream os2;
    os2 << h.get_value();
    ASSERT_EQ("{\"count\":4,\"sum\":6.25,\"counts\":[1,2,0,1]}", os2.str());
}

TEST_F(HistogramTest, describe_writes_a_non_finite_sum_as_null) {
    double big = std::numeric_limits<double>::max();
    bvar::Histogram h(bvar::Histogram::BucketSchema({0.5, 1.5}));
    h << big << big;
    ASSERT_FALSE(butil::IsFinite(h.get_value().sum));

    std::ostringstream os;
    h.describe(os, false);
    ASSERT_EQ("{\"count\":2,\"sum\":null,\"bounds\":[0.5,1.5],"
              "\"counts\":[0,0,2]}", os.str());

    std::ostringstream os2;
    os2 << h.get_value();
    ASSERT_EQ("{\"count\":2,\"sum\":null,\"counts\":[0,0,2]}", os2.str());
}

// Collects everything a Dumper is asked to write, in order.
class RecordingDumper : public bvar::Dumper {
public:
    bool dump(const std::string& name,
              const butil::StringPiece& desc) override {
        lines.push_back("dump_" + name + " " + desc.as_string());
        return true;
    }
    bool dump_mvar(const std::string& name,
                   const butil::StringPiece& desc) override {
        lines.push_back("mvar_" + name + " " + desc.as_string());
        return true;
    }
    bool dump_comment(const std::string& name,
                      const std::string& type) override {
        lines.push_back("comment " + name + " " + type);
        return true;
    }
    std::vector<std::string> lines;
};

TEST_F(HistogramTest, dump) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({0.5, 1.5, 3.25}));
    h << 0.25 << 1.25 << 1.25 << 3.5;

    RecordingDumper d;
    bvar::DumpOptions opt;
    ASSERT_TRUE(h.dump(&d, opt, "foo"));

    ASSERT_EQ(7u, d.lines.size());
    ASSERT_EQ("comment foo histogram", d.lines[0]);
    // The counts a histogram dumps are cumulative: 1, 1+2, 1+2+0, 1+2+0+1.
    ASSERT_EQ("mvar_foo_bucket{le=\"0.5\"} 1", d.lines[1]);
    ASSERT_EQ("mvar_foo_bucket{le=\"1.5\"} 3", d.lines[2]);
    ASSERT_EQ("mvar_foo_bucket{le=\"3.25\"} 3", d.lines[3]);
    ASSERT_EQ("mvar_foo_bucket{le=\"+Inf\"} 4", d.lines[4]);
    ASSERT_EQ("mvar_foo_sum 6.25", d.lines[5]);
    ASSERT_EQ("mvar_foo_count 4", d.lines[6]);
}

// A Dumper that stops partway must stop the histogram too.
class FailingDumper : public bvar::Dumper {
public:
    explicit FailingDumper(int fail_at) : _fail_at(fail_at), _n(0) {}
    bool dump(const std::string&, const butil::StringPiece&) override {
        return _n++ != _fail_at;
    }
    bool dump_mvar(const std::string&, const butil::StringPiece&) override {
        return _n++ != _fail_at;
    }
    bool dump_comment(const std::string&, const std::string&) override {
        return _n++ != _fail_at;
    }
    int count() const { return _n; }
private:
    int _fail_at;
    int _n;
};

TEST_F(HistogramTest, dump_stops_on_failure) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));
    h << 5;
    bvar::DumpOptions opt;
    // Fail on the comment, on a bucket, on _sum and on _count in turn.
    int fail_points[] = {0, 2, 5, 6};
    for (size_t i = 0; i < arraysize(fail_points); ++i) {
        FailingDumper d(fail_points[i]);
        ASSERT_FALSE(h.dump(&d, opt, "foo")) << "fail_at=" << fail_points[i];
        ASSERT_EQ(fail_points[i] + 1, d.count()) << "fail_at=" << fail_points[i];
    }
}

// Variables that don't override dump() must keep dumping as one metric.
TEST_F(HistogramTest, default_variable_dump_is_unchanged) {
    bvar::Adder<int> a;
    a << 7;
    RecordingDumper d;
    bvar::DumpOptions opt;
    ASSERT_TRUE(a.dump(&d, opt, "bar"));
    ASSERT_EQ(1u, d.lines.size());
    ASSERT_EQ("dump_bar 7", d.lines[0]);
}

// A Dumper written before composite metrics existed only implements dump().
// The samples of a histogram carry their labels inside the name, which such a
// dumper has no way of reading, so it must not start receiving them behind its
// back: opting in is what the dump_mvar() override is for.
class DumpOnlyDumper : public bvar::Dumper {
public:
    bool dump(const std::string& name,
              const butil::StringPiece& desc) override {
        lines.push_back(name + " " + desc.as_string());
        return true;
    }
    std::vector<std::string> lines;
};

TEST_F(HistogramTest, samples_reach_an_opted_in_dumper_only) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20}));
    h << 5 << 25;

    DumpOnlyDumper d;
    bvar::DumpOptions opt;
    ASSERT_TRUE(h.dump(&d, opt, "foo"));
    ASSERT_TRUE(d.lines.empty()) << d.lines[0];

    // A plain value is unaffected, it goes through dump() as it always did.
    bvar::Adder<int> a;
    a << 7;
    ASSERT_TRUE(a.dump(&d, opt, "bar"));
    ASSERT_EQ(1u, d.lines.size());
    ASSERT_EQ("bar 7", d.lines[0]);
}

TEST_F(HistogramTest, non_finite_sum_uses_prometheus_spelling) {
    double big = std::numeric_limits<double>::max();
    {
        bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20}));
        h << big << big;
        ASSERT_FALSE(butil::IsFinite(h.get_value().sum));

        RecordingDumper d;
        ASSERT_TRUE(h.dump_samples(&d, 0, "foo", butil::StringPiece()));
        ASSERT_EQ(5u, d.lines.size());
        ASSERT_EQ("mvar_foo_sum +Inf", d.lines[3]);
        // The count is untouched, both values were recorded.
        ASSERT_EQ("mvar_foo_count 2", d.lines[4]);
    }
    {
        bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20}));
        h << -big << -big;
        RecordingDumper d;
        ASSERT_TRUE(h.dump_samples(&d, 0, "foo", butil::StringPiece()));
        ASSERT_EQ("mvar_foo_sum -Inf", d.lines[3]);
    }
}

TEST_F(HistogramTest, dump_exposed_goes_through_the_virtual) {
    bvar::Histogram h("hist_dump_exposed_test",
                      bvar::Histogram::BucketSchema({10, 20}));
    h << 5 << 25;

    RecordingDumper d;
    bvar::DumpOptions opt;
    opt.white_wildcards = "hist_dump_exposed_test";
    ASSERT_EQ(1, bvar::Variable::dump_exposed(&d, &opt));
    ASSERT_EQ(6u, d.lines.size());
    ASSERT_EQ("comment hist_dump_exposed_test histogram", d.lines[0]);
    ASSERT_EQ("mvar_hist_dump_exposed_test_bucket{le=\"10\"} 1", d.lines[1]);
    ASSERT_EQ("mvar_hist_dump_exposed_test_bucket{le=\"20\"} 1", d.lines[2]);
    ASSERT_EQ("mvar_hist_dump_exposed_test_bucket{le=\"+Inf\"} 2", d.lines[3]);
    ASSERT_EQ("mvar_hist_dump_exposed_test_sum 30", d.lines[4]);
    ASSERT_EQ("mvar_hist_dump_exposed_test_count 2", d.lines[5]);
}

TEST_F(HistogramTest, expose_and_describe_exposed) {
    bvar::Histogram h({10, 20});
    ASSERT_EQ(0, h.expose("hist_expose_test"));
    ASSERT_EQ("hist_expose_test", h.name());
    h << 100;
    ASSERT_EQ(h.get_description(),
              bvar::Variable::describe_exposed("hist_expose_test"));
    // A distribution can't be plotted as a series.
    std::ostringstream os;
    bvar::SeriesOptions so;
    ASSERT_EQ(1, h.describe_series(os, so));
}

TEST_F(HistogramTest, requires_explicit_schema) {
    static_assert(!std::is_default_constructible<bvar::Histogram>::value,
                  "Histogram requires explicit bucket bounds");
    typedef bvar::MultiDimension<bvar::Histogram> Multi;
    typedef Multi::key_type Keys;
    typedef butil::StringPiece Name;
    static_assert(!std::is_constructible<Multi, const Keys&>::value, "no schema");
    static_assert(!std::is_constructible<Multi, Name, const Keys&>::value, "no schema");
    static_assert(!std::is_constructible<Multi, Name, Name, const Keys&>::value,
                  "no schema");
    static_assert(std::is_constructible<Multi, const Keys&, bvar::Histogram::BucketSchema>::value,
                  "explicit schema");
    static_assert(std::is_constructible<Multi, Name, const Keys&,
                  bvar::Histogram::BucketSchema>::value, "explicit schema");
    static_assert(std::is_constructible<Multi, Name, Name, const Keys&,
                  bvar::Histogram::BucketSchema>::value, "explicit schema");
    static_assert(!std::is_constructible<Multi, Name, const Keys&, int>::value,
                  "invalid schema argument");
}

TEST_F(HistogramTest, multi_dimension) {
    bvar::MultiDimension<bvar::Histogram> mhist("hist_mvar_test",
                                                {"method", "status"},
                                                test_latency_schema());
    bvar::Histogram* h = mhist.get_stats({"echo", "200"});
    ASSERT_TRUE(h != nullptr);
    *h << 15;
    ASSERT_EQ(1, h->count());
    ASSERT_EQ(1u, mhist.count_stats());
}

// A Histogram is one family, whatever the number of series inside it.
TEST_F(HistogramTest, list_metric_families) {
    std::vector<bvar::MetricFamily> families = bvar::Histogram::list_metric_families();
    ASSERT_EQ(1u, families.size());
    ASSERT_STREQ("", families[0].suffix);
    ASSERT_STREQ("histogram", families[0].type);
    ASSERT_EQ(std::vector<std::string>({"le"}), families[0].reserved_labels);

    families = bvar::LatencyRecorder::list_metric_families();
    ASSERT_EQ(5u, families.size());
    ASSERT_EQ(std::vector<std::string>({"quantile"}),
              families[0].reserved_labels);
    ASSERT_TRUE(families[1].reserved_labels.empty());
    ASSERT_TRUE(families[2].reserved_labels.empty());
    ASSERT_TRUE(families[3].reserved_labels.empty());
    ASSERT_TRUE(families[4].reserved_labels.empty());
}

// The labels of the enclosing MultiDimension share the brace group with `le`.
TEST_F(HistogramTest, dump_samples) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20}));
    h << 5 << 25;

    RecordingDumper d;
    ASSERT_TRUE(h.dump_samples(&d, 0, "foo", "method=\"echo\""));

    // No comment line: the caller owns the TYPE of the whole family.
    ASSERT_EQ(5u, d.lines.size());
    ASSERT_EQ("mvar_foo_bucket{method=\"echo\",le=\"10\"} 1", d.lines[0]);
    ASSERT_EQ("mvar_foo_bucket{method=\"echo\",le=\"20\"} 1", d.lines[1]);
    ASSERT_EQ("mvar_foo_bucket{method=\"echo\",le=\"+Inf\"} 2", d.lines[2]);
    ASSERT_EQ("mvar_foo_sum{method=\"echo\"} 30", d.lines[3]);
    ASSERT_EQ("mvar_foo_count{method=\"echo\"} 2", d.lines[4]);
}

TEST_F(HistogramTest, multi_dimension_dump) {
    const bvar::Histogram::BucketSchema schema = test_latency_schema();
    bvar::MultiDimension<bvar::Histogram> mhist("hist_mvar_dump_test",
                                                {"method", "status"}, schema);
    *mhist.get_stats({"echo", "200"}) << 5 << 25;
    *mhist.get_stats({"echo", "500"}) << 15;

    // One metric per bucket of the schema, plus _sum and _count.
    const size_t nmetrics_per_stats = schema.num_buckets() + 2;
    const size_t nbuckets = schema.num_buckets();

    RecordingDumper d;
    bvar::DumpOptions opt;
    // The count is metrics, not label sets: every bucket + _sum + _count for
    // each of the two label sets. Keeping it that way is what makes the cap of
    // FLAGS_bvar_max_dump_multi_dimension_metric_number mean lines of output.
    ASSERT_EQ(2u * nmetrics_per_stats, mhist.dump(&d, &opt));

    // Exactly one TYPE line for the family, and it comes first.
    ASSERT_EQ(1u + 2 * nmetrics_per_stats, d.lines.size());
    ASSERT_EQ("comment hist_mvar_dump_test histogram", d.lines[0]);

    // FlatMap gives no order across label sets, but each of them must be one
    // uninterrupted run of the buckets of the schema + _sum + _count.
    std::map<std::string, std::vector<std::string> > blocks;
    for (size_t i = 1; i < d.lines.size(); i += nmetrics_per_stats) {
        std::string& first = d.lines[i];
        std::string labels =
            first.substr(first.find('{'), first.find(",le=") - first.find('{'));
        ASSERT_TRUE(blocks.find(labels) == blocks.end())
            << "label set " << labels << " is not contiguous";
        blocks[labels].assign(d.lines.begin() + i,
                              d.lines.begin() + i + nmetrics_per_stats);
    }
    ASSERT_EQ(2u, blocks.size());

    std::vector<std::string>& ok =
        blocks["{method=\"echo\",status=\"200\""];
    ASSERT_EQ(nmetrics_per_stats, ok.size());
    std::string prefix =
        "mvar_hist_mvar_dump_test_bucket{method=\"echo\",status=\"200\",le=\"";
    // The test schema starts at 10 and doubles: 5 is in the first bucket,
    // 25 in the second one whose bound is 40.
    ASSERT_EQ(prefix + "10\"} 1", ok[0]);
    ASSERT_EQ(prefix + "20\"} 1", ok[1]);
    ASSERT_EQ(prefix + "40\"} 2", ok[2]);
    // The +Inf bucket closes the run and, being cumulative, equals _count.
    ASSERT_EQ(prefix + "+Inf\"} 2", ok[nbuckets - 1]);
    ASSERT_EQ("mvar_hist_mvar_dump_test_sum"
              "{method=\"echo\",status=\"200\"} 30", ok[nbuckets]);
    ASSERT_EQ("mvar_hist_mvar_dump_test_count"
              "{method=\"echo\",status=\"200\"} 2", ok[nbuckets + 1]);

    std::vector<std::string>& err =
        blocks["{method=\"echo\",status=\"500\""];
    ASSERT_EQ(nmetrics_per_stats, err.size());
    ASSERT_EQ("mvar_hist_mvar_dump_test_count"
              "{method=\"echo\",status=\"500\"} 1", err[nbuckets + 1]);
}

// An empty MultiDimension<Histogram> must not leave a dangling TYPE line.
TEST_F(HistogramTest, multi_dimension_dump_when_empty) {
    bvar::MultiDimension<bvar::Histogram> mhist("hist_mvar_empty_test", {"method"},
                                                bvar::Histogram::BucketSchema({10, 20}));
    RecordingDumper d;
    bvar::DumpOptions opt;
    ASSERT_EQ(0u, mhist.dump(&d, &opt));
    ASSERT_TRUE(d.lines.empty());
}

// A false from the dumper stops the whole dump, as it does for a plain
// Variable, and the count only covers what the dumper accepted.
TEST_F(HistogramTest, multi_dimension_dump_stops_on_failure) {
    bvar::MultiDimension<bvar::Histogram> mhist("hist_mvar_failing_test",
                                                {"method"},
                                                bvar::Histogram::BucketSchema({10, 20}));
    *mhist.get_stats({"echo"}) << 5;
    *mhist.get_stats({"write"}) << 5;
    bvar::DumpOptions opt;

    // Fail on the TYPE line: no label set is dumped at all.
    FailingDumper on_comment(0);
    ASSERT_EQ(0u, mhist.dump(&on_comment, &opt));
    ASSERT_EQ(1, on_comment.count());

    // Fail on the _sum of whichever label set comes first. The 3 buckets ahead
    // of it were dumped, and the second label set is not attempted: 11 lines
    // would have come out had the dump run to the end.
    FailingDumper on_sum(4);
    ASSERT_EQ(3u, mhist.dump(&on_sum, &opt));
    ASSERT_EQ(5, on_sum.count());
}

// The explicit schema reaches every label combination through the value factory.
TEST_F(HistogramTest, multi_dimension_with_custom_schema) {
    bvar::MultiDimension<bvar::Histogram> mhist(
        "hist_mvar_schema_test", {"method"},
        bvar::Histogram::BucketSchema({10, 20}));

    bvar::Histogram* h = mhist.get_stats({"echo"});
    ASSERT_TRUE(h != nullptr);
    std::vector<double> expected{10, 20};
    ASSERT_EQ(expected, h->schema().bounds());
    // The schema reaches values created later too, not just the first one.
    ASSERT_EQ(expected, mhist.get_stats({"write"})->schema().bounds());

    *h << 5 << 25;
    RecordingDumper d;
    bvar::DumpOptions opt;
    mhist.dump(&d, &opt);
    // One TYPE line, then the 3 buckets of the schema + _sum + _count for each
    // of the two label sets. FlatMap gives no order across them, so look the
    // line up instead of indexing into the dump.
    ASSERT_EQ(1u + 2 * 5u, d.lines.size());
    ASSERT_NE(d.lines.end(),std::find(d.lines.begin(), d.lines.end(),
                                      "mvar_hist_mvar_schema_test_bucket"
                                      "{method=\"echo\",le=\"+Inf\"} 2"));
}

// A series is a list of numbers for /vars to plot. A distribution is not one,
// so an exposed Window<Histogram> must not keep a series: nothing could draw it
// and it would cost 174 samples of a value this wide.
TEST_F(HistogramTest, exposed_window_has_no_series) {
    ASSERT_FALSE(bvar::detail::HasPlottableSeries<bvar::Histogram::Value>::value);
    ASSERT_TRUE(bvar::detail::HasPlottableSeries<int64_t>::value);
    // Otherwise nothing below would keep a series in the first place.
    ASSERT_TRUE(bvar::FLAGS_save_series);

    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20}));
    bvar::Window<bvar::Histogram> w("hist_window_series_test", &h, 2);
    std::ostringstream os;
    bvar::SeriesOptions options;
    // 1 is what Variable::describe_series() answers when there is no series.
    ASSERT_EQ(1, bvar::Variable::describe_series_exposed("hist_window_series_test", os, options));
    ASSERT_TRUE(os.str().empty());

    // A window over a number does keep one, the opt-out is not global.
    bvar::Adder<int64_t> a;
    bvar::Window<bvar::Adder<int64_t> > wa("adder_window_series_test", &a, 2);
    ASSERT_EQ(0, bvar::Variable::describe_series_exposed("adder_window_series_test", os, options));
    ASSERT_FALSE(os.str().empty());
}

TEST_F(HistogramTest, window) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({0.5, 1.5, 3.0}));
    bvar::Window<bvar::Histogram> w(&h, 10);

    h << 0.25 << 1.25 << 2.5;

    // Window values are produced by the global sampler thread. Wait until it
    // observes all writes instead of assuming a fixed sleep covers a sampling
    // round, which is not guaranteed when the test host is busy.
    bvar::Histogram::Value wv;
    for (int i = 0; i < 50; ++i) {
        wv = w.get_value();
        if (wv.num == 3) {
            break;
        }
        usleep(100 * 1000);
    }

    // Everything recorded, all of it inside the window.
    ASSERT_EQ(3, wv.num);
    ASSERT_DOUBLE_EQ(4.0, wv.sum);
    ASSERT_EQ(1u, wv.counts[0]);
    ASSERT_EQ(1u, wv.counts[1]);
    ASSERT_EQ(1u, wv.counts[2]);

    // Unlike Percentile, the underlying histogram is not reset by sampling:
    // its op has an inverse, so the window is a difference of two snapshots.
    ASSERT_EQ(3, h.count());
    ASSERT_DOUBLE_EQ(4.0, h.sum());
}

TEST_F(HistogramTest, window_forgets_old_samples) {
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));
    bvar::Window<bvar::Histogram> w(&h, 1);
    h << 5;
    sleep(2);
    // The value recorded 2 seconds ago has left the 1-second window.
    ASSERT_EQ(0, w.get_value().num);
    // But the histogram itself still remembers it.
    ASSERT_EQ(1, h.count());
}

struct AddArgs {
    bvar::Histogram* h;
    int64_t nvalues;
};

static void* add_values(void* arg) {
    AddArgs* args = (AddArgs*)arg;
    for (int64_t i = 0; i < args->nvalues; ++i) {
        // Cycle over the buckets so that every one of them is contended.
        *args->h << (i % 40);
    }
    return nullptr;
}

TEST_F(HistogramTest, multithreaded) {
    int64_t nvalues = 20000;
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));

    pthread_t threads[8];
    AddArgs args = {&h, nvalues};
    for (size_t i = 0; i < arraysize(threads); ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], nullptr, add_values, &args));
    }
    for (size_t i = 0; i < arraysize(threads); ++i) {
        ASSERT_EQ(0, pthread_join(threads[i], nullptr));
    }

    int64_t nrecords = (int64_t)arraysize(threads) * nvalues;
    bvar::Histogram::Value v = h.get_value();
    ASSERT_EQ(nrecords, v.num);
    // 0..39 repeated, so each bucket gets a quarter of the values.
    uint64_t per_bucket = (uint64_t)nrecords / 4;
    ASSERT_EQ(per_bucket + nrecords / 40, v.counts[0]);  // 0..10
    ASSERT_EQ(v.num, (int64_t)(v.counts[0] + v.counts[1] +
                               v.counts[2] + v.counts[3]));
    int64_t expected_sum = 0;
    for (int64_t i = 0; i < 40; ++i) {
        expected_sum += i;
    }
    ASSERT_DOUBLE_EQ((double)expected_sum * nrecords / 40, v.sum);
}

// What a thread recorded outlives it: the AgentCombiner backend commits a
// dying agent into its global result, the babylon one keeps the slot of an
// exited thread around for whichever thread inherits its id later.
TEST_F(HistogramTest, values_of_dead_threads_are_kept) {
    int64_t nvalues = 100;
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));
    AddArgs args = {&h, nvalues};
    for (int64_t round = 1; round <= 3; ++round) {
        pthread_t th;
        ASSERT_EQ(0, pthread_create(&th, nullptr, add_values, &args));
        ASSERT_EQ(0, pthread_join(th, nullptr));
        // Every round runs on a thread which is gone by the time count() reads
        // what it recorded, and the next one may well reuse its slot.
        ASSERT_EQ(nvalues * round, h.count());
    }
}

static void check_snapshots_while_recording(bvar::Histogram* h,
                                            int64_t nrecords) {
    bvar::Histogram::Value v = h->get_value();
    std::vector<uint64_t> last_counts(v.num_buckets, 0);
    for (int i = 0; i < 1000 || v.num < nrecords; ++i) {
        v = h->get_value();
        uint64_t total = 0;
        for (size_t b = 0; b < last_counts.size(); ++b) {
            total += v.counts[b];
            // Every sample is taken after the previous one returned and a
            // bucket count only ever grows, so this snapshot cannot hold less
            // than the last one did.
            ASSERT_LE(last_counts[b], v.counts[b]) << "i=" << i << " bucket=" << b;
            last_counts[b] = v.counts[b];
        }
        ASSERT_EQ(v.num, (int64_t)total) << "i=" << i;
    }
}

// The invariant the per thread seqlock buys, the mutex of the ElementContainer
// without WITH_BABYLON_COUNTER: a snapshot never mixes a bucket that has
// already been incremented with a `num` that has not. It holds for the slice
// of one thread, and summing consistent slices keeps it, so the whole snapshot
// still satisfies the `+Inf bucket == _count` rule of the prometheus format
// while other threads are recording.
TEST_F(HistogramTest, snapshot_is_self_consistent_under_contention) {
    int64_t nvalues = 50000;
    bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));

    pthread_t threads[4];
    AddArgs args = {&h, nvalues};
    for (size_t i = 0; i < arraysize(threads); ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], nullptr, add_values, &args));
    }

    int64_t nrecords = (int64_t)arraysize(threads) * nvalues;
    check_snapshots_while_recording(&h, nrecords);

    for (size_t i = 0; i < arraysize(threads); ++i) {
        ASSERT_EQ(0, pthread_join(threads[i], nullptr));
    }
    ASSERT_EQ(nrecords, h.count());
}

static const size_t PERF_OPS_PER_THREAD = 500000;

struct PerfArgs {
    bvar::Histogram* h;
    int64_t elapsed_ns;
};

static void* record_into_histogram(void* arg) {
    PerfArgs* args = (PerfArgs*)arg;
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < PERF_OPS_PER_THREAD; ++i) {
        *args->h << (double)(i % 40);
    }
    timer.stop();
    args->elapsed_ns = timer.n_elapsed();
    return nullptr;
}

static double time_records(bvar::Histogram* h, size_t nthread) {
    PerfArgs proto = {h, 0};
    std::vector<PerfArgs> args(nthread, proto);
    std::vector<pthread_t> threads(nthread);
    for (size_t i = 0; i < nthread; ++i) {
        EXPECT_EQ(0, pthread_create(&threads[i], nullptr,
                                    record_into_histogram, &args[i]));
    }
    int64_t total_ns = 0;
    for (size_t i = 0; i < nthread; ++i) {
        EXPECT_EQ(0, pthread_join(threads[i], nullptr));
        total_ns += args[i].elapsed_ns;
    }
    return (double)total_ns / (double)(PERF_OPS_PER_THREAD * nthread);
}

TEST_F(HistogramTest, write_perf) {
#if WITH_BABYLON_COUNTER
    const char* backend = "babylon";
#else
    const char* backend = "combiner";
#endif // WITH_BABYLON_COUNTER
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "threads\t" << backend << " (ns per record)\n";
    for (size_t nthread = 1; nthread <= 8; nthread *= 2) {
        bvar::Histogram h(bvar::Histogram::BucketSchema({10, 20, 30}));
        double ns = time_records(&h, nthread);
        ASSERT_EQ((int64_t)(PERF_OPS_PER_THREAD * nthread), h.count());
        oss << nthread << '\t' << ns << '\n';
    }
    LOG(INFO) << "Histogram write performance:\n" << oss.str();
}

}  // namespace
