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

#ifndef BRPC_URMA_STREAM_TRACE_H
#define BRPC_URMA_STREAM_TRACE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "butil/synchronization/lock.h"
#include "urma_types.h"

namespace brpc {
namespace urma {

// Diagnostic-only, non-cryptographic fingerprints. No protocol bytes change.
class UrmaStreamTrace {
public:
    enum Stage { TX, RX_CQ, RX_USE, STAGE_COUNT };
    static constexpr size_t CAPACITY = 32;
    static constexpr size_t PREFIX_SIZE = 16;

    struct Summary {
        uint64_t hash = UINT64_C(14695981039346656037);
        size_t length = 0;
        std::string prefix;
    };

    static std::string HexPrefix(const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        static const char digits[] = "0123456789abcdef";
        std::string hex;
        const size_t count = size < PREFIX_SIZE ? size : PREFIX_SIZE;
        hex.reserve(count * 2);
        for (size_t i = 0; i < count; ++i) {
            hex.push_back(digits[bytes[i] >> 4]);
            hex.push_back(digits[bytes[i] & 15]);
        }
        return hex;
    }

    static Summary Summarize(const urma_sge_t* sges, size_t count) {
        Summary summary;
        for (size_t i = 0; i < count; ++i) {
            const auto* bytes =
                reinterpret_cast<const unsigned char*>(sges[i].addr);
            for (uint32_t j = 0; j < sges[i].len; ++j) {
                summary.hash ^= bytes[j];
                summary.hash *= UINT64_C(1099511628211);
            }
            if (summary.length < PREFIX_SIZE) {
                const size_t room = PREFIX_SIZE - summary.length;
                const size_t prefix_len = std::min<size_t>(sges[i].len, room);
                summary.prefix += HexPrefix(bytes, prefix_len);
            }
            summary.length += sges[i].len;
        }
        return summary;
    }

    void Record(Stage stage, size_t slot, uint64_t user_ctx, uint64_t imm,
                const urma_sge_t* sges, size_t count) {
        Entry entry;
        entry.stage = stage;
        entry.slot = slot;
        entry.user_ctx = user_ctx;
        entry.imm = imm;
        entry.sges = count;
        entry.summary = Summarize(sges, count);
        std::lock_guard<butil::Mutex> lock(_mutex);
        entry.sequence = _sequences[stage]++;
        entry.offset = _offsets[stage];
        _offsets[stage] += entry.summary.length;
        _entries[_written++ % CAPACITY] = entry;
    }

    // One bounded dump per connection; Record is safe for concurrent TX/CQ.
    std::string DumpOnce() {
        std::lock_guard<butil::Mutex> lock(_mutex);
        if (_dumped || _written == 0) {
            return std::string();
        }
        _dumped = true;
        const char* names[] = {"TX", "RX_CQ", "RX_USE"};
        std::ostringstream out;
        const size_t first = _written > CAPACITY ? _written - CAPACITY : 0;
        out << "records=" << _written - first << " total=" << _written;
        for (size_t i = first; i < _written; ++i) {
            const Entry& entry = _entries[i % CAPACITY];
            out << "\nURMA_TRACE stage=" << names[entry.stage]
                << " seq=" << entry.sequence << " offset=" << entry.offset
                << " len=" << entry.summary.length << " slot=" << entry.slot
                << " user_ctx=" << entry.user_ctx << " imm=" << entry.imm
                << " sges=" << entry.sges << " hash=" << std::hex
                << entry.summary.hash << std::dec
                << " prefix=" << entry.summary.prefix;
        }
        return out.str();
    }

private:
    struct Entry {
        Stage stage = TX;
        size_t slot = 0;
        uint64_t user_ctx = 0;
        uint64_t imm = 0;
        size_t sges = 0;
        uint64_t sequence = 0;
        uint64_t offset = 0;
        Summary summary;
    };
    butil::Mutex _mutex;
    std::array<Entry, CAPACITY> _entries;
    std::array<uint64_t, STAGE_COUNT> _sequences{};
    std::array<uint64_t, STAGE_COUNT> _offsets{};
    size_t _written = 0;
    bool _dumped = false;
};

}  // namespace urma
}  // namespace brpc
#endif  // BRPC_URMA_STREAM_TRACE_H
