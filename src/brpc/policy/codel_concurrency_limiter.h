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

#ifndef BRPC_POLICY_CODEL_CONCURRENCY_LIMITER_H
#define BRPC_POLICY_CODEL_CONCURRENCY_LIMITER_H

#include "bvar/bvar.h"
#include "butil/containers/bounded_queue.h"
#include "brpc/concurrency_limiter.h"

namespace brpc {
namespace policy {

class CodelConcurrencyLimiter : public ConcurrencyLimiter {
public:
    CodelConcurrencyLimiter();

    bool OnRequested(int current_concurrency, int64_t waiting_time_us) override;
    
    void OnResponded(int error_code, int64_t latency_us) override;

    CodelConcurrencyLimiter* New(const AdaptiveMaxConcurrency&) const override;

    int MaxConcurrency() override;

    int current_load_in_precent() const {
        return std::min<int>(100, 
            100 * _min_delay_us.load(butil::memory_order_relaxed) / discard_timeout_us());
    }

private:
    // Analyze current load. Return true if overloaded, otherwise return false
    bool AnalyzeLoad(uint64_t waiting_us);
    // When overloaded, the request request whose queuing delay exceeds this timeout 
    // will be discarded.
    uint64_t discard_timeout_us() const;

    // write per request
    std::atomic<int32_t> _realtime_tokens;
    std::atomic<uint64_t> _min_delay_us;
    std::atomic<uint64_t> _reset_min_delay_after_this_time_us;
    std::atomic<uint64_t> _reset_tokens_after_this_time_us;

    // write per interval
    std::atomic<bool> _reset_min_delay;
    std::atomic<bool> _overloaded;
};

}  // namespace policy
}  // namespace brpc


#endif // BRPC_POLICY_CODEL_CONCURRENCY_LIMITER_H
