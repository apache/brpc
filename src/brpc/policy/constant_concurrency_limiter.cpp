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

#include "brpc/policy/constant_concurrency_limiter.h"

namespace brpc {
namespace policy {

bool ConstantConcurrencyLimiter::OnRequested() {
    const int32_t current_concurreny = 
        _current_concurrency.fetch_add(1, butil::memory_order_relaxed);
    if (_max_concurrency != 0 && current_concurreny >= _max_concurrency) {
        return false;
    }
    return true;
}

void ConstantConcurrencyLimiter::OnResponded(int error_code, int64_t latency) {
    _current_concurrency.fetch_sub(1, butil::memory_order_relaxed);
}

int ConstantConcurrencyLimiter::MaxConcurrency() const {
    return _max_concurrency;
}

int& ConstantConcurrencyLimiter::MaxConcurrencyRef() {
    return _max_concurrency;
}

int ConstantConcurrencyLimiter::Expose(const butil::StringPiece& prefix) {
    return 0;
}

ConstantConcurrencyLimiter* ConstantConcurrencyLimiter::New() const {
    return new (std::nothrow) ConstantConcurrencyLimiter;
}

void ConstantConcurrencyLimiter::Destroy() {
    delete this;
}

}  // namespace policy
}  // namespace brpc
