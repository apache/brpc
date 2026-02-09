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
void AddSamplesAndTriggerWindow(brpc::policy::AutoConcurrencyLimiter& limiter,
                                 int succ_count, int64_t succ_latency,
                                 int fail_count, int64_t fail_latency) {
    int64_t now = butil::gettimeofday_us();

    // Add successful samples
    for (int i = 0; i < succ_count; ++i) {
        limiter.AddSample(0, succ_latency, now);
    }
    // Add failed samples
    for (int i = 0; i < fail_count; ++i) {
        limiter.AddSample(1, fail_latency, now);
    }

    // Wait for window to expire and trigger update
    bthread_usleep(brpc::policy::FLAGS_auto_cl_sample_window_size_ms * 1000 + 1000);

    // Add one more sample to trigger window submission
    limiter.AddSample(0, succ_latency, butil::gettimeofday_us());
}

// Test: When threshold is 0 (default), behavior is unchanged - punishment is applied
TEST_F(AutoConcurrencyLimiterTest, ThresholdZeroPreservesOriginalBehavior) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0;
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;  // Short window for testing

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // With threshold=0, failed_punish should NOT be attenuated
    // avg_latency = (10*1000 + 90*100) / 90 = (10000 + 9000) / 90 = 211us
    // This is significantly inflated from the actual success latency of 100us
    // _min_latency_us should reflect this inflation
    ASSERT_GT(limiter._min_latency_us, 150);  // Should be inflated
}

// Test: When error rate is below threshold, punishment is zero
TEST_F(AutoConcurrencyLimiterTest, BelowThresholdZeroPunishment) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.2;  // 20% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // With 10% error rate < 20% threshold, punishment should be zero
    // avg_latency should be close to actual success latency of 100us
    ASSERT_LT(limiter._min_latency_us, 150);  // Should NOT be inflated
    ASSERT_GT(limiter._min_latency_us, 50);   // Should be valid (around 100us)
}

// Test: When error rate is above threshold, punishment scales linearly
TEST_F(AutoConcurrencyLimiterTest, AboveThresholdLinearScaling) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;  // 10% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 50, 100, 50, 1000);

    // With 50% error rate > 10% threshold:
    // punish_factor = (0.5 - 0.1) / (1.0 - 0.1) = 0.4 / 0.9 = 0.444
    // failed_punish = 50 * 1000 * 1.0 * 0.444 = 22222us
    // avg_latency = (22222 + 50*100) / 50 = (22222 + 5000) / 50 = 544us
    // This should be inflated, but less than threshold=0 case
    ASSERT_GT(limiter._min_latency_us, 200);  // Should be somewhat inflated
}

// Test: Edge case - error rate exactly at threshold
TEST_F(AutoConcurrencyLimiterTest, ExactlyAtThresholdZeroPunishment) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;  // 10% threshold
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 90, 100, 10, 1000);

    // At exactly threshold, punishment should be zero (boundary case)
    // avg_latency should be close to actual success latency of 100us
    ASSERT_LT(limiter._min_latency_us, 150);
}

// Test: No failed requests - threshold has no effect
TEST_F(AutoConcurrencyLimiterTest, NoFailedRequestsThresholdNoEffect) {
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 100, 100, 0, 0);

    // No failed requests, so threshold logic shouldn't trigger
    ASSERT_GT(limiter._min_latency_us, 0);    // Should have valid latency
    ASSERT_LT(limiter._min_latency_us, 150);  // Should be close to 100us
}

// Test: Compare punishment at different thresholds for same error rate
TEST_F(AutoConcurrencyLimiterTest, DifferentThresholdsDifferentPunishment) {
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    // Test with threshold = 0 (original behavior)
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0;
    brpc::policy::AutoConcurrencyLimiter limiter1;
    AddSamplesAndTriggerWindow(limiter1, 95, 100, 5, 1000);  // 5% error rate
    int64_t latency_threshold_0 = limiter1._min_latency_us;

    // Test with threshold = 0.1 (5% < 10%, in dead zone)
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;
    brpc::policy::AutoConcurrencyLimiter limiter2;
    AddSamplesAndTriggerWindow(limiter2, 95, 100, 5, 1000);  // 5% error rate
    int64_t latency_threshold_10 = limiter2._min_latency_us;

    // With threshold=0, latency should be inflated
    // With threshold=0.1 and 5% error rate (below threshold), latency should not be inflated
    ASSERT_GT(latency_threshold_0, latency_threshold_10);
}

// Test: Verify linear scaling formula
TEST_F(AutoConcurrencyLimiterTest, LinearScalingFormula) {
    // At 90% error rate, punishment factor should be 0.889
    brpc::policy::FLAGS_auto_cl_error_rate_punish_threshold = 0.1;
    brpc::policy::FLAGS_auto_cl_sample_window_size_ms = 10;

    brpc::policy::AutoConcurrencyLimiter limiter;

    AddSamplesAndTriggerWindow(limiter, 10, 100, 90, 1000);

    // With 90% error rate > 10% threshold:
    // punish_factor = (0.9 - 0.1) / (1.0 - 0.1) = 0.8 / 0.9 = 0.889
    // High punishment factor, latency should be significantly inflated
    ASSERT_GT(limiter._min_latency_us, 500);
}

