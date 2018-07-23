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
                                            
#include "butil/containers/doubly_buffered_data.h"
#include "butil/atomicops.h"

namespace brpc {

class CircuitBreaker {
public:
    CircuitBreaker();

    ~CircuitBreaker() {}

    // Sampling the current rpc. Returns false if the endpoint needs to 
    // be deactivated. Otherwise return true.
    // error_code: Error_code of this call, 0 means success.
    // latency: Time cost of this call.
    // Note: Once OnCallEnd() determines that a node needs to be deactivated,
    // it will always return false until you call Reset(). Usually Reset() 
    // will be called in the health check thread.
    bool OnCallEnd(int error_code, int64_t latency);

    // Reset circuit breaker, will erase the historical data and start 
    // sampling again.
    // This method is thread safe, and it is inefficient, you better 
    // call it only when you need.
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
        void OnStarting(int error_code, int64_t latency);

        const int _window_size;
        const int _max_error_percent;
        const double _smooth;
        butil::atomic<bool> _init_completed;
        butil::atomic<int>  _sample_count;
        butil::atomic<int64_t> _ema_error_cost;
        butil::atomic<int64_t> _ema_latency;
        butil::atomic<bool> _broken;
    };
    typedef std::vector<std::unique_ptr<EmaErrorRecorder>> ErrRecorderList;

    static bool ResetEmaRecorders(ErrRecorderList& recorders) {
        for (auto& recorder : recorders) {
            recorder->Reset();
        }
        return true;
    }
    
    static bool AddErrorRecorder(ErrRecorderList& recorders,
                                 int window_size, int max_error_percent){
        recorders.emplace_back(
            new EmaErrorRecorder(window_size, max_error_percent));
        return true;
    }

    butil::DoublyBufferedData<ErrRecorderList> _recorders;
};

}  // namespace brpc

#endif // BRPC_CIRCUIT_BREAKER_H_
