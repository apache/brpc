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

#include "brpc/policy/timeout_concurrency_limiter.h"
#include "brpc/controller.h"
#include "brpc/errno.pb.h"
#include <cmath>
#include <gflags/gflags.h>

namespace brpc {
namespace policy {

DEFINE_int32(timeout_cl_sample_window_size_ms, 1000,
             "Duration of the sampling window.");
DEFINE_int32(timeout_cl_min_sample_count, 100,
             "During the duration of the sampling window, if the number of "
             "requests collected is less than this value, the sampling window "
             "will be discarded.");
DEFINE_int32(timeout_cl_max_sample_count, 200,
             "During the duration of the sampling window, once the number of "
             "requests collected is greater than this value, even if the "
             "duration of the window has not ended, the max_concurrency will "
             "be updated and a new sampling window will be started.");
DEFINE_double(timeout_cl_sampling_interval_ms, 0.1,
              "Interval for sampling request in auto concurrency limiter");
DEFINE_int32(timeout_cl_initial_avg_latency_us, 500,
             "Initial max concurrency for gradient concurrency limiter");
DEFINE_bool(
    timeout_cl_enable_error_punish, true,
    "Whether to consider failed requests when calculating maximum concurrency");
DEFINE_double(
    timeout_cl_fail_punish_ratio, 1.0,
    "Use the failed requests to punish normal requests. The larger "
    "the configuration item, the more aggressive the penalty strategy.");
DEFINE_int32(timeout_cl_default_timeout_ms, 500,
             "Default timeout for rpc request");
DEFINE_int32(timeout_cl_max_concurrency, 100,
             "When average latency statistics not refresh, this flag can keep "
             "requests not exceed this max concurrency");

TimeoutConcurrencyLimiter::TimeoutConcurrencyLimiter()
    : _avg_latency_us(FLAGS_timeout_cl_initial_avg_latency_us),
      _last_sampling_time_us(0),
      _timeout_ms(FLAGS_timeout_cl_default_timeout_ms),
      _max_concurrency(FLAGS_timeout_cl_max_concurrency) {}

TimeoutConcurrencyLimiter::TimeoutConcurrencyLimiter(
    const TimeoutConcurrencyConf &conf)
    : _avg_latency_us(FLAGS_timeout_cl_initial_avg_latency_us),
      _last_sampling_time_us(0),
      _timeout_ms(conf.timeout_ms),
      _max_concurrency(conf.max_concurrency) {}

TimeoutConcurrencyLimiter *TimeoutConcurrencyLimiter::New(
    const AdaptiveMaxConcurrency &amc) const {
    return new (std::nothrow)
        TimeoutConcurrencyLimiter(static_cast<TimeoutConcurrencyConf>(amc));
}

bool TimeoutConcurrencyLimiter::OnRequested(int current_concurrency,
                                            Controller *cntl) {
    auto timeout_ms = _timeout_ms;
    if (cntl != nullptr && cntl->timeout_ms() != UNSET_MAGIC_NUM) {
        timeout_ms = cntl->timeout_ms();
    }
    // In extreme cases, the average latency may be greater than requested
    // timeout, allow currency_concurrency is 1 ensures the average latency can
    // be obtained renew.
    return current_concurrency == 1 ||
           (current_concurrency <= _max_concurrency &&
            _avg_latency_us < timeout_ms * 1000);
}

void TimeoutConcurrencyLimiter::OnResponded(int error_code,
                                            int64_t latency_us) {
    if (ELIMIT == error_code) {
        return;
    }

    const int64_t now_time_us = butil::gettimeofday_us();
    int64_t last_sampling_time_us =
        _last_sampling_time_us.load(butil::memory_order_relaxed);

    if (last_sampling_time_us == 0 ||
        now_time_us - last_sampling_time_us >=
            FLAGS_timeout_cl_sampling_interval_ms * 1000) {
        bool sample_this_call = _last_sampling_time_us.compare_exchange_strong(
            last_sampling_time_us, now_time_us, butil::memory_order_relaxed);
        if (sample_this_call) {
            bool sample_window_submitted =
                AddSample(error_code, latency_us, now_time_us);
            if (sample_window_submitted) {
                // The following log prints has data-race in extreme cases,
                // unless you are in debug, you should not open it.
                VLOG(1) << "Sample window submitted, current avg_latency_us:"
                        << _avg_latency_us;
            }
        }
    }
}

int TimeoutConcurrencyLimiter::MaxConcurrency() {
    return FLAGS_timeout_cl_max_concurrency;
}

bool TimeoutConcurrencyLimiter::AddSample(int error_code, int64_t latency_us,
                                          int64_t sampling_time_us) {
    std::unique_lock<butil::Mutex> lock_guard(_sw_mutex);
    if (_sw.start_time_us == 0) {
        _sw.start_time_us = sampling_time_us;
    }

    if (error_code != 0 && FLAGS_timeout_cl_enable_error_punish) {
        ++_sw.failed_count;
        _sw.total_failed_us += latency_us;
    } else if (error_code == 0) {
        ++_sw.succ_count;
        _sw.total_succ_us += latency_us;
    }

    if (_sw.succ_count + _sw.failed_count < FLAGS_timeout_cl_min_sample_count) {
        if (sampling_time_us - _sw.start_time_us >=
            FLAGS_timeout_cl_sample_window_size_ms * 1000) {
            // If the sample size is insufficient at the end of the sampling
            // window, discard the entire sampling window
            ResetSampleWindow(sampling_time_us);
        }
        return false;
    }
    if (sampling_time_us - _sw.start_time_us <
            FLAGS_timeout_cl_sample_window_size_ms * 1000 &&
        _sw.succ_count + _sw.failed_count < FLAGS_timeout_cl_max_sample_count) {
        return false;
    }

    if (_sw.succ_count > 0) {
        UpdateAvgLatency();
    } else {
        // All request failed
        AdjustAvgLatency(_avg_latency_us * 2);
    }
    ResetSampleWindow(sampling_time_us);
    return true;
}

void TimeoutConcurrencyLimiter::ResetSampleWindow(int64_t sampling_time_us) {
    _sw.start_time_us = sampling_time_us;
    _sw.succ_count = 0;
    _sw.failed_count = 0;
    _sw.total_failed_us = 0;
    _sw.total_succ_us = 0;
}

void TimeoutConcurrencyLimiter::AdjustAvgLatency(int64_t avg_latency_us) {
    _avg_latency_us = avg_latency_us;
}

void TimeoutConcurrencyLimiter::UpdateAvgLatency() {
    double failed_punish =
        _sw.total_failed_us * FLAGS_timeout_cl_fail_punish_ratio;
    auto avg_latency_us =
        std::ceil((failed_punish + _sw.total_succ_us) / _sw.succ_count);
    AdjustAvgLatency(avg_latency_us);
}

}  // namespace policy
}  // namespace brpc
