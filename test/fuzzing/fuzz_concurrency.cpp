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
//
// OnResponded() stamps samples with the real clock: filling a sample window
// needs >=100us of wall time per sample and remeasurement cannot start
// until ~25s after construction, so coverage would depend on execution
// speed rather than on the input bytes.  Samples are therefore fed to
// AddSample() at a synthetic clock that every 8-byte record advances --
// the same sampling approach as test/brpc_auto_concurrency_limiter_unittest.cpp
// -- which lets an input jump straight across the sample window and the
// remeasure/reset deadlines.

#include <cstddef>
#include <cstdint>

#include "butil/time.h"
#include "brpc/policy/auto_concurrency_limiter.h"

// Input encoding: a stream of kRecordBytes-byte big-endian records with
// disjoint fields:
//   bits  0..7   error_code (0 = success)
//   bits  8..31  latency_us (24 bits, 0..~16.7M us)
//   bit  32      request-tick flag: also call OnRequested() for this record
//   bits 33..39  current_concurrency passed to OnRequested() (0..127,
//                straddling the limiter's adjusted max_concurrency so both
//                the accept and the ELIMIT outcomes occur)
//   bits 40..63  synthetic clock advance in us, plus a 1us floor
constexpr size_t kRecordBytes = 8;
constexpr uint64_t kErrorCodeMask = 0xff;
constexpr int kLatencyShift = 8;
constexpr uint64_t kLatencyMask = 0xffffff;
constexpr int kRequestTickShift = 32;
constexpr int kConcurrencyShift = 33;
constexpr uint64_t kConcurrencyMask = 0x7f;
constexpr int kClockAdvanceShift = 40;
// Issue a request tick every kRequestTickPeriod records even without the
// flag, so OnRequested()'s outcomes are exercised regularly.
constexpr size_t kRequestTickPeriod = 8;

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < kRecordBytes) {
        return 0;
    }

    brpc::policy::AutoConcurrencyLimiter limiter;
    int64_t now_us = butil::cpuwide_time_us();

    const size_t n = size / kRecordBytes;
    for (size_t i = 0; i < n; ++i) {
        uint64_t v = 0;
        for (size_t b = 0; b < kRecordBytes; ++b) {
            v = (v << 8) | data[i * kRecordBytes + b];
        }
        // Advance the synthetic clock by 1us..~16.8s per record.  The +1us
        // floor keeps the sampled window width strictly positive --
        // UpdateMaxConcurrency() divides by it when computing qps.
        now_us += static_cast<int64_t>((v >> kClockAdvanceShift) + 1);
        const int error_code = static_cast<int>(v & kErrorCodeMask);
        const int64_t latency_us =
            static_cast<int64_t>((v >> kLatencyShift) & kLatencyMask);
        limiter.AddSample(error_code, latency_us, now_us);
        limiter.MaxConcurrency();
        if (((v >> kRequestTickShift) & 1) != 0 ||
            (i % kRequestTickPeriod) == 0) {
            // A request tick (current concurrency taken from the stream).
            // Controller* is unused by AutoConcurrencyLimiter::OnRequested,
            // so nullptr is safe.
            limiter.OnRequested(
                static_cast<int>((v >> kConcurrencyShift) & kConcurrencyMask),
                nullptr);
        }
    }
    return 0;
}
