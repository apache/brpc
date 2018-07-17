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

#ifndef BRPC_POLICY_GRANDIENT_CONCURRENCY_LIMITER_H
#define BRPC_POLICY_GRANDIENT_CONCURRENCY_LIMITER_H

#include "bvar/bvar.h"
#include "butil/containers/bounded_queue.h"
#include "brpc/concurrency_limiter.h"

namespace brpc {
namespace policy {

class GradientConcurrencyLimiter : public ConcurrencyLimiter {
public:
    GradientConcurrencyLimiter();
    ~GradientConcurrencyLimiter() {}
    bool OnRequested() override;
    void OnResponded(int error_code, int64_t latency_us) override;
    int MaxConcurrency() const override;
    int& MaxConcurrencyRef() override;

    int Expose(const butil::StringPiece& prefix) override;
    GradientConcurrencyLimiter* New() const override;
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

    struct WindowSnap {
        WindowSnap(int64_t latency_us, int32_t concurrency, int32_t succ_req)
            : avg_latency_us(latency_us)
            , actual_concurrency(concurrency)
            , total_succ_req(succ_req) {}
        int64_t avg_latency_us;
        int32_t actual_concurrency;
        int32_t total_succ_req;
    };

    void AddSample(int error_code, int64_t latency_us, int64_t sampling_time_us);

    //NOT thread-safe, should be called in AddSample()
    void UpdateConcurrency();
    void ResetSampleWindow(int64_t sampling_time_us);
    
    SampleWindow _sw;
    butil::BoundedQueue<WindowSnap> _ws_queue;
    uint32_t _ws_index;
    int32_t _unused_max_concurrency;
    butil::Mutex _sw_mutex;
    bvar::PassiveStatus<int32_t> _max_concurrency_bvar;
    butil::atomic<int64_t> BAIDU_CACHELINE_ALIGNMENT _last_sampling_time_us;
    butil::atomic<int32_t> _max_concurrency;
    butil::atomic<int32_t> BAIDU_CACHELINE_ALIGNMENT _total_succ_req;
    butil::atomic<int32_t> _current_concurrency;
};

}  // namespace policy
}  // namespace brpc


#endif // BRPC_POLICY_GRANDIENT_CONCURRENCY_LIMITER_H
