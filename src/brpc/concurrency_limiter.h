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

#ifndef BRPC_CONCURRENCY_LIMITER_H
#define BRPC_CONCURRENCY_LIMITER_H
                                            
#include "brpc/describable.h"
#include "brpc/destroyable.h"
#include "brpc/extension.h"                       // Extension<T>

namespace brpc {

class ConcurrencyLimiter : public NonConstDescribable, public Destroyable {
public:
    ConcurrencyLimiter() {}

    // This method should be called each time a request comes in. It returns
    // false when the concurrency reaches the upper limit, otherwise it 
    // returns true. Normally, when OnRequested returns false, you should 
    // return an ELIMIT error directly.
    virtual bool OnRequested() = 0;

    // Each request should call this method before responding.
    // `error_code' : Error code obtained from the controller, 0 means success.
    // `latency' : Microseconds taken by RPC.
    // NOTE: Even if OnRequested returns false, after sending ELIMIT, you 
    // still need to call OnResponded.
    virtual void OnResponded(int error_code, int64_t latency_us) = 0;

    // Returns the current maximum concurrency. Note that the maximum 
    // concurrency of some ConcurrencyLimiters(eg: `auto', `gradient') 
    // is dynamically changing.
    virtual int MaxConcurrency() const = 0;

    // Returns the reference of maximum concurrency. mainly used to explicitly
    // specify the maximum concurrency. This method can only be called before
    // the server starts. 
    // NOTE: When using automatic concurrency limiter(eg: `auto', `gradient'),
    // the specified maximum concurrency will NOT take effect.
    virtual int& MaxConcurrencyRef() = 0;

    // Expose internal vars. NOT thread-safe.
    // Return 0 on success, -1 otherwise.
    virtual int Expose(const butil::StringPiece& prefix) = 0;

    // Create/destroy an instance.
    // Caller is responsible for Destroy() the instance after usage.
    virtual ConcurrencyLimiter* New() const = 0;

    virtual ~ConcurrencyLimiter() {}
};

inline Extension<const ConcurrencyLimiter>* ConcurrencyLimiterExtension() {
    return Extension<const ConcurrencyLimiter>::instance();
}

}  // namespace brpc


#endif // BRPC_CONCURRENCY_LIMITER_H
