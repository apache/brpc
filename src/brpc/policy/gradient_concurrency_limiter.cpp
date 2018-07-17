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

namespace bthread {
DECLARE_int32(bthread_concurrency);
}

DECLARE_int32(task_group_runqueue_capacity);

namespace brpc {
namespace policy {

DEFINE_int32(gradient_cl_sampling_interval_us, 100, 
    "Interval for sampling request in gradient concurrency limiter");
DEFINE_int32(gradient_cl_sample_window_size_ms, 1000,
    "Sample window size for update max concurrency in grandient "
    "concurrency limiter");
DEFINE_int32(gradient_cl_min_sample_count, 100,
    "Minimum sample count for update max concurrency");
DEFINE_int32(gradient_cl_adjust_smooth, 50,
    "Smooth coefficient for adjust the max concurrency, the value is 0-99,"
    "the larger the value, the smaller the amount of each change");
DEFINE_int32(gradient_cl_initial_max_concurrency, 400,
    "Initial max concurrency for grandient concurrency limiter");
DEFINE_bool(gradient_cl_enable_error_punish, true,
    "Whether to consider failed requests when calculating maximum concurrency");
DEFINE_int32(gradient_cl_max_error_punish_ms, 3000,
    "The maximum time wasted for a single failed request");
DEFINE_double(gradient_cl_fail_punish_ratio, 1.0,
    "Use the failed requests to punish normal requests. The larger the "
    "configuration item, the more aggressive the penalty strategy.");
DEFINE_int32(gradient_cl_window_count, 30,
    "Sample windows count for compute history min average latency");
DEFINE_int32(gradient_cl_reserved_concurrency, 0,
    "The maximum concurrency reserved when the service is not overloaded."
    "When the traffic increases, the larger the configuration item, the "
    "faster the maximum concurrency grows until the server is fully loaded."
    "When the value is less than or equal to 0, square root of current "
    "concurrency is used.");
DEFINE_double(gradient_cl_min_reduce_ratio, 0.5,
    "The minimum reduce ratio of maximum concurrency per calculation."
    " The value should be 0-1");

static int32_t cast_max_concurrency(void* arg) {
    return *(int32_t*) arg;
}

GradientConcurrencyLimiter::GradientConcurrencyLimiter()
    : _ws_queue(FLAGS_gradient_cl_window_count)
    , _ws_index(0)
    , _unused_max_concurrency(0)
    , _max_concurrency_bvar(cast_max_concurrency, &_max_concurrency)
    , _last_sampling_time_us(0)
    , _total_succ_req(0)
    , _max_concurrency(FLAGS_gradient_cl_initial_max_concurrency)
    , _current_concurrency(0) {
}

void GradientConcurrencyLimiter::Describe(
    std::ostream& os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "gradient_cl";
        return;
    }
    os << "Gradient{";
    os << "current_max_concurrency:" 
       << _max_concurrency.load(butil::memory_order_relaxed);
    os << '}';
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
    const int32_t current_concurreny = 
        _current_concurrency.fetch_add(1, butil::memory_order_relaxed);
    if (current_concurreny >= _max_concurrency.load(butil::memory_order_relaxed)) {
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
        UpdateConcurrency();
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

void GradientConcurrencyLimiter::UpdateConcurrency() {
    int32_t current_concurrency = _current_concurrency.load();
    int max_concurrency = _max_concurrency.load();
    int32_t total_succ_req = _total_succ_req.exchange(0, butil::memory_order_relaxed);

    int64_t failed_punish = _sw.total_failed_us * 
        FLAGS_gradient_cl_fail_punish_ratio;
    int64_t avg_latency = 
        (failed_punish + _sw.total_succ_us) / _sw.succ_count;
    avg_latency = std::max(static_cast<int64_t>(1), avg_latency);

    WindowSnap snap(avg_latency, current_concurrency, total_succ_req);
    _ws_queue.elim_push(snap);
    ++_ws_index;
    int64_t min_avg_latency_us = _ws_queue.bottom()->avg_latency_us; 
    int32_t safe_concurrency = _ws_queue.bottom()->actual_concurrency;
    for (size_t i = 0; i < _ws_queue.size(); ++i) {
        const WindowSnap& snap = *(_ws_queue.bottom(i));
        if (min_avg_latency_us > snap.avg_latency_us) {
            min_avg_latency_us = snap.avg_latency_us;
            safe_concurrency = snap.actual_concurrency;
        } else if (min_avg_latency_us == snap.avg_latency_us) {
            safe_concurrency = std::max(safe_concurrency, 
                                        snap.actual_concurrency);
        }
    }
                                
    int smooth = FLAGS_gradient_cl_adjust_smooth;
    if (smooth <= 0 || smooth > 99) {
        LOG_EVERY_N(WARNING, 100) 
            << "GFLAG `gradient_cl_adjust_smooth' should be 0-99,"
            << "current: " << FLAGS_gradient_cl_adjust_smooth
            << ", will compute with the defalut smooth value(50)";
        smooth = 50;
    }

    int reserved_concurrency = FLAGS_gradient_cl_reserved_concurrency;
    if (reserved_concurrency <= 0) {
        reserved_concurrency = std::ceil(std::sqrt(max_concurrency));
    } 
    double fix_gradient = std::min(
            1.0, double(min_avg_latency_us) / avg_latency);
    int32_t next_concurrency = std::ceil(
        max_concurrency * fix_gradient + reserved_concurrency);
    next_concurrency = std::ceil(
        (max_concurrency * smooth + next_concurrency * (100 - smooth)) / 100);

    double min_reduce_ratio = FLAGS_gradient_cl_min_reduce_ratio;
    if (min_reduce_ratio <= 0.0 || min_reduce_ratio >= 1.0) {
        LOG(INFO)
            << "GFLAG `gradient_cl_min_reduce_ratio' should "
            << "be 0-1, current:" << FLAGS_gradient_cl_min_reduce_ratio
            << " , will compute with the default value(0.5)";
        min_reduce_ratio = 0.5;
    }
    next_concurrency = std::max(
            next_concurrency, int32_t(max_concurrency * min_reduce_ratio));
    next_concurrency = std::max(
            next_concurrency, int32_t(safe_concurrency * min_reduce_ratio));

    if (current_concurrency + reserved_concurrency < max_concurrency &&
        max_concurrency < next_concurrency) {
        LOG(INFO)
            << "No need to expand the maximum concurrency"
            << ", min_avg_latency:" << min_avg_latency_us << "us"
            << ", sampling_avg_latency:" << avg_latency << "us"
            << ", current_concurrency:" << current_concurrency
            << ", current_max_concurrency:" << max_concurrency
            << ", next_max_concurrency:" << next_concurrency;
        return;
    } 
    if (fix_gradient < 1.0 && max_concurrency < next_concurrency) {
        for (size_t i = 0; i < _ws_queue.size(); ++i) {
            const WindowSnap& snap = *(_ws_queue.bottom(i));
            if (current_concurrency > snap.actual_concurrency &&
                total_succ_req < snap.total_succ_req &&
                avg_latency > snap.avg_latency_us) {
                int32_t fixed_next_concurrency = 
                    std::ceil(snap.actual_concurrency * 
                    snap.avg_latency_us / avg_latency);
                next_concurrency = 
                    std::min(next_concurrency, fixed_next_concurrency);
            }
        }
    }

    LOG(INFO)
        << "Update max_concurrency by gradient limiter:"
        << " pre_max_concurrency:" << max_concurrency 
        << ", min_avg_latency:" << min_avg_latency_us << "us"
        << ", sampling_avg_latency:" << avg_latency << "us"
        << ", failed_punish:" << failed_punish << "us"
        << ", fix_gradient=" << fix_gradient
        << ", succ sample count" << _sw.succ_count
        << ", failed sample count" << _sw.failed_count
        << ", current_concurrency:" << current_concurrency
        << ", next_max_concurrency:" << next_concurrency;
    _max_concurrency.store(next_concurrency, butil::memory_order_relaxed);
}

}  // namespace policy
}  // namespace brpc

