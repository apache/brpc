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

#ifndef BRPC_HANDSHAKE_HANDSHAKE_FRAME_H
#define BRPC_HANDSHAKE_HANDSHAKE_FRAME_H

#include <cstddef>
#include <string>

#include "brpc/handshake/handshake_io.h"

namespace brpc {
namespace handshake {

enum FrameResult {
    FRAME_OK = 0,
    FRAME_NOT_MINE,
    FRAME_NEED_MORE,
    FRAME_IO_ERROR,
    FRAME_PROTOCOL_ERROR,
};

struct FrameSpec {
    enum LengthEncoding {
        FIXED,
        U16_TOTAL_LENGTH,
        U32_BODY_LENGTH,
    };

    FrameSpec()
        : magic(nullptr), magic_len(0), min_frame_len(0), max_frame_len(0),
          length_encoding(FIXED) {}

    FrameSpec(const char* magic_in, size_t magic_len_in,
              size_t min_frame_len_in, size_t max_frame_len_in,
              LengthEncoding length_encoding_in)
        : magic(magic_in), magic_len(magic_len_in),
          min_frame_len(min_frame_len_in),
          max_frame_len(max_frame_len_in),
          length_encoding(length_encoding_in) {}

    const char* magic;
    size_t magic_len;
    size_t min_frame_len;
    size_t max_frame_len;
    LengthEncoding length_encoding;
};

// Handles only framing. Protocol implementations receive and produce payloads
// after magic/length fields and remain responsible for their own business
// fields and version semantics.
class FrameCodec {
public:
    static FrameResult Encode(const FrameSpec& spec,
                              const std::string& payload,
                              std::string* frame);
    static FrameResult ReadFrame(HandshakeIO* io, const FrameSpec& spec,
                                 bool push_back_on_not_mine,
                                 std::string* payload);
    static FrameResult ParseBufferedFrame(HandshakeInput* input,
                                          const FrameSpec& spec,
                                          std::string* payload,
                                          bool* magic_matched = nullptr);
    static FrameResult WriteFrame(HandshakeIO* io, const FrameSpec& spec,
                                  const std::string& payload);
    static FrameResult DrainFrame(HandshakeIO* io, const FrameSpec& spec);

private:
    static size_t LengthFieldSize(const FrameSpec& spec);
    static FrameResult DecodeLength(const FrameSpec& spec,
                                    const void* header,
                                    size_t* frame_len);
};

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_HANDSHAKE_HANDSHAKE_FRAME_H
