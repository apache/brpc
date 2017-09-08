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


namespace brpc {

// Record accessing stats of a method.
class MethodStatus : public Describable {
public:
    MethodStatus();
    ~MethodStatus();

    // Call this function when the method is about to be called.
    // Returns false when the request reaches max_concurrency to the method
    // and is suggested to be rejected.
    bool OnRequested();

    // Call this when the method just finished.
    // `success' : successful call or not.
    // `latency_us' : microseconds taken by a successful call. Latency can
    // be measured in this utility class as well, but the callsite often
    // did the time keeping and the cost is better saved. If `success' is
    // false, `latency_us' is not used.
    void OnResponded(bool success, int64_t latency_us);

    // Expose internal vars.
    // Return 0 on success, -1 otherwise.
    int Expose(const butil::StringPiece& prefix);

    // Describe internal vars, used by /status
    void Describe(std::ostream &os, const DescribeOptions&) const;

    int max_concurrency() const { return _max_concurrency; }
    int& max_concurrency() { return _max_concurrency; }
    
private:
friend class ScopedMethodStatus;
    DISALLOW_COPY_AND_ASSIGN(MethodStatus);
    void OnError();

    int _max_concurrency;
    bvar::Adder<int64_t>         _nerror;
    bvar::LatencyRecorder        _latency_rec;
    bvar::PassiveStatus<int>     _nprocessing_bvar;
    butil::atomic<int> BAIDU_CACHELINE_ALIGNMENT _nprocessing;
};

// If release() is not called before destruction of this object,
// an error will be counted.
class ScopedMethodStatus {
public:
    ScopedMethodStatus(MethodStatus* status) : _status(status) {}
    ~ScopedMethodStatus() {
        if (_status) {
            _status->OnError();
            _status = NULL;
        }
    }
    MethodStatus* release() {
        MethodStatus* tmp = _status;
        _status = NULL;
        return tmp;
    }
    operator MethodStatus* () const { return _status; }
private:
    DISALLOW_COPY_AND_ASSIGN(ScopedMethodStatus);
    MethodStatus* _status;
};

inline bool MethodStatus::OnRequested() {
    const int last_nproc = _nprocessing.fetch_add(1, butil::memory_order_relaxed);
    // _max_concurrency may be changed by user at any time.
    const int saved_max_concurrency = _max_concurrency;
    return (saved_max_concurrency <= 0 || last_nproc < saved_max_concurrency);
}

inline void MethodStatus::OnResponded(bool success, int64_t latency) {
    if (success) {
        _latency_rec << latency;
        _nprocessing.fetch_sub(1, butil::memory_order_relaxed);
    } else {
        OnError();
    }
}

inline void MethodStatus::OnError() {
    _nerror << 1;
    _nprocessing.fetch_sub(1, butil::memory_order_relaxed);
}

} // namespace brpc


#endif  //BRPC_METHOD_STATUS_H
