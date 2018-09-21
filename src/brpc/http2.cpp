// Copyright (c) 2014 Baidu, Inc.
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

#include "brpc/http2.h"
#include "brpc/details/hpack.h"
#include <limits>
#include "butil/logging.h"

namespace brpc {


enum H2SettingsIdentifier {
    HTTP2_SETTINGS_HEADER_TABLE_SIZE      = 0x1,
    HTTP2_SETTINGS_ENABLE_PUSH            = 0x2,
    HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    HTTP2_SETTINGS_INITIAL_WINDOW_SIZE    = 0x4,
    HTTP2_SETTINGS_MAX_FRAME_SIZE         = 0x5,
    HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE   = 0x6
};

inline uint16_t LoadUint16(butil::IOBufBytesIterator& it) {
    uint16_t v = *it; ++it;
    v = ((v << 8) | *it); ++it;
    return v;
}
inline uint32_t LoadUint32(butil::IOBufBytesIterator& it) {
    uint32_t v = *it; ++it;
    v = ((v << 8) | *it); ++it;
    v = ((v << 8) | *it); ++it;
    v = ((v << 8) | *it); ++it;
    return v;
}
inline void SaveUint16(void* out, uint16_t v) {
    uint8_t* p = (uint8_t*)out;
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}
inline void SaveUint32(void* out, uint32_t v) {
    uint8_t* p = (uint8_t*)out;
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

H2Settings::H2Settings()
    : header_table_size(DEFAULT_HEADER_TABLE_SIZE)
    , enable_push(DEFAULT_ENABLE_PUSH)
    , max_concurrent_streams(std::numeric_limits<uint32_t>::max())
    , initial_window_size(DEFAULT_INITIAL_WINDOW_SIZE)
    , max_frame_size(DEFAULT_MAX_FRAME_SIZE)
    , max_header_list_size(std::numeric_limits<uint32_t>::max()) {
}

bool H2Settings::ParseFrom(butil::IOBufBytesIterator& it, size_t n) {
    const uint32_t npairs = n / 6;
    if (npairs * 6 != n) {
        LOG(ERROR) << "Invalid payload_size=" << n;
        return false;
    }
    for (uint32_t i = 0; i < npairs; ++i) {
        uint16_t id = LoadUint16(it);
        uint32_t value = LoadUint32(it);
        switch (static_cast<H2SettingsIdentifier>(id)) {
        case HTTP2_SETTINGS_HEADER_TABLE_SIZE:
            header_table_size = value;
            break;
        case HTTP2_SETTINGS_ENABLE_PUSH:
            if (value > 1) {
                LOG(ERROR) << "Invalid value=" << value << " for ENABLE_PUSH";
                return false;
            }
            enable_push = value;
            break;
        case HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
            max_concurrent_streams = value;
            break;
        case HTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
            if (value > MAX_INITIAL_WINDOW_SIZE) {
                LOG(ERROR) << "Invalid initial_window_size=" << value;
                return false;
            }
            initial_window_size = value;
            break;
        case HTTP2_SETTINGS_MAX_FRAME_SIZE:
            if (value > MAX_OF_MAX_FRAME_SIZE ||
                value < DEFAULT_MAX_FRAME_SIZE) {
                LOG(ERROR) << "Invalid max_frame_size=" << value;
                return false;
            }
            max_frame_size = value;
            break;
        case HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
            max_header_list_size = value;
            break;
        default:
            // An endpoint that receives a SETTINGS frame with any unknown or
            // unsupported identifier MUST ignore that setting (section 6.5.2)
            LOG(WARNING) << "Unknown setting, id=" << id << " value=" << value;
            break;
        }
    }
    return true;
}

size_t H2Settings::ByteSize() const {
    size_t size = 0;
    if (header_table_size != DEFAULT_HEADER_TABLE_SIZE) {
        size += 6;
    }
    if (enable_push != DEFAULT_ENABLE_PUSH) {
        size += 6;
    }
    if (max_concurrent_streams != std::numeric_limits<uint32_t>::max()) {
        size += 6;
    }
    if (initial_window_size != DEFAULT_INITIAL_WINDOW_SIZE) {
        size += 6;
    }
    if (max_frame_size != DEFAULT_MAX_FRAME_SIZE) {
        size += 6;
    }
    if (max_header_list_size != std::numeric_limits<uint32_t>::max()) {
        size += 6;
    }
    return size;
}

size_t H2Settings::SerializeTo(void* out) const {
    uint8_t* p = (uint8_t*)out;
    if (header_table_size != DEFAULT_HEADER_TABLE_SIZE) {
        SaveUint16(p, HTTP2_SETTINGS_HEADER_TABLE_SIZE);
        SaveUint32(p + 2, header_table_size);
        p += 6;
    }
    if (enable_push != DEFAULT_ENABLE_PUSH) {
        SaveUint16(p, HTTP2_SETTINGS_ENABLE_PUSH);
        SaveUint32(p + 2, enable_push);
        p += 6;
    }
    if (max_concurrent_streams != std::numeric_limits<uint32_t>::max()) {
        SaveUint16(p, HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
        SaveUint32(p + 2, max_concurrent_streams);
        p += 6;
    }
    if (initial_window_size != DEFAULT_INITIAL_WINDOW_SIZE) {
        SaveUint16(p, HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
        SaveUint32(p + 2, initial_window_size);
        p += 6;
    }
    if (max_frame_size != DEFAULT_MAX_FRAME_SIZE) {
        SaveUint16(p, HTTP2_SETTINGS_MAX_FRAME_SIZE);
        SaveUint32(p + 2, max_frame_size);
        p += 6;
    }
    if (max_header_list_size != std::numeric_limits<uint32_t>::max()) {
        SaveUint16(p, HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE);
        SaveUint32(p + 2, max_header_list_size);
        p += 6;
    }
    return static_cast<size_t>(p - (uint8_t*)out);
}

void H2Settings::Print(std::ostream& os) const {
    os << "{header_table_size=" << header_table_size
       << " enable_push=" << enable_push
       << " max_concurrent_streams=" << max_concurrent_streams
       << " initial_window_size=" << initial_window_size
       << " max_frame_size=" << max_frame_size
       << " max_header_list_size=" << max_header_list_size
       << '}';
}

const char* H2ErrorToString(H2Error e) {
    switch (e) {
    case H2_NO_ERROR: return "NO_ERROR";
    case H2_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
    case H2_INTERNAL_ERROR: return "INTERNAL_ERROR";
    case H2_FLOW_CONTROL_ERROR: return "FLOW_CONTROL_ERROR";
    case H2_SETTINGS_TIMEOUT: return "SETTINGS_TIMEOUT";
    case H2_STREAM_CLOSED_ERROR: return "STREAM_CLOSED";
    case H2_FRAME_SIZE_ERROR: return "FRAME_SIZE_ERROR";
    case H2_REFUSED_STREAM: return "REFUSED_STREAM";
    case H2_CANCEL: return "CANCEL";
    case H2_COMPRESSION_ERROR: return "COMPRESSION_ERROR";
    case H2_CONNECT_ERROR: return "CONNECT_ERROR";
    case H2_ENHANCE_YOUR_CALM: return "ENHANCE_YOUR_CALM";
    case H2_INADEQUATE_SECURITY: return "INADEQUATE_SECURITY";
    case H2_HTTP_1_1_REQUIRED: return "HTTP_1_1_REQUIRED";
    }
    return "Unknown-H2Error";
}


} // namespace brpc
