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

#include "brpc/circuit_breaker.h"

#include <cmath>
#include <gflags/gflags.h>

#include "brpc/errno.pb.h"
#include "butil/time.h"

namespace brpc {

DEFINE_int32(circuit_breaker_short_window_size, 1500,
    "Short window sample size.");
DEFINE_int32(circuit_breaker_long_window_size, 3000,
    "Long window sample size.");
DEFINE_int32(circuit_breaker_short_window_error_percent, 10,
    "The maximum error rate allowed by the short window, ranging from 0-99.");
DEFINE_int32(circuit_breaker_long_window_error_percent, 5,
    "The maximum error rate allowed by the long window, ranging from 0-99.");
DEFINE_int32(circuit_breaker_min_error_cost_us, 500,
    "The minimum error_cost, when the ema of error cost is less than this "
    "value, it will be set to zero.");
DEFINE_int32(circuit_breaker_max_failed_latency_mutiple, 2,
    "The maximum multiple of the latency of the failed request relative to "
    "the average latency of the success requests.");
DEFINE_int32(circuit_breaker_min_isolation_duration_ms, 100,
    "Minimum isolation duration in milliseconds");
DEFINE_int32(circuit_breaker_max_isolation_duration_ms, 30000,
    "Maximum isolation duration in milliseconds");
DEFINE_double(circuit_breaker_epsilon_value, 0.02,
    "ema_alpha = 1 - std::pow(epsilon, 1.0 / window_size)");

namespace {
// EPSILON is used to generate the smoothing coefficient when calculating EMA.
// The larger the EPSILON, the larger the smoothing coefficient, which means
// that the proportion of early data is larger.
// smooth = pow(EPSILON, 1 / window_size),
// eg: when window_size = 100,
// EPSILON = 0.1, smooth = 0.9772
// EPSILON = 0.3, smooth = 0.9880
// when window_size = 1000,
// EPSILON = 0.1, smooth = 0.9977
// EPSILON = 0.3, smooth = 0.9987

#define EPSILON (FLAGS_circuit_breaker_epsilon_value)

}  // namespace

CircuitBreaker::EmaErrorRecorder::EmaErrorRecorder(int window_size,
                                                   int max_error_percent)
    : _window_size(window_size)
    , _max_error_percent(max_error_percent)
    , _smooth(std::pow(EPSILON, 1.0/window_size))
    , _sample_count_when_initializing(0)
    , _error_count_when_initializing(0)
    , _ema_error_cost(0)
    , _ema_latency(0) {
}

bool CircuitBreaker::EmaErrorRecorder::OnCallEnd(int error_code,
                                                 int64_t latency) {
    int64_t ema_latency = 0;
    bool healthy = false;
    if (error_code == 0) {
        ema_latency = UpdateLatency(latency);
        healthy = UpdateErrorCost(0, ema_latency);
    } else {
        ema_latency = _ema_latency.load(butil::memory_order_relaxed);
        healthy = UpdateErrorCost(latency, ema_latency);
    }

    // When the window is initializing, use error_rate to determine
    // if it needs to be isolated.
    if (_sample_count_when_initializing.load(butil::memory_order_relaxed) < _window_size &&
        _sample_count_when_initializing.fetch_add(1, butil::memory_order_relaxed) < _window_size) {
        if (error_code != 0) {
            const int32_t error_count =
                _error_count_when_initializing.fetch_add(1, butil::memory_order_relaxed);
            return error_count < _window_size * _max_error_percent / 100;
        }
        // Because once OnCallEnd returned false, the node will be ioslated soon,
        // so when error_code=0, we no longer check the error count.
        return true;
    }

    return healthy;
}

void CircuitBreaker::EmaErrorRecorder::Reset() {
    if (_sample_count_when_initializing.load(butil::memory_order_relaxed) < _window_size) {
        _sample_count_when_initializing.store(0, butil::memory_order_relaxed);
        _error_count_when_initializing.store(0, butil::memory_order_relaxed);
        _ema_latency.store(0, butil::memory_order_relaxed);
    }
    _ema_error_cost.store(0, butil::memory_order_relaxed);
}

int64_t CircuitBreaker::EmaErrorRecorder::UpdateLatency(int64_t latency) {
    int64_t ema_latency = _ema_latency.load(butil::memory_order_relaxed);
    do {
        int64_t next_ema_latency = 0;
        if (0 == ema_latency) {
            next_ema_latency = latency;
        } else {
            next_ema_latency = ema_latency * _smooth + latency * (1 - _smooth);
        }
        if (_ema_latency.compare_exchange_weak(ema_latency, next_ema_latency)) {
            return next_ema_latency;
        }
    } while(true);
}

bool CircuitBreaker::EmaErrorRecorder::UpdateErrorCost(int64_t error_cost,
                                                       int64_t ema_latency) {
    const int max_mutiple = FLAGS_circuit_breaker_max_failed_latency_mutiple;
    if (ema_latency != 0) {
        error_cost = std::min(ema_latency * max_mutiple, error_cost);
    }
    //Errorous response
    if (error_cost != 0) {
        int64_t ema_error_cost =
            _ema_error_cost.fetch_add(error_cost, butil::memory_order_relaxed);
        ema_error_cost += error_cost;
        const int64_t max_error_cost =
            ema_latency * _window_size * (_max_error_percent / 100.0) * (1.0 + EPSILON);
        return ema_error_cost <= max_error_cost;
    }

    //Ordinary response
    int64_t ema_error_cost = _ema_error_cost.load(butil::memory_order_relaxed);
    do {
        if (ema_error_cost == 0) {
            break;
        } else if (ema_error_cost < FLAGS_circuit_breaker_min_error_cost_us) {
            if (_ema_error_cost.compare_exchange_weak(
                ema_error_cost, 0, butil::memory_order_relaxed)) {
                break;
            }
        } else {
            int64_t next_ema_error_cost = ema_error_cost * _smooth;
            if (_ema_error_cost.compare_exchange_weak(
                ema_error_cost, next_ema_error_cost)) {
                break;
            }
        }
    } while (true);
    return true;
}

CircuitBreaker::CircuitBreaker()
    : _long_window(FLAGS_circuit_breaker_long_window_size,
                   FLAGS_circuit_breaker_long_window_error_percent)
    , _short_window(FLAGS_circuit_breaker_short_window_size,
                    FLAGS_circuit_breaker_short_window_error_percent)
    , _last_reset_time_ms(0)
    , _isolation_duration_ms(FLAGS_circuit_breaker_min_isolation_duration_ms)
    , _isolated_times(0)
    , _broken(false) {
}

bool CircuitBreaker::OnCallEnd(int error_code, int64_t latency) {
    // If the server has reached its maximum concurrency, it will return
    // ELIMIT directly when a new request arrives. This usually means that
    // the entire downstream cluster is overloaded. If we isolate nodes at
    // this time, may increase the pressure on downstream. On the other hand,
    // since the latency corresponding to ELIMIT is usually very small, we
    // cannot handle it as a successful request. Here we simply ignore the requests
    // that returned ELIMIT.
    if (error_code == ELIMIT) {
        return true;
    }
    if (_broken.load(butil::memory_order_relaxed)) {
        return false;
    }
    if (_long_window.OnCallEnd(error_code, latency) &&
        _short_window.OnCallEnd(error_code, latency)) {
        return true;
    }
    MarkAsBroken();
    return false;
}

void CircuitBreaker::Reset() {
    _long_window.Reset();
    _short_window.Reset();
    _last_reset_time_ms = butil::cpuwide_time_ms();
    _broken.store(false, butil::memory_order_release);
}

void CircuitBreaker::MarkAsBroken() {
    if (!_broken.exchange(true, butil::memory_order_acquire)) {
        _isolated_times.fetch_add(1, butil::memory_order_relaxed);
        UpdateIsolationDuration();
    }
}

void CircuitBreaker::UpdateIsolationDuration() {
    int64_t now_time_ms = butil::cpuwide_time_ms();
    int isolation_duration_ms = _isolation_duration_ms.load(butil::memory_order_relaxed);
    const int max_isolation_duration_ms =
        FLAGS_circuit_breaker_max_isolation_duration_ms;
    const int min_isolation_duration_ms =
        FLAGS_circuit_breaker_min_isolation_duration_ms;
    if (now_time_ms - _last_reset_time_ms < max_isolation_duration_ms) {
        isolation_duration_ms =
            std::min(isolation_duration_ms * 2, max_isolation_duration_ms);
    } else {
        isolation_duration_ms = min_isolation_duration_ms;
    }
    _isolation_duration_ms.store(isolation_duration_ms, butil::memory_order_relaxed);
}


}  // namespace brpc
