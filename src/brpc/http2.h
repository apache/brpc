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

#ifndef BAIDU_RPC_HTTP2_H
#define BAIDU_RPC_HTTP2_H

#include "brpc/http_status_code.h"

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

namespace brpc {

struct H2Settings {
    // Construct with default values.
    H2Settings();

    // Returns true iff all options are valid.
    bool IsValid(bool log_error = false) const;

    // Allows the sender to inform the remote endpoint of the maximum size of
    // the header compression table used to decode header blocks, in octets.
    // The encoder can select any size equal to or less than this value by
    // using signaling specific to the header compression format inside a
    // header block (see [COMPRESSION]).
    // Default: 4096
    static const uint32_t DEFAULT_HEADER_TABLE_SIZE = 4096;
    uint32_t header_table_size;

    // Enable server push or not (Section 8.2).
    // An endpoint MUST NOT send a PUSH_PROMISE frame if it receives this
    // parameter set to a value of 0. An endpoint that has both set this
    // parameter to 0 and had it acknowledged MUST treat the receipt of a
    // PUSH_PROMISE frame as a connection error (Section 5.4.1) of type
    // PROTOCOL_ERROR.
    // Default: false (server push is disabled)
    static const bool DEFAULT_ENABLE_PUSH = true;
    bool enable_push;

    // The maximum number of concurrent streams that the sender will allow.
    // This limit is directional: it applies to the number of streams that the
    // sender permits the receiver to create. It is recommended that this value
    // be no smaller than 100, so as to not unnecessarily limit parallelism.
    // 0 prevents the creation of new streams. However, this can also happen
    // for any limit that is exhausted with active streams. Servers SHOULD only
    // set a zero value for short durations; if a server does not wish to
    // accept requests, closing the connection is more appropriate.
    // Default: unlimited
    uint32_t max_concurrent_streams;

    // Sender's initial window size (in octets) for stream-level flow control.
    // This setting affects the window size of all streams (see Section 6.9.2).
    // Values above the maximum flow-control window size of 2^31-1 are treated
    // as a connection error (Section 5.4.1) of type FLOW_CONTROL_ERROR
    // Default: 256 * 1024
    static const uint32_t DEFAULT_INITIAL_WINDOW_SIZE = 65535;
    static const uint32_t MAX_WINDOW_SIZE = (1u << 31) - 1;
    uint32_t stream_window_size;

    // Initial window size for connection-level flow control.
    // Default: 1024 * 1024
    // Setting to zero stops printing this field.
    uint32_t connection_window_size;

    // Size of the largest frame payload that the sender is willing to receive,
    // in octets. The value advertised by an endpoint MUST be between 16384 and
    // 16777215, inclusive. Values outside this range are treated as a
    // connection error(Section 5.4.1) of type PROTOCOL_ERROR.
    // Default: 16384
    static const uint32_t DEFAULT_MAX_FRAME_SIZE = 16384;
    static const uint32_t MAX_OF_MAX_FRAME_SIZE = 16777215;
    uint32_t max_frame_size;

    // This advisory setting informs a peer of the maximum size of header list
    // that the sender is prepared to accept, in octets. The value is based on
    // the uncompressed size of header fields, including the length of the name
    // and value in octets plus an overhead of 32 octets for each header field.
    // For any given request, a lower limit than what is advertised MAY be
    // enforced.
    // Default: unlimited.
    uint32_t max_header_list_size;
};

std::ostream& operator<<(std::ostream& os, const H2Settings& s);

enum H2Error {
    H2_NO_ERROR            = 0x0, // Graceful shutdown
    H2_PROTOCOL_ERROR      = 0x1, // Protocol error detected
    H2_INTERNAL_ERROR      = 0x2, // Implementation fault
    H2_FLOW_CONTROL_ERROR  = 0x3, // Flow-control limits exceeded
    H2_SETTINGS_TIMEOUT    = 0x4, // Settings not acknowledged  
    H2_STREAM_CLOSED_ERROR = 0x5, // Frame received for closed stream
    H2_FRAME_SIZE_ERROR    = 0x6, // Frame size incorrect
    H2_REFUSED_STREAM      = 0x7, // Stream not processed
    H2_CANCEL              = 0x8, // Stream cancelled
    H2_COMPRESSION_ERROR   = 0x9, // Compression state not updated
    H2_CONNECT_ERROR       = 0xa, // TCP connection error for CONNECT method
    H2_ENHANCE_YOUR_CALM   = 0xb, // Processing capacity exceeded
    H2_INADEQUATE_SECURITY = 0xc, // Negotiated TLS parameters not acceptable
    H2_HTTP_1_1_REQUIRED   = 0xd, // Use HTTP/1.1 for the request   
};

// Get description of the error.
const char* H2ErrorToString(H2Error e);

// Convert the error to status code with similar semantics
int H2ErrorToStatusCode(H2Error e);

} // namespace brpc

#endif  // BAIDU_RPC_HTTP2_H
