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

#include "brpc/transport_handshake.h"

#include <errno.h>

#include "butil/logging.h"
#include "butil/object_pool.h"

namespace brpc {
namespace handshake {

ServerHandshakeContext* ServerHandshakeContext::Create(
    HandshakeAdapter* adapter) {
    ServerHandshakeContext* context =
        butil::get_object<ServerHandshakeContext>();
    if (context != NULL) {
        context->_adapter = adapter;
    }
    return context;
}

void ServerHandshakeContext::Destroy() {
    _adapter = NULL;
    butil::return_object(this);
}

static StepResult FinishWithFailure(
    HandshakeSession* session, const std::function<void()>& on_failed) {
    if (on_failed) {
        on_failed();
    }
    session->MarkFailed();
    return STEP_ERROR;
}

static StepResult FinishWithFallback(
    HandshakeSession* session, const std::function<void()>& set_tcp_active) {
    session->PublishFallback([&set_tcp_active]() {
        if (set_tcp_active) {
            set_tcp_active();
        }
    });
    return STEP_FALLBACK;
}

static StepResult ConvertFrameResult(FrameResult result) {
    switch (result) {
    case FRAME_OK: return STEP_OK;
    case FRAME_NOT_MINE: return STEP_NOT_MINE;
    case FRAME_NEED_MORE: return STEP_NEED_MORE;
    case FRAME_IO_ERROR: return STEP_ERROR;
    case FRAME_PROTOCOL_ERROR:
        errno = EPROTO;
        return STEP_ERROR;
    }
    errno = EPROTO;
    return STEP_ERROR;
}

StepResult HandshakeSession::SendHello(const HandshakeCodec& codec,
                                       bool enabled) {
    CHECK(codec.build_hello);
    std::string payload;
    const StepResult result = codec.build_hello(enabled, &payload);
    if (result != STEP_OK) {
        return result;
    }
    return ConvertFrameResult(
        FrameCodec::WriteFrame(_io, codec.hello_frame, payload));
}

StepResult HandshakeSession::ReceiveHello(const HandshakeCodec& codec,
                                          HandshakeInput* input,
                                          bool push_back_on_not_mine,
                                          bool* magic_matched) {
    CHECK(codec.parse_hello);
    std::string payload;
    const FrameResult frame_result = input != NULL
        ? FrameCodec::ParseBufferedFrame(
              input, codec.hello_frame, &payload, magic_matched)
        : FrameCodec::ReadFrame(
              _io, codec.hello_frame, push_back_on_not_mine, &payload);
    const StepResult result = ConvertFrameResult(frame_result);
    if (result != STEP_OK) {
        return result;
    }
    set_protocol_version(codec.protocol_version);
    return codec.parse_hello(payload);
}

StepResult HandshakeSession::SendAck(const HandshakeCodec& codec,
                                     bool enabled) {
    CHECK(codec.build_ack);
    std::string payload;
    const StepResult result = codec.build_ack(enabled, &payload);
    if (result != STEP_OK) {
        return result;
    }
    return ConvertFrameResult(
        FrameCodec::WriteFrame(_io, codec.ack_frame, payload));
}

StepResult HandshakeSession::ReceiveAck(const HandshakeCodec& codec,
                                        HandshakeInput* input,
                                        bool* enabled) {
    CHECK(codec.parse_ack);
    std::string payload;
    const FrameResult frame_result = input != NULL
        ? FrameCodec::ParseBufferedFrame(input, codec.ack_frame, &payload)
        : FrameCodec::ReadFrame(_io, codec.ack_frame, false, &payload);
    const StepResult result = ConvertFrameResult(frame_result);
    if (result != STEP_OK) {
        return result;
    }
    return codec.parse_ack(payload, enabled);
}

StepResult HandshakeSession::SelectAndReceiveHello(
    const std::vector<HandshakeCodec>& codecs, HandshakeInput* input,
    bool push_back_on_not_mine, const HandshakeCodec** selected) {
    CHECK(!codecs.empty());
    CHECK(selected != NULL);
    if (input == NULL) {
        // A blocking byte stream cannot try a second codec after consuming
        // bytes from the fd. Such protocols must select a single codec before
        // entering the common session.
        CHECK_EQ(1UL, codecs.size());
        *selected = &codecs.front();
        return ReceiveHello(**selected, NULL, push_back_on_not_mine);
    }

    bool need_more = false;
    for (size_t i = 0; i < codecs.size(); ++i) {
        bool magic_matched = false;
        const StepResult result = ReceiveHello(
            codecs[i], input, false, &magic_matched);
        if (result == STEP_NOT_MINE) {
            continue;
        }
        if (result == STEP_NEED_MORE) {
            if (magic_matched) {
                *selected = &codecs[i];
                set_protocol_version(codecs[i].protocol_version);
                return STEP_NEED_MORE;
            }
            need_more = true;
            continue;
        }
        *selected = &codecs[i];
        return result;
    }
    return need_more ? STEP_NEED_MORE : STEP_NOT_MINE;
}

StepResult HandshakeSession::RunClient(
    const ClientHandshakeCallbacks& callbacks) {
    CHECK(callbacks.transport.prepare_resources);
    CHECK(callbacks.transport.negotiate_resources);
    CHECK(callbacks.transport.set_high_speed_active);
    CHECK(callbacks.transport.set_tcp_active);
    // A client handshake runs once on a potentially reused bthread. Do not
    // let an errno left by earlier work override this handshake's result.
    errno = 0;

    SetPhase(PREPARING);
    StepResult result = callbacks.transport.prepare_resources();
    if (result == STEP_FALLBACK) {
        return FinishWithFallback(this, callbacks.transport.set_tcp_active);
    }
    if (result != STEP_OK) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }

