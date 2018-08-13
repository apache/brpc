// Copyright (c) 2014 Baidu, Inc.G
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Authors: Lei He (helei@qiyi.com)

#include <cmath>
#include <gflags/gflags.h>
#include "brpc/errno.pb.h"
#include "brpc/policy/auto_concurrency_limiter.h"

namespace brpc {
namespace policy {

DEFINE_int32(auto_cl_peak_qps_window_size, 50, 
             "The number of samples windows used for peak-qps calculations.");
DEFINE_double(auto_cl_sampling_interval_ms, 0.1, 
             "Interval for sampling request in auto concurrency limiter");
DEFINE_int32(auto_cl_sample_window_size_ms, 1000,
             "Sample window size for update max concurrency in grandient "
             "concurrency limiter");
DEFINE_int32(auto_cl_min_sample_count, 100,
             "Minimum sample count for update max concurrency");
DEFINE_int32(auto_cl_max_sample_count, 500,
             "Maximum sample count for update max concurrency");
DEFINE_int32(auto_cl_initial_max_concurrency, 40,
             "Initial max concurrency for grandient concurrency limiter");
DEFINE_int32(auto_cl_reset_interval_ms, 50000, 
             "Interval for remeasurement of noload_latency. The period of "
             "remeasurement of noload_latency will halve max_concurrency.");
DEFINE_int32(auto_cl_reset_duration_ms, 2000, 
             "The duration of the remeasurement of noload_latency.");
DEFINE_int32(auto_cl_min_concurrency, 40, "Minimum value of max_concurrency");

DEFINE_double(auto_cl_adjust_smooth, 0.9,
              "Smooth coefficient for adjust the max concurrency, the value "
              "is 0-1, the larger the value, the smaller the amount of each "
              "change");
DEFINE_double(auto_cl_fail_punish_ratio, 1.0,
              "Use the failed requests to punish normal requests. The larger "
              "the configuration item, the more aggressive the penalty strategy.");
DEFINE_double(auto_cl_overload_threshold, 0.40, 
              "Expected ratio of latency fluctuations");

DEFINE_bool(auto_cl_enable_error_punish, true,
            "Whether to consider failed requests when calculating maximum concurrency");

static int32_t cast_max_concurrency(void* arg) {
    return *(int32_t*) arg;
}

AutoConcurrencyLimiter::AutoConcurrencyLimiter()
    : _reset_start_us(NextResetTime())
    , _reset_end_us(0)
    , _min_latency_us(-1)
    , _ema_peak_qps(-1)
    , _qps_bq(FLAGS_auto_cl_peak_qps_window_size)
    , _smooth(FLAGS_auto_cl_adjust_smooth)
    , _overload_threshold(FLAGS_auto_cl_overload_threshold)
    , _max_concurrency_bvar(cast_max_concurrency, &_max_concurrency)
    , _last_sampling_time_us(0)
    , _total_succ_req(0)
    , _current_concurrency(0) {
    _max_concurrency = FLAGS_auto_cl_initial_max_concurrency;
}

int AutoConcurrencyLimiter::Expose(const butil::StringPiece& prefix) {
    if (_max_concurrency_bvar.expose_as(prefix, "auto_cl_max_concurrency") != 0) {
        return -1;
    }
    return 0;
}

AutoConcurrencyLimiter* AutoConcurrencyLimiter::New() const {
    return new (std::nothrow) AutoConcurrencyLimiter;
}

void AutoConcurrencyLimiter::Destroy() {
    delete this;
}

bool AutoConcurrencyLimiter::OnRequested() {
    const int32_t current_concurrency = 
        _current_concurrency.fetch_add(1, butil::memory_order_relaxed);
    if (current_concurrency >= _max_concurrency) {
        return false;
    }
    return true;
}

void AutoConcurrencyLimiter::OnResponded(int error_code, int64_t latency_us) {
    _current_concurrency.fetch_sub(1, butil::memory_order_relaxed);
    if (0 == error_code) {
        _total_succ_req.fetch_add(1, butil::memory_order_relaxed);
    } else if (ELIMIT == error_code) {
        return;
    }

    int64_t now_time_us = butil::gettimeofday_us();
    int64_t last_sampling_time_us = 
        _last_sampling_time_us.load(butil::memory_order_relaxed);

    if (last_sampling_time_us == 0 || 
        now_time_us - last_sampling_time_us >= 
            FLAGS_auto_cl_sampling_interval_ms * 1000) {
        bool sample_this_call = _last_sampling_time_us.compare_exchange_weak(
                last_sampling_time_us, now_time_us, 
                butil::memory_order_relaxed);
        if (sample_this_call) {
            int32_t max_concurrency = AddSample(error_code, latency_us, now_time_us);
            if (max_concurrency != 0) {
                LOG(INFO) 
                    << "MaxConcurrency updated by auto limiter,"
                    << "current_max_concurrency:" << max_concurrency;
            }
        }
    }
}

int64_t AutoConcurrencyLimiter::NextResetTime() {
    int64_t reset_start_us = butil::gettimeofday_us() + 
        (FLAGS_auto_cl_reset_interval_ms / 2 + 
         butil::fast_rand_less_than(FLAGS_auto_cl_reset_interval_ms / 2)) * 1000;
    return reset_start_us;
}

int32_t AutoConcurrencyLimiter::AddSample(int error_code, 
                                              int64_t latency_us, 
                                              int64_t sampling_time_us) {
    std::unique_lock<butil::Mutex> lock_guard(_sw_mutex);
    if (_sw.start_time_us == 0) {
        _sw.start_time_us = sampling_time_us;
    }

    if (error_code != 0 && 
        FLAGS_auto_cl_enable_error_punish) {
        ++_sw.failed_count;
        _sw.total_failed_us += latency_us;
    } else if (error_code == 0) {
        ++_sw.succ_count;
        _sw.total_succ_us += latency_us;
    }

    if (_sw.succ_count + _sw.failed_count < FLAGS_auto_cl_min_sample_count) {
        return 0;
    } 
    if (sampling_time_us - _sw.start_time_us < FLAGS_auto_cl_sample_window_size_ms &&
        _sw.succ_count + _sw.failed_count < FLAGS_auto_cl_max_sample_count) {
        return 0;
    }

    if(_sw.succ_count > 0) {
        int max_concurrency = UpdateMaxConcurrency(sampling_time_us);
        ResetSampleWindow(sampling_time_us);
        return max_concurrency;
    } else {
        // All request failed
        int32_t current_concurrency = 
            _current_concurrency.load(butil::memory_order_relaxed);
        _current_concurrency.store(
            current_concurrency / 2, butil::memory_order_relaxed);
        return 0;
    }
}

void AutoConcurrencyLimiter::ResetSampleWindow(int64_t sampling_time_us) {
    _sw.start_time_us = sampling_time_us;
    _sw.succ_count = 0;
    _sw.failed_count = 0;
    _sw.total_failed_us = 0;
    _sw.total_succ_us = 0;
}

void AutoConcurrencyLimiter::UpdateMinLatency(int64_t latency_us) {
    if (_min_latency_us <= 0) {
        _min_latency_us = latency_us;
    } else if (latency_us < _min_latency_us) {
        _min_latency_us = _min_latency_us * _smooth + latency_us * (1 - _smooth);
    }
}

void AutoConcurrencyLimiter::UpdateQps(int32_t succ_count, 
                                       int64_t sampling_time_us) {
    double qps = 1000000.0 * succ_count / (sampling_time_us - _sw.start_time_us);
    _qps_bq.elim_push(qps);
    double peak_qps = *(_qps_bq.bottom());

    for (size_t i = 0; i < _qps_bq.size(); ++i) {
        peak_qps = std::max(*(_qps_bq.bottom(i)), peak_qps);
    }

    if (peak_qps >= _ema_peak_qps) {
        _ema_peak_qps = peak_qps;
    } else {
        _ema_peak_qps = _ema_peak_qps * _smooth + peak_qps * (1 - _smooth);
    }
}

int32_t AutoConcurrencyLimiter::UpdateMaxConcurrency(int64_t sampling_time_us) {
    int32_t total_succ_req = 
        _total_succ_req.exchange(0, butil::memory_order_relaxed);
    double failed_punish = 
        _sw.total_failed_us * FLAGS_auto_cl_fail_punish_ratio;
    int64_t avg_latency = 
        std::ceil((failed_punish + _sw.total_succ_us) / _sw.succ_count);
    UpdateMinLatency(avg_latency);
    UpdateQps(total_succ_req, sampling_time_us);

    if (_reset_end_us > sampling_time_us) {
        return 0;
    }

    int next_max_concurrency = 0;
    if (_reset_start_us <= sampling_time_us) {
        _min_latency_us = -1;
        _reset_start_us = NextResetTime();
        _reset_end_us = sampling_time_us + FLAGS_auto_cl_reset_duration_ms * 1000;
        next_max_concurrency = _max_concurrency / 2;
    } else {
        int32_t noload_concurrency = 
            std::ceil(_min_latency_us * _ema_peak_qps / 1000000);
        if (avg_latency < (1.0 + _overload_threshold) * _min_latency_us) {
            next_max_concurrency = std::ceil(noload_concurrency * 
                (2.0 + _overload_threshold - double(avg_latency) / _min_latency_us));
        } else {
            next_max_concurrency = noload_concurrency;
        }
    }
    next_max_concurrency = 
        std::max(next_max_concurrency, FLAGS_auto_cl_min_concurrency);

    if (next_max_concurrency != _max_concurrency) {
        _max_concurrency = next_max_concurrency;
    }
    return next_max_concurrency;
}

}  // namespace policy
}  // namespace brpc
