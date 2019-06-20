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

#include "brpc/policy/constant_concurrency_limiter.h"

namespace brpc {
namespace policy {

ConstantConcurrencyLimiter::ConstantConcurrencyLimiter(int max_concurrency)
    : _max_concurrency(max_concurrency) {
}

bool ConstantConcurrencyLimiter::OnRequested(int current_concurrency) {
    return current_concurrency <= _max_concurrency;
}

void ConstantConcurrencyLimiter::OnResponded(int error_code, int64_t latency) {
}

int ConstantConcurrencyLimiter::MaxConcurrency() {
    return _max_concurrency.load(butil::memory_order_relaxed);
}

ConstantConcurrencyLimiter*
ConstantConcurrencyLimiter::New(const AdaptiveMaxConcurrency& amc) const {
    CHECK_EQ(amc.type(), AdaptiveMaxConcurrency::CONSTANT());
    return new ConstantConcurrencyLimiter(static_cast<int>(amc));
}

}  // namespace policy
}  // namespace brpc
