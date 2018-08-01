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

#ifndef BRPC_POLICY_AUTO_CONCURRENCY_LIMITER_H
#define BRPC_POLICY_AUTO_CONCURRENCY_LIMITER_H

#include "bvar/bvar.h"
#include "butil/containers/bounded_queue.h"
#include "brpc/concurrency_limiter.h"

namespace brpc {
namespace policy {

class AutoConcurrencyLimiter : public ConcurrencyLimiter {
public:
    AutoConcurrencyLimiter();
    ~AutoConcurrencyLimiter() {}
    bool OnRequested() override;
    void OnResponded(int error_code, int64_t latency_us) override;

    int Expose(const butil::StringPiece& prefix) override;
    AutoConcurrencyLimiter* New() const override;
    void Destroy() override;

private:
    struct SampleWindow {
        SampleWindow() 
            : start_time_us(0)
            , succ_count(0)
            , failed_count(0)
            , total_failed_us(0)
            , total_succ_us(0) {}
        int64_t start_time_us;
        int32_t succ_count;
        int32_t failed_count;
        int64_t total_failed_us;
        int64_t total_succ_us;
    };

    int32_t AddSample(int error_code, int64_t latency_us, int64_t sampling_time_us);
    int NextResetCount();

    // The following methods are not thread safe and can only be called 
    // in AppSample()
    int32_t UpdateMaxConcurrency(int64_t sampling_time_us);
    void ResetSampleWindow(int64_t sampling_time_us);
    void UpdateMinLatency(int64_t latency_us);
    void UpdateQps(int32_t succ_count, int64_t sampling_time_us);
    double peak_qps();
    
    SampleWindow _sw;
    int _reset_count;
    int64_t _min_latency_us;
    const double _smooth;
    double _ema_peak_qps;
    butil::BoundedQueue<double> _qps_bq;
    butil::Mutex _sw_mutex;
    bvar::PassiveStatus<int32_t> _max_concurrency_bvar;
    butil::atomic<int64_t> BAIDU_CACHELINE_ALIGNMENT _last_sampling_time_us;
    butil::atomic<int32_t> _total_succ_req;
    butil::atomic<int32_t> _current_concurrency;
};

}  // namespace policy
}  // namespace brpc


#endif // BRPC_POLICY_AUTO_CONCURRENCY_LIMITER_H
