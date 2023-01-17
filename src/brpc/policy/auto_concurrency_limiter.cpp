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

#include <cmath>
#include <gflags/gflags.h>
#include "brpc/errno.pb.h"
#include "brpc/policy/auto_concurrency_limiter.h"

namespace bthread {

DECLARE_int32(bthread_concurrency);

}  // namespace bthread

namespace brpc {
namespace policy {

DEFINE_int32(auto_cl_sample_window_size_ms, 1000, "Duration of the sampling window.");
DEFINE_int32(auto_cl_min_sample_count, 100,
             "During the duration of the sampling window, if the number of "
             "requests collected is less than this value, the sampling window "
             "will be discarded.");
DEFINE_int32(auto_cl_max_sample_count, 200,
             "During the duration of the sampling window, once the number of "
             "requests collected is greater than this value, even if the "
             "duration of the window has not ended, the max_concurrency will "
             "be updated and a new sampling window will be started.");
DEFINE_double(auto_cl_sampling_interval_ms, 0.1, 
             "Interval for sampling request in auto concurrency limiter");
DEFINE_int32(auto_cl_initial_max_concurrency, 40,
             "Initial max concurrency for gradient concurrency limiter");
DEFINE_int32(auto_cl_noload_latency_remeasure_interval_ms, 50000, 
             "Interval for remeasurement of noload_latency. In the period of "
             "remeasurement of noload_latency will halve max_concurrency.");
DEFINE_double(auto_cl_alpha_factor_for_ema, 0.1,
              "The smoothing coefficient used in the calculation of ema, "
              "the value range is 0-1. The smaller the value, the smaller "
              "the effect of a single sample_window on max_concurrency.");
DEFINE_bool(auto_cl_enable_error_punish, true,
            "Whether to consider failed requests when calculating maximum concurrency");
DEFINE_double(auto_cl_fail_punish_ratio, 1.0,
              "Use the failed requests to punish normal requests. The larger "
              "the configuration item, the more aggressive the penalty strategy.");
DEFINE_double(auto_cl_max_explore_ratio, 0.3, 
              "The larger the value, the higher the tolerance of the server to "
              "the fluctuation of latency at low load, and the the greater the "
              "maximum growth rate of qps. Correspondingly, the server will have "
              "a higher latency for a short period of time after the overload.");
DEFINE_double(auto_cl_min_explore_ratio, 0.06,
              "Auto concurrency limiter will perform fault tolerance based on "
              "this parameter when judging the load situation of the server. "
              "It should be a positive value close to 0, the larger it is, "
              "the higher the latency of the server at full load.");
DEFINE_double(auto_cl_change_rate_of_explore_ratio, 0.02, 
              "The speed of change of auto_cl_max_explore_ratio when the "
              "load situation of the server changes, The value range is "
              "(0 - `max_explore_ratio')");
DEFINE_double(auto_cl_reduce_ratio_while_remeasure, 0.9, 
              "This value affects the reduction ratio to mc during retesting "
              "noload_latency. The value range is (0-1)");
DEFINE_int32(auto_cl_latency_fluctuation_correction_factor, 1,
             "Affect the judgement of the server's load situation. The larger "
             "the value, the higher the tolerance for the fluctuation of the "
             "latency. If the value is too large, the latency will be higher "
             "when the server is overloaded.");

AutoConcurrencyLimiter::AutoConcurrencyLimiter()
    : _max_concurrency(FLAGS_auto_cl_initial_max_concurrency)
    , _remeasure_start_us(NextResetTime(butil::gettimeofday_us()))
    , _reset_latency_us(0)
    , _min_latency_us(-1)
    , _ema_max_qps(-1)
    , _explore_ratio(FLAGS_auto_cl_max_explore_ratio)
    , _last_sampling_time_us(0)
    , _total_succ_req(0) {
}

AutoConcurrencyLimiter* AutoConcurrencyLimiter::New(const AdaptiveMaxConcurrency&) const {
    return new (std::nothrow) AutoConcurrencyLimiter;
}

bool AutoConcurrencyLimiter::OnRequested(int current_concurrency, Controller*) {
    return current_concurrency <= _max_concurrency;
}

void AutoConcurrencyLimiter::OnResponded(int error_code, int64_t latency_us) {
    if (0 == error_code) {
        _total_succ_req.fetch_add(1, butil::memory_order_relaxed);
    } else if (ELIMIT == error_code) {
        return;
    }

    const int64_t now_time_us = butil::gettimeofday_us();
    int64_t last_sampling_time_us = 
        _last_sampling_time_us.load(butil::memory_order_relaxed);

    if (last_sampling_time_us == 0 || 
        now_time_us - last_sampling_time_us >= 
            FLAGS_auto_cl_sampling_interval_ms * 1000) {
        bool sample_this_call = _last_sampling_time_us.compare_exchange_strong(
            last_sampling_time_us, now_time_us, butil::memory_order_relaxed);
        if (sample_this_call) {
            bool sample_window_submitted = AddSample(error_code, latency_us, 
                                                     now_time_us);
            if (sample_window_submitted) {
                // The following log prints has data-race in extreme cases, 
                // unless you are in debug, you should not open it.
                VLOG(1)
                    << "Sample window submitted, current max_concurrency:"
                    << _max_concurrency 
                    << ", min_latency_us:" << _min_latency_us
                    << ", ema_max_qps:" << _ema_max_qps
                    << ", explore_ratio:" << _explore_ratio;
            }
        }
    }
}

int AutoConcurrencyLimiter::MaxConcurrency() {
    return _max_concurrency;
}

int64_t AutoConcurrencyLimiter::NextResetTime(int64_t sampling_time_us) {
    int64_t reset_start_us = sampling_time_us + 
        (FLAGS_auto_cl_noload_latency_remeasure_interval_ms / 2 + 
        butil::fast_rand_less_than(FLAGS_auto_cl_noload_latency_remeasure_interval_ms / 2)) * 1000;
    return reset_start_us;
}

bool AutoConcurrencyLimiter::AddSample(int error_code, 
                                       int64_t latency_us, 
                                       int64_t sampling_time_us) {
    std::unique_lock<butil::Mutex> lock_guard(_sw_mutex);
    if (_reset_latency_us != 0) {
        // min_latency is about to be reset soon.
        if (_reset_latency_us > sampling_time_us) {
            // ignoring samples during waiting for the deadline.
            return false;
        }
        // Remeasure min_latency when concurrency has dropped to low load
        _min_latency_us = -1;
        _reset_latency_us = 0;
        _remeasure_start_us = NextResetTime(sampling_time_us);
        ResetSampleWindow(sampling_time_us);
    }

    if (_sw.start_time_us == 0) {
        _sw.start_time_us = sampling_time_us;
    }

    if (error_code != 0 && FLAGS_auto_cl_enable_error_punish) {
        ++_sw.failed_count;
        _sw.total_failed_us += latency_us;
    } else if (error_code == 0) {
        ++_sw.succ_count;
        _sw.total_succ_us += latency_us;
    }

    if (_sw.succ_count + _sw.failed_count < FLAGS_auto_cl_min_sample_count) {
        if (sampling_time_us - _sw.start_time_us >= 
            FLAGS_auto_cl_sample_window_size_ms * 1000) {
            // If the sample size is insufficient at the end of the sampling 
            // window, discard the entire sampling window
            ResetSampleWindow(sampling_time_us);
        }
        return false;
    } 
    if (sampling_time_us - _sw.start_time_us < 
        FLAGS_auto_cl_sample_window_size_ms * 1000 &&
        _sw.succ_count + _sw.failed_count < FLAGS_auto_cl_max_sample_count) {
        return false;
    }

    if(_sw.succ_count > 0) {
        UpdateMaxConcurrency(sampling_time_us);
    } else {
        // All request failed
        AdjustMaxConcurrency(_max_concurrency / 2);
    }
    ResetSampleWindow(sampling_time_us);
    return true;
}

void AutoConcurrencyLimiter::ResetSampleWindow(int64_t sampling_time_us) {
    _total_succ_req.exchange(0, butil::memory_order_relaxed);
    _sw.start_time_us = sampling_time_us;
    _sw.succ_count = 0;
    _sw.failed_count = 0;
    _sw.total_failed_us = 0;
    _sw.total_succ_us = 0;
}

void AutoConcurrencyLimiter::UpdateMinLatency(int64_t latency_us) {
    const double ema_factor = FLAGS_auto_cl_alpha_factor_for_ema;
    if (_min_latency_us <= 0) {
        _min_latency_us = latency_us;
    } else if (latency_us < _min_latency_us) {
        _min_latency_us = latency_us * ema_factor + _min_latency_us * (1 - ema_factor);
    }
}

void AutoConcurrencyLimiter::UpdateQps(double qps) {
    const double ema_factor = FLAGS_auto_cl_alpha_factor_for_ema / 10;
    if (qps >= _ema_max_qps) {
        _ema_max_qps = qps;
    } else {
        _ema_max_qps = qps * ema_factor + _ema_max_qps * (1 - ema_factor);
    }
}

void AutoConcurrencyLimiter::AdjustMaxConcurrency(int next_max_concurrency) {
    next_max_concurrency = std::max(bthread::FLAGS_bthread_concurrency, next_max_concurrency);
    if (next_max_concurrency != _max_concurrency) {
        _max_concurrency = next_max_concurrency;
    }
}

void AutoConcurrencyLimiter::UpdateMaxConcurrency(int64_t sampling_time_us) {
    int32_t total_succ_req = _total_succ_req.load(butil::memory_order_relaxed);
    double failed_punish = _sw.total_failed_us * FLAGS_auto_cl_fail_punish_ratio;
    int64_t avg_latency = 
        std::ceil((failed_punish + _sw.total_succ_us) / _sw.succ_count);
    double qps = 1000000.0 * total_succ_req / (sampling_time_us - _sw.start_time_us);
    UpdateMinLatency(avg_latency);
    UpdateQps(qps);

    int next_max_concurrency = 0;
    // Remeasure min_latency at regular intervals
    if (_remeasure_start_us <= sampling_time_us) {
        const double reduce_ratio = FLAGS_auto_cl_reduce_ratio_while_remeasure;
        _reset_latency_us = sampling_time_us + avg_latency * 2;
        next_max_concurrency = 
            std::ceil(_ema_max_qps * _min_latency_us / 1000000 * reduce_ratio);
    } else {
        const double change_step = FLAGS_auto_cl_change_rate_of_explore_ratio;
        const double max_explore_ratio = FLAGS_auto_cl_max_explore_ratio;
        const double min_explore_ratio = FLAGS_auto_cl_min_explore_ratio;
        const double correction_factor = FLAGS_auto_cl_latency_fluctuation_correction_factor;
        if (avg_latency <= _min_latency_us * (1.0 + min_explore_ratio * correction_factor) || 
            qps <= _ema_max_qps / (1.0 + min_explore_ratio)) {
            _explore_ratio  = std::min(max_explore_ratio, _explore_ratio + change_step); 
        } else {
            _explore_ratio = std::max(min_explore_ratio, _explore_ratio - change_step);
        }
        next_max_concurrency = 
            _min_latency_us * _ema_max_qps / 1000000 *  (1 + _explore_ratio);
    }

    AdjustMaxConcurrency(next_max_concurrency);
}

}  // namespace policy
}  // namespace brpc
