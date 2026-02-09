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

#include "brpc/policy/auto_concurrency_limiter.h"
#include "butil/time.h"
#include "bthread/bthread.h"
#include <gtest/gtest.h>

namespace brpc {
namespace policy {

DECLARE_int32(auto_cl_sample_window_size_ms);
DECLARE_int32(auto_cl_min_sample_count);
DECLARE_int32(auto_cl_max_sample_count);
DECLARE_bool(auto_cl_enable_error_punish);
DECLARE_double(auto_cl_fail_punish_ratio);
DECLARE_double(auto_cl_error_rate_punish_threshold);

}  // namespace policy
}  // namespace brpc

class AutoConcurrencyLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original values
        orig_sample_window_size_ms_ = brpc::policy::FLAGS_auto_cl_sample_window_size_ms;
        orig_min_sample_count_ = brpc::policy::FLAGS_auto_cl_min_sample_count;
        orig_max_sample_count_ = brpc::policy::FLAGS_auto_cl_max_sample_count;
        orig_enable_error_punish_ = brpc::policy::FLAGS_auto_cl_enable_error_punish;
        orig_fail_punish_ratio_ = brpc::policy::FLAGS_auto_cl_fail_punish_ratio;
        orig_error_rate_threshold_ = brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold;

        // Set test-friendly values
        brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 1000;
        brpc::policy::FLAGS_auto_cl_min_sample_count = 5;
        brpc::policy::FLAGS_auto_cl_max_sample_count = 200;
        brpc::policy::FLAGS_auto_cl_enable_error_punish = true;
        brpc::policy::FLAGS_auto_cl_fail_punish_ratio = 1.0;
    }

    void TearDown() override {
        // Restore original values
        brpc::policy::FLAGS_auto_cl_sample_window_size_ms = orig_sample_window_size_ms_;
        brpc::policy::FLAGS_auto_cl_min_sample_count = orig_min_sample_count_;
        brpc::policy::FLAGS_auto_cl_max_sample_count = orig_max_sample_count_;
        brpc::policy::FLAGS_auto_cl_enable_error_punish = orig_enable_error_punish_;
        brpc::policy::FLAGS_auto_cl_fail_punish_ratio = orig_fail_punish_ratio_;
        brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = orig_error_rate_threshold_;
    }

private:
    int32_t orig_sample_window_size_ms_;
    int32_t orig_min_sample_count_;
    int32_t orig_max_sample_count_;
    bool orig_enable_error_punish_;
    double orig_fail_punish_ratio_;
    double orig_error_rate_threshold_;
};

// Helper function to add samples and trigger window completion
// Uses synthetic timestamps instead of sleeping for faster, deterministic tests.
// The final successful sample is used as the trigger, so actual counts match
// succ_count/fail_count exactly (preserving intended error rates).
void AddSamplesAndTriggerWindow(brpc::policy::AutoConcurrencyLimiter& limiter,
                                 int succ_count, int64_t succ_latency,
                                 int fail_count, int64_t fail_latency) {
    ASSERT_GT(succ_count, 0) << "Need at least 1 success to trigger window";
    int64_t now = butil::gettimeofday_us();

    // Add successful samples (reserve one for the trigger)
    for (int i = 0; i < succ_count - 1; ++i) {
        limiter.AddSample(0, succ_latency, now);
    }
    // Add failed samples
    for (int i = 0; i < fail_count; ++i) {
        limiter.AddSample(1, fail_latency, now);
    }

    // Advance timestamp past window expiry instead of sleeping
    int64_t after_window = now + brpc::policy::FLAGS_auto_cl_sample_window_size_ms * 1000 + 1000;

    // Use the final success sample to trigger window submission
    limiter.AddSample(0, succ_latency, after_window);
}

// Test 1: Backward compatibility - threshold=0 preserves original punishment behavior
TEST_F(AutoConcurrencyLimiterTest, ThresholdZeroPreservesOriginalBehavior) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0;
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;
    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // 10% error rate, threshold=0 means full punishment applied
    // avg_latency = (10*1000 + 90*100) / 90 = 211us
    ASSERT_GT(limiter._min_latency_us, 180);
    ASSERT_LT(limiter._min_latency_us, 250);
}

// Test 2: Dead zone - error rate below threshold produces zero punishment
TEST_F(AutoConcurrencyLimiterTest, BelowThresholdZeroPunishment) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.2;  // 20% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;
    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // 10% error rate < 20% threshold, punishment should be zero
    // avg_latency = 90*100 / 90 = 100us (no inflation)
    ASSERT_GT(limiter._min_latency_us, 80);
    ASSERT_LT(limiter._min_latency_us, 130);
}

// Test 3: Boundary - error rate exactly at threshold produces zero punishment
TEST_F(AutoConcurrencyLimiterTest, ExactlyAtThresholdZeroPunishment) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;  // 10% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;
    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // 10% error rate == 10% threshold, punishment should be zero
    // avg_latency = 90*100 / 90 = 100us
    ASSERT_GT(limiter._min_latency_us, 80);
    ASSERT_LT(limiter._min_latency_us, 130);
}

// Test 4: Linear scaling - above threshold, punishment scales proportionally
TEST_F(AutoConcurrencyLimiterTest, AboveThresholdLinearScaling) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;  // 10% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    // Case A: 50% error rate
    // punish_factor = (0.5 - 0.1) / (1.0 - 0.1) = 0.444
    // failed_punish = 50 * 1000 * 0.444 = 22222us
    // avg_latency = (22222 + 50*100) / 50 = 544us
    {
        brpc::policy::AutoConcurrencyLimiter limiter;
        AddSamplesAndTriggerWindow(limiter, 50, 100, 50, 1000);
        ASSERT_GT(limiter._min_latency_us, 450);
        ASSERT_LT(limiter._min_latency_us, 650);
    }

    // Case B: 90% error rate (near full punishment)
    // punish_factor = (0.9 - 0.1) / (1.0 - 0.1) = 0.889
    // failed_punish = 90 * 1000 * 0.889 = 80000us
    // avg_latency = (80000 + 10*100) / 10 = 8100us
    {
        brpc::policy::AutoConcurrencyLimiter limiter;
        AddSamplesAndTriggerWindow(limiter, 10, 100, 90, 1000);
        ASSERT_GT(limiter._min_latency_us, 7000);
        ASSERT_LT(limiter._min_latency_us, 9000);
    }
}

