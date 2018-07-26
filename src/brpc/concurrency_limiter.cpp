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

#include "brpc/concurrency_limiter.h"

namespace brpc {

ConcurrencyLimiter* ConcurrencyLimiter::CreateConcurrencyLimiterOrDie(
    const AdaptiveMaxConcurrency& max_concurrency) {
    const ConcurrencyLimiter* cl = 
        ConcurrencyLimiterExtension()->Find(max_concurrency.name().c_str());
    CHECK(cl != NULL) 
        << "Fail to find ConcurrencyLimiter by `" 
        << max_concurrency.name() << "'";
    ConcurrencyLimiter* cl_copy = cl->New();
    CHECK(cl_copy != NULL) << "Fail to new ConcurrencyLimiter";
    if (max_concurrency == "constant") {
        cl_copy->SetMaxConcurrency(max_concurrency);
    }
    return cl_copy;
} 

}  // namespace brpc
