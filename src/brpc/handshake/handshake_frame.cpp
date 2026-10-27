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

#include "brpc/handshake/handshake_frame.h"

#include <cstdint>
#include <cstring>
#include <limits>

#include "butil/sys_byteorder.h"

namespace brpc {
namespace handshake {

size_t FrameCodec::LengthFieldSize(const FrameSpec& spec) {
    switch (spec.length_encoding) {
    case FrameSpec::FIXED: return 0;
    case FrameSpec::U16_TOTAL_LENGTH: return sizeof(uint16_t);
    case FrameSpec::U32_BODY_LENGTH: return sizeof(uint32_t);
    }
    return 0;
}

FrameResult FrameCodec::DecodeLength(const FrameSpec& spec,
                                     const void* header,
                                     size_t* frame_len) {
    const size_t length_size = LengthFieldSize(spec);
    const size_t header_len = spec.magic_len + length_size;
    if (spec.min_frame_len < header_len ||
        spec.max_frame_len < spec.min_frame_len) {
        return FRAME_PROTOCOL_ERROR;
    }

    if (spec.length_encoding == FrameSpec::FIXED) {
        if (spec.min_frame_len != spec.max_frame_len) {
            return FRAME_PROTOCOL_ERROR;
        }
        *frame_len = spec.min_frame_len;
    } else if (spec.length_encoding == FrameSpec::U16_TOTAL_LENGTH) {
        uint16_t total_be = 0;
        memcpy(&total_be, static_cast<const char*>(header) + spec.magic_len,
               sizeof(total_be));
        *frame_len = butil::NetToHost16(total_be);
    } else {
        uint32_t body_be = 0;
        memcpy(&body_be, static_cast<const char*>(header) + spec.magic_len,
               sizeof(body_be));
        const size_t body_len = butil::NetToHost32(body_be);
        if (body_len > std::numeric_limits<size_t>::max() - header_len) {
            return FRAME_PROTOCOL_ERROR;
        }
        *frame_len = header_len + body_len;
    }

    if (*frame_len < spec.min_frame_len ||
        *frame_len > spec.max_frame_len) {
        return FRAME_PROTOCOL_ERROR;
    }
    return FRAME_OK;
}

FrameResult FrameCodec::Encode(const FrameSpec& spec,
                               const std::string& payload,
                               std::string* frame) {
    if (frame == nullptr || (spec.magic_len != 0 && spec.magic == nullptr)) {
        return FRAME_PROTOCOL_ERROR;
    }
    const size_t length_size = LengthFieldSize(spec);
    const size_t header_len = spec.magic_len + length_size;
    if (payload.size() > std::numeric_limits<size_t>::max() - header_len) {
        return FRAME_PROTOCOL_ERROR;
    }
    const size_t total_len = header_len + payload.size();
    if (total_len < spec.min_frame_len || total_len > spec.max_frame_len) {
        return FRAME_PROTOCOL_ERROR;
    }
    if (spec.length_encoding == FrameSpec::FIXED &&
        spec.min_frame_len != spec.max_frame_len) {
        return FRAME_PROTOCOL_ERROR;
    }
    if (spec.length_encoding == FrameSpec::U16_TOTAL_LENGTH &&
        total_len > std::numeric_limits<uint16_t>::max()) {
        return FRAME_PROTOCOL_ERROR;
    }
    if (spec.length_encoding == FrameSpec::U32_BODY_LENGTH &&
        payload.size() > std::numeric_limits<uint32_t>::max()) {
        return FRAME_PROTOCOL_ERROR;
    }

    frame->clear();
    frame->reserve(total_len);
    if (spec.magic_len != 0) {
        frame->append(spec.magic, spec.magic_len);
    }
    if (spec.length_encoding == FrameSpec::U16_TOTAL_LENGTH) {
        const uint16_t total_be =
            butil::HostToNet16(static_cast<uint16_t>(total_len));
        frame->append(reinterpret_cast<const char*>(&total_be),
                      sizeof(total_be));
    } else if (spec.length_encoding == FrameSpec::U32_BODY_LENGTH) {
        const uint32_t body_be =
            butil::HostToNet32(static_cast<uint32_t>(payload.size()));
        frame->append(reinterpret_cast<const char*>(&body_be),
                      sizeof(body_be));
    }
    frame->append(payload);
    return FRAME_OK;
}

FrameResult FrameCodec::ReadFrame(HandshakeIO* io, const FrameSpec& spec,
                                  bool push_back_on_not_mine,
                                  std::string* payload) {
    if (io == nullptr || payload == nullptr ||
        (spec.magic_len != 0 && spec.magic == nullptr)) {
        return FRAME_PROTOCOL_ERROR;
    }

    std::string header(spec.magic_len + LengthFieldSize(spec), '\0');
    if (spec.magic_len != 0 &&
        io->ReadExact(&header[0], spec.magic_len) < 0) {
        return FRAME_IO_ERROR;
    }
    if (spec.magic_len != 0 &&
        memcmp(header.data(), spec.magic, spec.magic_len) != 0) {
        if (push_back_on_not_mine &&
            io->PushBack(header.data(), spec.magic_len) < 0) {
            return FRAME_IO_ERROR;
        }
        return FRAME_NOT_MINE;
    }

    const size_t length_size = LengthFieldSize(spec);
    if (length_size != 0 &&
        io->ReadExact(&header[spec.magic_len], length_size) < 0) {
        return FRAME_IO_ERROR;
    }
    size_t frame_len = 0;
    FrameResult result = DecodeLength(spec, header.data(), &frame_len);
    if (result != FRAME_OK) {
        return result;
    }
    const size_t body_len = frame_len - header.size();
    payload->assign(body_len, '\0');
    if (body_len != 0 && io->ReadExact(&(*payload)[0], body_len) < 0) {
        return FRAME_IO_ERROR;
    }
    return FRAME_OK;
}

FrameResult FrameCodec::ParseBufferedFrame(HandshakeInput* input,
                                           const FrameSpec& spec,
                                           std::string* payload,
                                           bool* magic_matched) {
    if (magic_matched != nullptr) {
        *magic_matched = false;
    }
    if (input == nullptr || payload == nullptr ||
        (spec.magic_len != 0 && spec.magic == nullptr)) {
        return FRAME_PROTOCOL_ERROR;
    }
    const size_t header_len = spec.magic_len + LengthFieldSize(spec);
    if (input->Size() < spec.magic_len) {
        return FRAME_NEED_MORE;
    }
    std::string header(header_len, '\0');
    if (spec.magic_len != 0 &&
        !input->CopyTo(&header[0], spec.magic_len)) {
        return FRAME_NEED_MORE;
    }
    if (spec.magic_len != 0 &&
        memcmp(header.data(), spec.magic, spec.magic_len) != 0) {
        return FRAME_NOT_MINE;
    }
    if (magic_matched != nullptr) {
        *magic_matched = true;
    }
    if (input->Size() < header_len ||
        (header_len != 0 && !input->CopyTo(&header[0], header_len))) {
        return FRAME_NEED_MORE;
    }
    size_t frame_len = 0;
    FrameResult result = DecodeLength(spec, header.data(), &frame_len);
    if (result != FRAME_OK) {
        return result;
    }
    if (input->Size() < frame_len) {
        return FRAME_NEED_MORE;
    }

    std::string frame(frame_len, '\0');
    if (!input->CopyTo(&frame[0], frame_len)) {
        return FRAME_NEED_MORE;
    }
    if (!input->Consume(frame_len)) {
        return FRAME_PROTOCOL_ERROR;
    }
    payload->assign(frame.data() + header_len, frame_len - header_len);
    return FRAME_OK;
}

FrameResult FrameCodec::WriteFrame(HandshakeIO* io, const FrameSpec& spec,
                                   const std::string& payload) {
    if (io == nullptr) {
        return FRAME_PROTOCOL_ERROR;
    }
    std::string frame;
    const FrameResult result = Encode(spec, payload, &frame);
    if (result != FRAME_OK) {
        return result;
    }
    return io->WriteAll(frame.data(), frame.size()) == 0
        ? FRAME_OK : FRAME_IO_ERROR;
}

FrameResult FrameCodec::DrainFrame(HandshakeIO* io, const FrameSpec& spec) {
    std::string ignored;
    return ReadFrame(io, spec, false, &ignored);
}

}  // namespace handshake
}  // namespace brpc
