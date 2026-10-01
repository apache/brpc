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
#include <cstring>

#include "butil/logging.h"
#include "butil/object_pool.h"
#include "butil/sys_byteorder.h"

namespace brpc {
namespace handshake {

namespace {

const size_t COMMON_ACK_SIZE = sizeof(uint32_t);
const uint32_t COMMON_ACK_OK = 0x1;

}  // namespace

StepResult HandshakeProtocol::BuildAck(bool enabled, std::string* payload) {
    CHECK(payload != NULL);
    const uint32_t flags = butil::HostToNet32(enabled ? COMMON_ACK_OK : 0);
    payload->assign(reinterpret_cast<const char*>(&flags), sizeof(flags));
    return STEP_OK;
}

StepResult HandshakeProtocol::ParseAck(const std::string& payload,
                                       bool* enabled) {
    CHECK(enabled != NULL);
    if (payload.size() != COMMON_ACK_SIZE) {
        errno = EPROTO;
        return STEP_ERROR;
    }
    uint32_t flags = 0;
    memcpy(&flags, payload.data(), sizeof(flags));
    *enabled = (butil::NetToHost32(flags) & COMMON_ACK_OK) != 0;
    return STEP_OK;
}

const FrameSpec& HandshakeProtocol::ExtensionFrameSpec() const {
    static const FrameSpec empty_spec(
        NULL, 0, 0, 0, FrameSpec::FIXED);
    return empty_spec;
}

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

static StepResult FinishWithFailure(HandshakeSession* session,
                                    HandshakeTransport* transport) {
    transport->OnFailed();
    session->MarkFailed();
    return STEP_ERROR;
}

static StepResult FinishWithFallback(HandshakeSession* session,
                                     HandshakeTransport* transport) {
    session->PublishFallback([transport]() { transport->OnFallback(); });
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

StepResult HandshakeSession::SendHello(HandshakeProtocol* protocol,
                                       bool enabled) {
    std::string payload;
    const StepResult result = protocol->BuildHello(enabled, &payload);
    if (result != STEP_OK) {
        return result;
    }
    return ConvertFrameResult(
        FrameCodec::WriteFrame(_io, protocol->HelloFrameSpec(), payload));
}

StepResult HandshakeSession::ReceiveHello(HandshakeProtocol* protocol,
                                          HandshakeInput* input,
                                          bool push_back_on_not_mine,
                                          bool* magic_matched) {
    std::string payload;
    const FrameResult frame_result = input != NULL
        ? FrameCodec::ParseBufferedFrame(
              input, protocol->HelloFrameSpec(), &payload, magic_matched)
        : FrameCodec::ReadFrame(
              _io, protocol->HelloFrameSpec(), push_back_on_not_mine,
              &payload);
    const StepResult result = ConvertFrameResult(frame_result);
    if (result != STEP_OK) {
        return result;
    }
    set_protocol_version(protocol->ProtocolVersion());
    return protocol->ParseHello(payload);
}

StepResult HandshakeSession::SendAck(HandshakeProtocol* protocol,
                                     bool enabled) {
    std::string payload;
    const StepResult result = protocol->BuildAck(enabled, &payload);
    if (result != STEP_OK) {
        return result;
    }
    return ConvertFrameResult(
        FrameCodec::WriteFrame(_io, protocol->AckFrameSpec(), payload));
}

StepResult HandshakeSession::ReceiveAck(HandshakeProtocol* protocol,
                                        HandshakeInput* input,
                                        bool* enabled) {
    std::string payload;
    const FrameResult frame_result = input != NULL
        ? FrameCodec::ParseBufferedFrame(
              input, protocol->AckFrameSpec(), &payload)
        : FrameCodec::ReadFrame(
              _io, protocol->AckFrameSpec(), false, &payload);
    const StepResult result = ConvertFrameResult(frame_result);
    if (result != STEP_OK) {
        return result;
    }
    return protocol->ParseAck(payload, enabled);
}

StepResult HandshakeSession::SendExtension(HandshakeProtocol* protocol,
                                          bool enabled) {
    std::string payload;
    const StepResult result = protocol->BuildExtension(enabled, &payload);
    if (result != STEP_OK) {
        return result;
    }
    return ConvertFrameResult(
        FrameCodec::WriteFrame(
            _io, protocol->ExtensionFrameSpec(), payload));
}

StepResult HandshakeSession::ReceiveExtension(HandshakeProtocol* protocol,
                                             HandshakeInput* input) {
    std::string payload;
    const FrameResult frame_result = input != NULL
        ? FrameCodec::ParseBufferedFrame(
              input, protocol->ExtensionFrameSpec(), &payload)
        : FrameCodec::ReadFrame(
              _io, protocol->ExtensionFrameSpec(), false, &payload);
    const StepResult result = ConvertFrameResult(frame_result);
    return result == STEP_OK ? protocol->ParseExtension(payload) : result;
}

StepResult HandshakeSession::SelectAndReceiveHello(
    const std::vector<HandshakeProtocol*>& protocols, HandshakeInput* input,
    bool push_back_on_not_mine, HandshakeProtocol** selected) {
    CHECK(!protocols.empty());
    CHECK(selected != NULL);
    if (input == NULL) {
        // A blocking byte stream cannot try a second codec after consuming
        // bytes from the fd. Such protocols must select a single codec before
        // entering the common session.
        CHECK_EQ(1UL, protocols.size());
        *selected = protocols.front();
        return ReceiveHello(*selected, NULL, push_back_on_not_mine);
    }

    bool need_more = false;
    for (size_t i = 0; i < protocols.size(); ++i) {
        bool magic_matched = false;
        const StepResult result = ReceiveHello(
            protocols[i], input, false, &magic_matched);
        if (result == STEP_NOT_MINE) {
            continue;
        }
        if (result == STEP_NEED_MORE) {
            if (magic_matched) {
                *selected = protocols[i];
                set_protocol_version(protocols[i]->ProtocolVersion());
                return STEP_NEED_MORE;
            }
            need_more = true;
            continue;
        }
        *selected = protocols[i];
        return result;
    }
    return need_more ? STEP_NEED_MORE : STEP_NOT_MINE;
}

StepResult HandshakeSession::RunClient(HandshakeProtocol* protocol,
                                       HandshakeTransport* transport) {
    CHECK(protocol != NULL);
    CHECK(transport != NULL);
    transport->OnProtocolSelected(protocol);
    // A client handshake runs once on a potentially reused bthread. Do not
    // let an errno left by earlier work override this handshake's result.
    errno = 0;

    SetPhase(PREPARING);
    StepResult result = transport->PrepareResources();
    if (result == STEP_FALLBACK) {
        return FinishWithFallback(this, transport);
    }
    if (result != STEP_OK) {
        return FinishWithFailure(this, transport);
    }

    SetPhase(HELLO_SEND);
    if (SendHello(protocol, true) != STEP_OK) {
        return FinishWithFailure(this, transport);
    }

    SetPhase(HELLO_WAIT);
    result = ReceiveHello(protocol, NULL, false);
    if (result == STEP_NOT_MINE || result == STEP_NEED_MORE) {
        errno = EPROTO;
    }
    if (result == STEP_ERROR || result == STEP_NOT_MINE ||
        result == STEP_NEED_MORE) {
        return FinishWithFailure(this, transport);
    }
    bool enabled = result == STEP_OK;

    if (enabled && protocol->HasExtension()) {
        SetPhase(EXTENSION_SEND);
        if (SendExtension(protocol, true) != STEP_OK) {
            return FinishWithFailure(this, transport);
        }
        SetPhase(EXTENSION_WAIT);
        result = ReceiveExtension(protocol, NULL);
        if (result != STEP_OK && result != STEP_FALLBACK) {
            return FinishWithFailure(this, transport);
        }
        enabled = result == STEP_OK;
    }

    if (enabled) {
        SetPhase(NEGOTIATING);
        result = transport->NegotiateResources();
        if (result != STEP_OK && result != STEP_FALLBACK) {
            return FinishWithFailure(this, transport);
        }
        enabled = result == STEP_OK;
    }

    SetPhase(ACK_SEND);
    if (SendAck(protocol, enabled) != STEP_OK) {
        return FinishWithFailure(this, transport);
    }

    if (enabled) {
        transport->OnEstablished();
        MarkEstablished();
        return STEP_OK;
    }
    return FinishWithFallback(this, transport);
}

StepResult HandshakeSession::RunServer(
    const std::vector<HandshakeProtocol*>& protocols, HandshakeInput* input,
    HandshakeTransport* transport, bool fallback_on_not_mine) {
    CHECK(!protocols.empty());
    CHECK(transport != NULL);

    // Once TCP fallback has been published, subsequent bytes are application
    // protocol data and must bypass every upgrade codec without changing the
    // terminal state.
    if (phase() == FALLBACK_TCP) {
        return STEP_NOT_MINE;
    }

    HandshakeProtocol* selected = NULL;
    if (phase() != ACK_WAIT && phase() != EXTENSION_WAIT) {
        const int previous_phase = phase();
        _local_enabled = false;
        SetPhase(HELLO_WAIT);
        StepResult result = SelectAndReceiveHello(
            protocols, input, fallback_on_not_mine, &selected);
        if (result == STEP_NOT_MINE) {
            if (fallback_on_not_mine) {
                return FinishWithFallback(this, transport);
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
            return FinishWithFailure(this, transport);
        }
        CHECK(selected != NULL);
        transport->OnProtocolSelected(selected);
        if (result == STEP_FALLBACK) {
            // Publish the disabled transport state immediately. The server
            // still sends a disabled hello and consumes the peer ACK before
            // publishing the terminal FALLBACK_TCP phase.
            transport->OnFallback();
        }
        bool enabled = result == STEP_OK;

        if (enabled) {
            SetPhase(PREPARING);
            result = transport->PrepareResources();
            if (result != STEP_OK && result != STEP_FALLBACK) {
                return FinishWithFailure(this, transport);
            }
            enabled = result == STEP_OK;
        }

        if (enabled) {
            SetPhase(NEGOTIATING);
            result = transport->NegotiateResources();
            if (result != STEP_OK && result != STEP_FALLBACK) {
                return FinishWithFailure(this, transport);
            }
            enabled = result == STEP_OK;
        }

        SetPhase(HELLO_SEND);
        _local_enabled = enabled;
        if (SendHello(selected, enabled) != STEP_OK) {
            return FinishWithFailure(this, transport);
        }
        SetPhase(enabled && selected->HasExtension()
                     ? EXTENSION_WAIT
                     : ACK_WAIT);
    } else {
        for (size_t i = 0; i < protocols.size(); ++i) {
            if (protocols[i]->ProtocolVersion() == protocol_version()) {
                selected = protocols[i];
                break;
            }
        }
        CHECK(selected != NULL);
    }

    if (phase() == EXTENSION_WAIT) {
        StepResult result = ReceiveExtension(selected, input);
        if (result == STEP_NEED_MORE) {
            return STEP_NEED_MORE;
        }
        if (result != STEP_OK && result != STEP_FALLBACK) {
            return FinishWithFailure(this, transport);
        }
        if (result == STEP_FALLBACK) {
            _local_enabled = false;
        }
        SetPhase(EXTENSION_SEND);
        if (SendExtension(selected, _local_enabled) != STEP_OK) {
            return FinishWithFailure(this, transport);
        }
        SetPhase(ACK_WAIT);
    }

    // Always try the ACK callback once. For a non-blocking server it returns
    // STEP_NEED_MORE when the ACK has not arrived; when Hello and ACK are
    // coalesced in the input buffer this consumes the ACK without waiting for
    // another socket edge.
    bool peer_enabled = false;
    StepResult result = ReceiveAck(selected, input, &peer_enabled);
    if (result == STEP_NEED_MORE) {
        return STEP_NEED_MORE;
    }
    if (result == STEP_ERROR || result == STEP_NOT_MINE) {
        return FinishWithFailure(this, transport);
    }
    if (result == STEP_FALLBACK) {
        return FinishWithFallback(this, transport);
    }
    if (!peer_enabled) {
        return FinishWithFallback(this, transport);
    }
    if (!_local_enabled) {
        errno = EPROTO;
        return FinishWithFailure(this, transport);
    }
    if (transport->ValidateEstablished() != STEP_OK) {
        return FinishWithFailure(this, transport);
    }

    transport->OnEstablished();
    MarkEstablished();
    return STEP_OK;
}

}  // namespace handshake
}  // namespace brpc
