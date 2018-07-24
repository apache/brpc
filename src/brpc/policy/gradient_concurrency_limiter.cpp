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
#include "brpc/policy/gradient_concurrency_limiter.h"

namespace brpc {
namespace policy {

DEFINE_int32(gradient_cl_sampling_interval_us, 100, 
    "Interval for sampling request in gradient concurrency limiter");
DEFINE_int32(gradient_cl_sample_window_size_ms, 1000,
    "Sample window size for update max concurrency in grandient "
    "concurrency limiter");
DEFINE_int32(gradient_cl_min_sample_count, 100,
    "Minimum sample count for update max concurrency");
DEFINE_double(gradient_cl_adjust_smooth, 0.9,
    "Smooth coefficient for adjust the max concurrency, the value is 0-1,"
    "the larger the value, the smaller the amount of each change");
DEFINE_int32(gradient_cl_initial_max_concurrency, 40,
    "Initial max concurrency for grandient concurrency limiter");
DEFINE_bool(gradient_cl_enable_error_punish, true,
    "Whether to consider failed requests when calculating maximum concurrency");
DEFINE_int32(gradient_cl_max_error_punish_ms, 3000,
    "The maximum time wasted for a single failed request");
DEFINE_double(gradient_cl_fail_punish_ratio, 1.0,
    "Use the failed requests to punish normal requests. The larger the "
    "configuration item, the more aggressive the penalty strategy.");
DEFINE_int32(gradient_cl_reserved_concurrency, 0,
    "The maximum concurrency reserved when the service is not overloaded."
    "When the traffic increases, the larger the configuration item, the "
    "faster the maximum concurrency grows until the server is fully loaded."
    "When the value is less than or equal to 0, square root of current "
    "concurrency is used.");
DEFINE_int32(gradient_cl_reset_count, 30, 
    "The service's latency will be re-measured every `reset_count' windows.");

static int32_t cast_max_concurrency(void* arg) {
    return *(int32_t*) arg;
}

GradientConcurrencyLimiter::GradientConcurrencyLimiter()
    : _unused_max_concurrency(0)
    , _reset_count(NextResetCount())
    , _min_latency_us(-1)
    , _smooth(FLAGS_gradient_cl_adjust_smooth)
    , _ema_qps(0)
    , _max_concurrency_bvar(cast_max_concurrency, &_max_concurrency)
    , _last_sampling_time_us(0)
    , _max_concurrency(FLAGS_gradient_cl_initial_max_concurrency)
    , _total_succ_req(0)
    , _current_concurrency(0) {
}

int GradientConcurrencyLimiter::MaxConcurrency() const {
    return _max_concurrency.load(butil::memory_order_relaxed);
}

int& GradientConcurrencyLimiter::MaxConcurrencyRef() {
    return _unused_max_concurrency;
}

int GradientConcurrencyLimiter::Expose(const butil::StringPiece& prefix) {
    if (_max_concurrency_bvar.expose_as(prefix, "gradient_cl_max_concurrency") != 0) {
        return -1;
    }
    return 0;
}

GradientConcurrencyLimiter* GradientConcurrencyLimiter::New() const {
    return new (std::nothrow) GradientConcurrencyLimiter;
}

void GradientConcurrencyLimiter::Destroy() {
    delete this;
}

bool GradientConcurrencyLimiter::OnRequested() {
    const int32_t current_concurrency = 
        _current_concurrency.fetch_add(1, butil::memory_order_relaxed);
    if (current_concurrency >= _max_concurrency.load(butil::memory_order_relaxed)) {
        return false;
    }
    return true;
}

void GradientConcurrencyLimiter::OnResponded(int error_code, 
                                             int64_t latency_us) {
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
            FLAGS_gradient_cl_sampling_interval_us) {
        bool sample_this_call = _last_sampling_time_us.compare_exchange_weak(
                last_sampling_time_us, now_time_us, 
                butil::memory_order_relaxed);
        if (sample_this_call) {
            AddSample(error_code, latency_us, now_time_us);
        }
    }
}

int GradientConcurrencyLimiter::NextResetCount() {
    int max_reset_count = FLAGS_gradient_cl_reset_count;
    return rand() % (max_reset_count / 2) + max_reset_count / 2;
}

void GradientConcurrencyLimiter::AddSample(int error_code, int64_t latency_us, 
                                           int64_t sampling_time_us) {
    BAIDU_SCOPED_LOCK(_sw_mutex);
    if (_sw.start_time_us == 0) {
        _sw.start_time_us = sampling_time_us;
    }

    if (error_code != 0 && 
        FLAGS_gradient_cl_enable_error_punish) {
        ++_sw.failed_count;
        latency_us = 
            std::min(int64_t(FLAGS_gradient_cl_max_error_punish_ms) * 1000,
                     latency_us);
        _sw.total_failed_us += latency_us;
    } else if (error_code == 0) {
        ++_sw.succ_count;
        _sw.total_succ_us += latency_us;
    }

    if (sampling_time_us - _sw.start_time_us < 
            FLAGS_gradient_cl_sample_window_size_ms * 1000) {
        return;
    } else if (_sw.succ_count + _sw.failed_count < 
        FLAGS_gradient_cl_min_sample_count) {
        LOG_EVERY_N(INFO, 100) << "Insufficient sample size";
    } else if (_sw.succ_count > 0) {
        UpdateConcurrency(sampling_time_us);
        ResetSampleWindow(sampling_time_us);
    } else {
        LOG(ERROR) << "All request failed, resize max_concurrency";
        int32_t current_concurrency = 
            _current_concurrency.load(butil::memory_order_relaxed);
        _current_concurrency.store(
            current_concurrency / 2, butil::memory_order_relaxed);
    }
}

void GradientConcurrencyLimiter::ResetSampleWindow(int64_t sampling_time_us) {
    _sw.start_time_us = sampling_time_us;
    _sw.succ_count = 0;
    _sw.failed_count = 0;
    _sw.total_failed_us = 0;
    _sw.total_succ_us = 0;
}

void GradientConcurrencyLimiter::UpdateMinLatency(int64_t latency_us) {
    if (_min_latency_us <= 0) {
        _min_latency_us = latency_us;
    } else if (latency_us < _min_latency_us) {
        _min_latency_us = _min_latency_us * _smooth + latency_us * (1 - _smooth);
    }
}

void GradientConcurrencyLimiter::UpdateQps(int32_t succ_count, 
                                           int64_t sampling_time_us) {
    double qps = double(succ_count) / (sampling_time_us - _sw.start_time_us)
                  * 1000 * 1000;
    _ema_qps = _ema_qps * _smooth + qps * (1 - _smooth);
}

void GradientConcurrencyLimiter::UpdateConcurrency(int64_t sampling_time_us) {
    int32_t current_concurrency = _current_concurrency.load();
    int max_concurrency = _max_concurrency.load();
    int32_t total_succ_req = 
        _total_succ_req.exchange(0, butil::memory_order_relaxed);
    int64_t failed_punish = 
        _sw.total_failed_us * FLAGS_gradient_cl_fail_punish_ratio;
    int64_t avg_latency = 
        std::ceil((failed_punish + _sw.total_succ_us) / _sw.succ_count);
    UpdateMinLatency(avg_latency);
    UpdateQps(total_succ_req, sampling_time_us);

    int reserved_concurrency = FLAGS_gradient_cl_reserved_concurrency;
    if (reserved_concurrency <= 0) {
        reserved_concurrency = std::ceil(std::sqrt(max_concurrency));
    } 

    int32_t next_concurrency = 
        std::ceil(_ema_qps * _min_latency_us / 1000.0 / 1000);
    int32_t saved_min_latency_us = _min_latency_us;
    if (--_reset_count == 0) {
        _reset_count = NextResetCount();
        if (current_concurrency >= max_concurrency - 2) {
            _min_latency_us = -1;
            next_concurrency -= std::sqrt(max_concurrency);
            next_concurrency = std::max(next_concurrency, reserved_concurrency);
        } else {
            // current_concurrency < max_concurrency means the server is 
            // not overloaded and does not need to detect noload_latency by 
            // lowering the maximum concurrency
            next_concurrency += reserved_concurrency;
        }
    } else {
        next_concurrency += reserved_concurrency;
    }

    LOG(INFO)
        << "Update max_concurrency by gradient limiter:"
        << " pre_max_concurrency:" << max_concurrency 
        << ", min_avg_latency:" << saved_min_latency_us << "us"
        << ", reserved_concurrency:" << reserved_concurrency
        << ", sampling_avg_latency:" << avg_latency << "us"
        << ", failed_punish:" << failed_punish << "us"
        << ", ema_qps:" << _ema_qps
        << ", succ sample count" << _sw.succ_count
        << ", failed sample count" << _sw.failed_count
        << ", current_concurrency:" << current_concurrency
        << ", next_max_concurrency:" << next_concurrency;
    if (next_concurrency != max_concurrency) {
        _max_concurrency.store(next_concurrency, butil::memory_order_relaxed);
    }
}

}  // namespace policy
}  // namespace brpc

