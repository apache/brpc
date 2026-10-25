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

// Date 2014/10/13 19:47:59

#include <pthread.h>                                // pthread_*

#include <cmath>
#include <cstddef>
#include <memory>
#include <iostream>
#include <vector>
#include "butil/time.h"
#include "butil/macros.h"
#include "bvar/recorder.h"
#include "bvar/latency_recorder.h"
#include <gtest/gtest.h>

namespace {
#if !WITH_BABYLON_COUNTER
TEST(RecorderTest, test_complement) {
    LOG(INFO) << "sizeof(LatencyRecorder)=" << sizeof(bvar::LatencyRecorder)
              << " " << sizeof(bvar::detail::Percentile)
              << " " << sizeof(bvar::Maxer<int64_t>)
              << " " << sizeof(bvar::IntRecorder)
              << " " << sizeof(bvar::Window<bvar::IntRecorder>)
              << " " << sizeof(bvar::Window<bvar::detail::Percentile>);

    for (int a = -10000000; a < 10000000; ++a) {
        const uint64_t complement = bvar::IntRecorder::_get_complement(a);
        const int64_t b = bvar::IntRecorder::_extend_sign_bit(complement);
        ASSERT_EQ(a, b);
    }
}

TEST(RecorderTest, test_compress) {
    const uint64_t num = 125345;
    const uint64_t sum = 26032906;
    const uint64_t compressed = bvar::IntRecorder::_compress(num, sum);
    ASSERT_EQ(num, bvar::IntRecorder::_get_num(compressed));
    ASSERT_EQ(sum, bvar::IntRecorder::_get_sum(compressed));
}

TEST(RecorderTest, test_compress_negtive_number) {
    for (int a = -10000000; a < 10000000; ++a) {
        const uint64_t sum = bvar::IntRecorder::_get_complement(a);
        const uint64_t num = 123456;
        const uint64_t compressed = bvar::IntRecorder::_compress(num, sum);
        ASSERT_EQ(num, bvar::IntRecorder::_get_num(compressed));
        ASSERT_EQ(a, bvar::IntRecorder::_extend_sign_bit(bvar::IntRecorder::_get_sum(compressed)));
    }
}
#endif // !WITH_BABYLON_COUNTER

TEST(RecorderTest, sanity) {
    {
        bvar::IntRecorder recorder;
        ASSERT_TRUE(recorder.valid());
        ASSERT_EQ(0, recorder.expose("var1"));
        for (size_t i = 0; i < 100; ++i) {
            recorder << 2;
        }
        ASSERT_EQ(2l, recorder.average());
        ASSERT_EQ("2", bvar::Variable::describe_exposed("var1"));
        std::vector<std::string> vars;
        bvar::Variable::list_exposed(&vars);
        ASSERT_EQ(1UL, vars.size());
        ASSERT_EQ("var1", vars[0]);
        ASSERT_EQ(1UL, bvar::Variable::count_exposed());
    }
    {
        bvar::IntRecorder recorder("var2");
        recorder << 2;
        ASSERT_EQ(2l, recorder.average());
    }
    ASSERT_EQ(0UL, bvar::Variable::count_exposed());
}

TEST(RecorderTest, window) {
    bvar::IntRecorder c1;
    ASSERT_TRUE(c1.valid());
    bvar::Window<bvar::IntRecorder> w1(&c1, 1);
    bvar::Window<bvar::IntRecorder> w2(&c1, 2);
    bvar::Window<bvar::IntRecorder> w3(&c1, 3);

    const int N = 10000;
    int64_t last_time = butil::cpuwide_time_us();
    for (int i = 1; i <= N; ++i) {
        c1 << i;
        int64_t now = butil::cpuwide_time_us();
        if (now - last_time >= 1000000L) {
            last_time = now;
            LOG(INFO) << "c1=" << c1 << " w1=" << w1 << " w2=" << w2 << " w3=" << w3;
        } else {
            usleep(950);
        }
    }
}

TEST(RecorderTest, negative) {
    bvar::IntRecorder recorder;
    ASSERT_TRUE(recorder.valid());
    for (size_t i = 0; i < 3; ++i) {
        recorder << -2;
    }
    ASSERT_EQ(-2, recorder.average());
}

TEST(RecorderTest, positive_overflow) {
    bvar::IntRecorder recorder1;
    ASSERT_TRUE(recorder1.valid());
    for (int i = 0; i < 5; ++i) {
        recorder1 << std::numeric_limits<int64_t>::max();
    }
    ASSERT_EQ(std::numeric_limits<int>::max(), recorder1.average());

    bvar::IntRecorder recorder2;
    ASSERT_TRUE(recorder2.valid());
    recorder2.set_debug_name("recorder2");
    for (int i = 0; i < 5; ++i) {
        recorder2 << std::numeric_limits<int64_t>::max();
    }
    ASSERT_EQ(std::numeric_limits<int>::max(), recorder2.average());

    bvar::IntRecorder recorder3;
    ASSERT_TRUE(recorder3.valid());
    recorder3.expose("recorder3");
    for (int i = 0; i < 5; ++i) {
        recorder3 << std::numeric_limits<int64_t>::max();
    }
    ASSERT_EQ(std::numeric_limits<int>::max(), recorder3.average());

    bvar::LatencyRecorder latency1;
    latency1.expose("latency1");
    latency1 << std::numeric_limits<int64_t>::max();

    bvar::LatencyRecorder latency2;
    latency2 << std::numeric_limits<int64_t>::max();
}

TEST(RecorderTest, negtive_overflow) {
    bvar::IntRecorder recorder1;
    ASSERT_TRUE(recorder1.valid());
    for (int i = 0; i < 5; ++i) {
        recorder1 << std::numeric_limits<int64_t>::min();
    }
    ASSERT_EQ(std::numeric_limits<int>::min(), recorder1.average());

    bvar::IntRecorder recorder2;
    ASSERT_TRUE(recorder2.valid());
    recorder2.set_debug_name("recorder2");
    for (int i = 0; i < 5; ++i) {
        recorder2 << std::numeric_limits<int64_t>::min();
    }
    ASSERT_EQ(std::numeric_limits<int>::min(), recorder2.average());

    bvar::IntRecorder recorder3;
    ASSERT_TRUE(recorder3.valid());
    recorder3.expose("recorder3");
    for (int i = 0; i < 5; ++i) {
        recorder3 << std::numeric_limits<int64_t>::min();
    }
    ASSERT_EQ(std::numeric_limits<int>::min(), recorder3.average());

    bvar::LatencyRecorder latency1;
    latency1.expose("latency1");
    latency1 << std::numeric_limits<int64_t>::min();

    bvar::LatencyRecorder latency2;
    latency2 << std::numeric_limits<int64_t>::min();
}

const size_t OPS_PER_THREAD = 20000000;

static void *thread_counter(void *arg) {
    bvar::IntRecorder *recorder = (bvar::IntRecorder *)arg;
    butil::Timer timer;
    timer.start();
    for (int i = 0; i < (int)OPS_PER_THREAD; ++i) {
        *recorder << i;
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

TEST(RecorderTest, perf) {
    bvar::IntRecorder recorder;
    ASSERT_TRUE(recorder.valid());
    pthread_t threads[8];
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], nullptr, &thread_counter, (void *)&recorder);
    }
    long totol_time = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        void *ret; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    ASSERT_EQ(((int64_t)OPS_PER_THREAD - 1) / 2, recorder.average());
    LOG(INFO) << "Recorder takes " << totol_time / (OPS_PER_THREAD * ARRAY_SIZE(threads)) 
              << "ns per sample with " << ARRAY_SIZE(threads) 
              << " threads";
}

