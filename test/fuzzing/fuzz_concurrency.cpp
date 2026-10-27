// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to You under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and
// limitations under the License.

// Fuzz the auto concurrency limiter (src/brpc/policy/auto_concurrency_limiter.cpp):
// fuzz bytes drive a stream of (error_code, latency_us) responses and
// request ticks through the real gradient-based limiter, including the
// recovery/reset paths exercised by bursts of errors.

#include "brpc/policy/auto_concurrency_limiter.h"

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) {
        return 0;
    }

    brpc::policy::AutoConcurrencyLimiter limiter;

    const size_t n = size / 8;
    for (size_t i = 0; i < n; ++i) {
        uint64_t v = 0;
        for (size_t b = 0; b < 8; ++b) {
            v = (v << 8) | data[i * 8 + b];
        }
        const int error_code = static_cast<int>(v & 0xff);
        const int64_t latency_us = static_cast<int64_t>(v >> 8) % 10000000;
        limiter.OnResponded(error_code, latency_us);
        limiter.MaxConcurrency();
        if ((v & 0x100) || (i & 7) == 0) {
            // a request tick (current concurrency taken from the stream)
            limiter.OnRequested(static_cast<int>((v >> 20) % 5096), nullptr);
        }
    }
    return 0;
}
