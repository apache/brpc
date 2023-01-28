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

    bool OnRequested(int current_concurrency, Controller*) override;
    
    void OnResponded(int error_code, int64_t latency_us) override;

    int MaxConcurrency() override;

    AutoConcurrencyLimiter* New(const AdaptiveMaxConcurrency&) const override;

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

    bool AddSample(int error_code, int64_t latency_us, int64_t sampling_time_us);
    int64_t NextResetTime(int64_t sampling_time_us);

    // The following methods are not thread safe and can only be called 
    // in AppSample()
    void UpdateMaxConcurrency(int64_t sampling_time_us);
    void ResetSampleWindow(int64_t sampling_time_us);
    void UpdateMinLatency(int64_t latency_us);
    void UpdateQps(double qps);

    void AdjustMaxConcurrency(int next_max_concurrency);

    // modified per sample-window or more
    int _max_concurrency;
    int64_t _remeasure_start_us;
    int64_t _reset_latency_us;
    int64_t _min_latency_us; 
    double _ema_max_qps;
    double _explore_ratio;
  
    // modified per sample.
    BAIDU_CACHELINE_ALIGNMENT butil::atomic<int64_t> _last_sampling_time_us;
    butil::Mutex _sw_mutex;
    SampleWindow _sw;

    // modified per request.
    BAIDU_CACHELINE_ALIGNMENT butil::atomic<int32_t> _total_succ_req;
};

}  // namespace policy
}  // namespace brpc


#endif // BRPC_POLICY_AUTO_CONCURRENCY_LIMITER_H
