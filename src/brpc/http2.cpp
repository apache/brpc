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

H2Settings::H2Settings()
    : header_table_size(DEFAULT_HEADER_TABLE_SIZE)
    , enable_push(false)
    , max_concurrent_streams(std::numeric_limits<uint32_t>::max())
    , initial_window_size(DEFAULT_INITIAL_WINDOW_SIZE)
    , max_frame_size(DEFAULT_MAX_FRAME_SIZE)
    , max_header_list_size(std::numeric_limits<uint32_t>::max()) {
}

std::ostream& operator<<(std::ostream& os, const H2Settings& s) {
    os << "{header_table_size=" << s.header_table_size
       << " enable_push=" << s.enable_push
       << " max_concurrent_streams=" << s.max_concurrent_streams
       << " initial_window_size=" << s.initial_window_size
       << " max_frame_size=" << s.max_frame_size
       << " max_header_list_size=" << s.max_header_list_size
       << '}';
    return os;
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
