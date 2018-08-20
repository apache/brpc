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
    // ensure that no one else is calling OnCallEnd.
    void Reset();
private:
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
        butil::atomic<bool> _init_completed;
        butil::atomic<int>  _sample_count;
        butil::atomic<int64_t> _ema_error_cost;
        butil::atomic<int64_t> _ema_latency;
        butil::atomic<bool> _broken;
    };

    EmaErrorRecorder _long_window;
    EmaErrorRecorder _short_window;
};

}  // namespace brpc

#endif // BRPC_CIRCUIT_BREAKER_H_