class LatencyRecorderWithWindowSample : public bvar::LatencyRecorder {
public:
    explicit LatencyRecorderWithWindowSample(time_t window_size)
        : bvar::LatencyRecorder(window_size) {}

    bool get_latency_sample(bvar::detail::Sample<bvar::Stat>* sample) const {
        return _latency_window.get_span(sample);
    }

    bool get_latency_sample(time_t window_size,
                            bvar::detail::Sample<bvar::Stat>* sample) const {
        return _latency_window.get_span(window_size, sample);
    }

    bool has_full_latency_window() const {
        std::vector<bvar::Stat> samples;
        _latency_window.get_samples(&samples);
        // get_samples() excludes the oldest snapshot used by get_span().
        return samples.size() + 1 ==
            static_cast<size_t>(_latency_window.window_size());
    }
};

TEST(RecorderTest, latency_recorder_window_statistics) {
    // Keep both batches across sampler ticks. A ten-second window is longer
    // than the five-second wait budget, which allows for busy CI runners.
    const time_t window_size = 10;
    LatencyRecorderWithWindowSample recorder(window_size);
    const int64_t sampling_timeout_us = 5 * 1000000L;
    const int64_t deadline =
        butil::monotonic_time_us() + sampling_timeout_us;
    bvar::detail::Sample<bvar::Stat> sample;

    // Deliberately sample a partial batch before recording the remaining
    // values, rather than assuming all writes fall between sampler ticks.
    recorder << 1 << 2 << 3;
    while (butil::monotonic_time_us() < deadline) {
        if (recorder.get_latency_sample(&sample) && sample.data.num >= 3) {
            break;
        }
        usleep(10000);
    }
    ASSERT_EQ(3, sample.data.num) << "First batch was not sampled";
    EXPECT_EQ(6, sample.data.sum);

    recorder << 4 << 5 << 6 << 7;
    EXPECT_EQ(7, recorder.count());
    bool complete_sample = false;
    int64_t max_latency = 0;
    while (butil::monotonic_time_us() < deadline) {
        if (!complete_sample && recorder.get_latency_sample(&sample) &&
            sample.data.num >= 7) {
            complete_sample = true;
        }
        if (max_latency < 7) {
            max_latency = recorder.max_latency();
        }
        if (complete_sample && max_latency >= 7) {
            break;
        }
        usleep(10000);
    }

    // Validate completeness before checking sum / count from the same saved
    // snapshot. Rereading latency() could observe a different sampling span.
    ASSERT_TRUE(complete_sample) << "Full batch was not sampled; count="
                                 << sample.data.num;
    ASSERT_EQ(7, sample.data.num);
    EXPECT_EQ(28, sample.data.sum);
    EXPECT_EQ(4, sample.data.get_average_int());
    // The maximum has an independent sampler and is saved separately.
    EXPECT_EQ(7, max_latency);
}

