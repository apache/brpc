// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef  BRPC_METHOD_STATUS_H
#define  BRPC_METHOD_STATUS_H

#include "butil/macros.h"                  // DISALLOW_COPY_AND_ASSIGN
#include "bvar/bvar.h"                    // vars
#include "brpc/describable.h"
#include "brpc/concurrency_limiter.h"


namespace brpc {

class Controller;
class Server;
// Record accessing stats of a method.
class MethodStatus : public Describable {
public:
    MethodStatus();
    ~MethodStatus();

    // Call this function when the method is about to be called.
    // Returns false when the method is overloaded. If rejected_cc is not
    // NULL, it's set with the rejected concurrency.
    bool OnRequested(int* rejected_cc = NULL);

    // Call this when the method just finished.
    // `error_code' : The error code obtained from the controller. Equal to 
    // 0 when the call is successful.
    // `latency_us' : microseconds taken by a successful call. Latency can
    // be measured in this utility class as well, but the callsite often
    // did the time keeping and the cost is better saved. 
    void OnResponded(int error_code, int64_t latency_us);

    // Expose internal vars.
    // Return 0 on success, -1 otherwise.
    int Expose(const butil::StringPiece& prefix);

    // Describe internal vars, used by /status
    void Describe(std::ostream &os, const DescribeOptions&) const;

    // Current max_concurrency of the method.
    int MaxConcurrency() const { return _cl ? _cl->MaxConcurrency() : 0; }

private:
friend class Server;
    DISALLOW_COPY_AND_ASSIGN(MethodStatus);

    // Note: SetConcurrencyLimiter() is not thread safe and can only be called 
    // before the server is started. 
    void SetConcurrencyLimiter(ConcurrencyLimiter* cl);

    std::unique_ptr<ConcurrencyLimiter> _cl;
    butil::atomic<int> _nconcurrency;
    bvar::Adder<int64_t>  _nerror_bvar;
    bvar::LatencyRecorder _latency_rec;
    bvar::PassiveStatus<int>  _nconcurrency_bvar;
    bvar::PerSecond<bvar::Adder<int64_t>> _eps_bvar;
    bvar::PassiveStatus<int32_t> _max_concurrency_bvar;
};

class ConcurrencyRemover {
public:
    ConcurrencyRemover(MethodStatus* status, Controller* c, int64_t received_us)
        : _status(status) 
        , _c(c)
        , _received_us(received_us) {}
    ~ConcurrencyRemover();
private:
    DISALLOW_COPY_AND_ASSIGN(ConcurrencyRemover);
    MethodStatus* _status;
    Controller* _c;
    uint64_t _received_us;
};

inline bool MethodStatus::OnRequested(int* rejected_cc) {
    const int cc = _nconcurrency.fetch_add(1, butil::memory_order_relaxed) + 1;
    if (NULL == _cl || _cl->OnRequested(cc)) {
        return true;
    } 
    if (rejected_cc) {
        *rejected_cc = cc;
    }
    return false;
}

inline void MethodStatus::OnResponded(int error_code, int64_t latency) {
    _nconcurrency.fetch_sub(1, butil::memory_order_relaxed);
    if (0 == error_code) {
        _latency_rec << latency;
    } else {
        _nerror_bvar << 1;
    }
    if (NULL != _cl) {
        _cl->OnResponded(error_code, latency);
    }
}

} // namespace brpc

#endif  //BRPC_METHOD_STATUS_H