    SetPhase(HELLO_SEND);
    if (SendHello(callbacks.codec, true) != STEP_OK) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }

    SetPhase(HELLO_WAIT);
    result = ReceiveHello(callbacks.codec, NULL, false);
    if (result == STEP_NOT_MINE || result == STEP_NEED_MORE) {
        errno = EPROTO;
    }
    if (result == STEP_ERROR || result == STEP_NOT_MINE ||
        result == STEP_NEED_MORE) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }
    bool enabled = result == STEP_OK;

    if (enabled) {
        SetPhase(NEGOTIATING);
        result = callbacks.transport.negotiate_resources();
        if (result != STEP_OK && result != STEP_FALLBACK) {
            return FinishWithFailure(this, callbacks.transport.on_failed);
        }
        enabled = result == STEP_OK;
    }

    SetPhase(ACK_SEND);
    if (SendAck(callbacks.codec, enabled) != STEP_OK) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }

    if (enabled) {
        callbacks.transport.set_high_speed_active();
        MarkEstablished();
        return STEP_OK;
    }
    return FinishWithFallback(this, callbacks.transport.set_tcp_active);
}

StepResult HandshakeSession::RunServer(
    const ServerHandshakeCallbacks& callbacks) {
    CHECK(!callbacks.codecs.empty());
    CHECK(callbacks.transport.prepare_resources);
    CHECK(callbacks.transport.negotiate_resources);
    CHECK(callbacks.transport.set_high_speed_active);
    CHECK(callbacks.transport.set_tcp_active);

    // Once TCP fallback has been published, subsequent bytes are application
    // protocol data and must bypass every upgrade codec without changing the
    // terminal state.
    if (phase() == FALLBACK_TCP) {
        return STEP_NOT_MINE;
    }

    const HandshakeCodec* selected = NULL;
    if (phase() != ACK_WAIT) {
        const int previous_phase = phase();
        _local_enabled = false;
        SetPhase(HELLO_WAIT);
        StepResult result = SelectAndReceiveHello(
            callbacks.codecs, callbacks.input,
            callbacks.fallback_on_not_mine, &selected);
        if (result == STEP_NOT_MINE) {
            if (callbacks.fallback_on_not_mine) {
                return FinishWithFallback(this, callbacks.transport.set_tcp_active);
            }
            SetPhase(UNINITIALIZED);
            return STEP_NOT_MINE;
        }
        if (result == STEP_NEED_MORE) {
            if (selected == NULL) {
                SetPhase(previous_phase);
            }
            return STEP_NEED_MORE;
        }
        if (result == STEP_ERROR) {
            return FinishWithFailure(this, callbacks.transport.on_failed);
        }
        bool enabled = result == STEP_OK;

        if (enabled) {
            SetPhase(PREPARING);
            result = callbacks.transport.prepare_resources();
            if (result != STEP_OK && result != STEP_FALLBACK) {
                return FinishWithFailure(this, callbacks.transport.on_failed);
            }
            enabled = result == STEP_OK;
        }

        if (enabled) {
            SetPhase(NEGOTIATING);
            result = callbacks.transport.negotiate_resources();
            if (result != STEP_OK && result != STEP_FALLBACK) {
                return FinishWithFailure(this, callbacks.transport.on_failed);
            }
            enabled = result == STEP_OK;
        }

        SetPhase(HELLO_SEND);
        CHECK(selected != NULL);
        _local_enabled = enabled;
        if (SendHello(*selected, enabled) != STEP_OK) {
            return FinishWithFailure(this, callbacks.transport.on_failed);
        }
        SetPhase(ACK_WAIT);
    } else {
        for (size_t i = 0; i < callbacks.codecs.size(); ++i) {
            if (callbacks.codecs[i].protocol_version == protocol_version()) {
                selected = &callbacks.codecs[i];
                break;
            }
        }
        CHECK(selected != NULL);
    }

    // Always try the ACK callback once. For a non-blocking server it returns
    // STEP_NEED_MORE when the ACK has not arrived; when Hello and ACK are
    // coalesced in the input buffer this consumes the ACK without waiting for
    // another socket edge.
    bool peer_enabled = false;
    StepResult result = ReceiveAck(
        *selected, callbacks.input, &peer_enabled);
    if (result == STEP_NEED_MORE) {
        return STEP_NEED_MORE;
    }
    if (result == STEP_ERROR || result == STEP_NOT_MINE) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }
    if (result == STEP_FALLBACK) {
        return FinishWithFallback(this, callbacks.transport.set_tcp_active);
    }
    if (!peer_enabled) {
        return FinishWithFallback(this, callbacks.transport.set_tcp_active);
    }
    if (!_local_enabled) {
        errno = EPROTO;
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }
    if (callbacks.validate_established &&
        callbacks.validate_established() != STEP_OK) {
        return FinishWithFailure(this, callbacks.transport.on_failed);
    }

    callbacks.transport.set_high_speed_active();
    MarkEstablished();
    return STEP_OK;
}

}  // namespace handshake
}  // namespace brpc