TEST(RecorderTest, latency_recorder_qps_accuracy) {
    LatencyRecorderWithWindowSample lr1_2s(2);
    LatencyRecorderWithWindowSample lr2_2s(2);
    LatencyRecorderWithWindowSample lr3_2s(2);
    LatencyRecorderWithWindowSample lr4_2s(2);
    LatencyRecorderWithWindowSample lr1_3s(3);
    LatencyRecorderWithWindowSample lr2_3s(3);
    LatencyRecorderWithWindowSample lr3_3s(3);
    LatencyRecorderWithWindowSample lr4_3s(3);

    auto write = [](bvar::LatencyRecorder& lr, int times) {   
        for (int i = 0; i < times; ++i) {
            lr << 1;
        }
    };
    write(lr1_2s, 10);
    write(lr2_2s, 11);
    write(lr3_2s, 3);
    write(lr4_2s, 1);
    write(lr1_3s, 10);
    write(lr2_3s, 11);
    write(lr3_3s, 3);
    write(lr4_3s, 1);

    auto check_qps = [](LatencyRecorderWithWindowSample& lr, int count,
                        time_t window_size, bool use_default_window) {
        const int64_t deadline = butil::monotonic_time_us() + 5 * 1000000L;
        while (butil::monotonic_time_us() < deadline) {
            bvar::detail::Sample<bvar::Stat> before;
            if (lr.get_latency_sample(window_size, &before) &&
                lr.has_full_latency_window() &&
                before.data.num == count && before.time_us > 0) {
                // qps() reads the window again. Only compare it with `before'
                // when no sampler tick occurs between the two reads.
                const int64_t qps = use_default_window
                    ? lr.qps() : lr.qps(window_size);
                bvar::detail::Sample<bvar::Stat> after;
                if (!lr.get_latency_sample(window_size, &after) ||
                    after.time_us != before.time_us) {
                    continue;
                }
                const double expected_qps =
                    before.data.num * 1000000.0 / before.time_us;
                EXPECT_GE(qps, static_cast<int64_t>(std::floor(expected_qps)));
                EXPECT_LE(qps, static_cast<int64_t>(std::ceil(expected_qps)));
                return;
            }
            usleep(10000);
        }
        ADD_FAILURE() << "Recorded latencies were not sampled";
    };

    check_qps(lr1_2s, 10, 2, true);
    check_qps(lr2_2s, 11, 2, true);
    check_qps(lr3_2s, 3, 2, true);
    check_qps(lr4_2s, 1, 2, true);
    check_qps(lr1_3s, 10, 3, false);
    check_qps(lr2_3s, 11, 3, false);
    check_qps(lr3_3s, 3, 3, false);
    check_qps(lr4_3s, 1, 3, false);
}

} // namespace
