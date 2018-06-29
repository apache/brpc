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

#ifndef BRPC_POLICY_CONSTANT_CONCURRENCY_LIMITER_H
#define BRPC_POLICY_CONSTANT_CONCURRENCY_LIMITER_H

#include "brpc/concurrency_limiter.h"

namespace brpc {
namespace policy {

class ConstantConcurrencyLimiter : public ConcurrencyLimiter {
public:
    ConstantConcurrencyLimiter() 
        : _max_concurrency(0), 
        _current_concurrency(0) {}

    ~ConstantConcurrencyLimiter() {}

    bool OnRequested() override;
    void OnResponded(int error_code, int64_t latency_us) override;
    virtual int MaxConcurrency() const override;
    virtual int& MaxConcurrency() override;

    int Expose(const butil::StringPiece& prefix) override;
    ConstantConcurrencyLimiter* New() const override;
    void Destroy() override;
    void Describe(std::ostream&, const DescribeOptions& options) override;

private:
    int32_t _max_concurrency;
    butil::atomic<int32_t> _current_concurrency;
};

}  // namespace policy
}  // namespace brpc


#endif // BRPC_POLICY_CONSTANT_CONCURRENCY_LIMITER_H
