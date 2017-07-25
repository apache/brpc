// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/03/20 17:43:39

#ifndef  BRPC_METHOD_STATUS_H
#define  BRPC_METHOD_STATUS_H

#include "base/macros.h"                  // DISALLOW_COPY_AND_ASSIGN
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
    int Expose(const base::StringPiece& prefix);

    // Describe internal vars, used by /status
    void Describe(std::ostream &os, const DescribeOptions&) const;

    int max_concurrency() const { return _max_concurrency; }
    int& max_concurrency() { return _max_concurrency; }
    
private:
friend class ScopedMethodStatus;
    DISALLOW_COPY_AND_ASSIGN(MethodStatus);
    void OnError();

    bvar::LatencyRecorder        _latency_rec;
    bvar::Adder<int64_t>         _nerror;
    bvar::PassiveStatus<int>     _nprocessing_bvar;
    int _max_concurrency;
    base::atomic<int> BAIDU_CACHELINE_ALIGNMENT _nprocessing;
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
    const int last_nproc = _nprocessing.fetch_add(1, base::memory_order_relaxed);
    // _max_concurrency may be changed by user at any time.
    const int saved_max_concurrency = _max_concurrency;
    return (saved_max_concurrency <= 0 || last_nproc < saved_max_concurrency);
}

inline void MethodStatus::OnResponded(bool success, int64_t latency) {
    if (success) {
        _latency_rec << latency;
        _nprocessing.fetch_sub(1, base::memory_order_relaxed);
    } else {
        OnError();
    }
}

inline void MethodStatus::OnError() {
    _nerror << 1;
    _nprocessing.fetch_sub(1, base::memory_order_relaxed);
}

} // namespace brpc


#endif  //BRPC_METHOD_STATUS_H
