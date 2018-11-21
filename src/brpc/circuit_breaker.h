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

#ifndef BRPC_CIRCUIT_BREAKER_H
#define BRPC_CIRCUIT_BREAKER_H
                                            
#include "butil/atomicops.h"

namespace brpc {

class CircuitBreaker {
public:
    CircuitBreaker();

    ~CircuitBreaker() {}

    // Sampling the current rpc. Returns false if a node needs to 
    // be isolated. Otherwise return true.
    // error_code: Error_code of this call, 0 means success.
    // latency: Time cost of this call.
    // Note: Once OnCallEnd() determined that a node needs to be isolated,
    // it will always return false until you call Reset(). Usually Reset() 
    // will be called in the health check thread.
    bool OnCallEnd(int error_code, int64_t latency);

    // Reset CircuitBreaker and clear history data. will erase the historical 
    // data and start sampling again. Before you call this method, you need to
    // ensure that no one else is accessing CircuitBreaker.
    void Reset();

    // Mark the Socket as broken. Call this method when you want to isolate a 
    // node in advance. When this method is called multiple times in succession, 
    // only the first call will take effect.
    void MarkAsBroken();

    // Number of times marked as broken
    int isolated_times() const {
        return _isolated_times.load(butil::memory_order_relaxed);
    }

    // The duration that should be isolated when the socket fails in milliseconds.
    // The higher the frequency of socket errors, the longer the duration.
    int isolation_duration_ms() const {
        return _isolation_duration_ms.load(butil::memory_order_relaxed);
    }

private:
    void UpdateIsolationDuration();

    class EmaErrorRecorder {
    public:
        EmaErrorRecorder(int windows_size,  int max_error_percent);
        bool OnCallEnd(int error_code, int64_t latency);
        void Reset();

    private:
        int64_t UpdateLatency(int64_t latency);
        bool UpdateErrorCost(int64_t latency, int64_t ema_latency);

        const int _window_size;
        const int _max_error_percent;
        const double _smooth;

        butil::atomic<int32_t> _sample_count_when_initializing;
        butil::atomic<int32_t> _error_count_when_initializing;
        butil::atomic<int64_t> _ema_error_cost;
        butil::atomic<int64_t> _ema_latency;
    };

    EmaErrorRecorder _long_window;
    EmaErrorRecorder _short_window;
    int64_t _last_reset_time_ms; 
    butil::atomic<int> _isolation_duration_ms;
    butil::atomic<int> _isolated_times;
    butil::atomic<bool> _broken;
};

}  // namespace brpc

#endif // BRPC_CIRCUIT_BREAKER_H_
