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
//
// Each 8-byte record is big-endian and decoded as:
//   bits  0..7   error_code
//   bits  8..15  request-tick flag (bit 8 / 0x100)
//   bits  8..31  latency_us (24 bits)
//   bits 20..31  also feed OnRequested concurrency (mod kMaxConcurrency)
//   bits 40..63  synthetic clock delta (plus 1us floor)

#include <cstddef>
#include <cstdint>

#include "butil/time.h"
#include "brpc/policy/auto_concurrency_limiter.h"

namespace {

constexpr size_t kRecordBytes = 8;
// Cap for OnRequested()'s current_concurrency argument: wide enough to
// cross typical limiter ceilings without running into huge ints.
constexpr int kMaxConcurrency = 4096;
constexpr int kErrorCodeBits = 8;
constexpr int kLatencyShift = 8;
constexpr uint64_t kLatencyMask = 0xffffffULL;
constexpr int kRequestFlagBit = 8;   // bit 8 of the record
constexpr int kConcurrencyShift = 20;
constexpr int kClockDeltaShift = 40;

}  // namespace

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
        now_us += static_cast<int64_t>((v >> kClockDeltaShift) + 1);
        const int error_code =
            static_cast<int>(v & ((1ULL << kErrorCodeBits) - 1));
        const int64_t latency_us =
            static_cast<int64_t>((v >> kLatencyShift) & kLatencyMask);
        limiter.AddSample(error_code, latency_us, now_us);
        limiter.MaxConcurrency();
        if ((v & (1ULL << kRequestFlagBit)) || (i & 7) == 0) {
            // a request tick (current concurrency taken from the stream)
            // Controller* is unused by AutoConcurrencyLimiter::OnRequested.
            limiter.OnRequested(
                static_cast<int>((v >> kConcurrencyShift) % kMaxConcurrency),
                nullptr);
        }
    }
    return 0;
}
