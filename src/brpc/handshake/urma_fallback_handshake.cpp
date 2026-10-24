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

#include "brpc/handshake/urma_handshake.h"

#include <errno.h>

#include "brpc/adapter_transport.h"
#include "brpc/urma/urma_handshake.pb.h"

namespace brpc {
namespace handshake {

// This codec needs neither liburma nor its SDK headers. A TCP-only server
// still recognizes the URMA control frames and replies with a disabled Hello.
class UrmaFallbackProtocol : public FallbackHandshakeProtocol {
public:
    explicit UrmaFallbackProtocol(int version) : _version(version) {}

    int ProtocolVersion() const override { return _version; }
    bool AckDisabledHello() const override { return false; }
    const FrameSpec& HelloFrameSpec() const override {
        static const FrameSpec v2("URMA", 4, 86, 4096,
                                  FrameSpec::U16_TOTAL_LENGTH);
        static const FrameSpec v3("URM3", 4, 8, 4104,
                                  FrameSpec::U32_BODY_LENGTH);
        return _version == 3 ? v3 : v2;
    }
    const FrameSpec& AckFrameSpec() const override {
        static const FrameSpec spec(nullptr, 0, 4, 4, FrameSpec::FIXED);
        return spec;
    }
    StepResult ParseHello(const std::string& payload) override {
        if (_version == 3) {
            urma::UrmaHello hello;
            if (!hello.ParseFromString(payload) || hello.eid().size() != 16 ||
                hello.seg_eid().size() != 16) {
                errno = EPROTO;
                return STEP_ERROR;
            }
        }
        return STEP_FALLBACK;
    }
    StepResult BuildHello(bool enabled, std::string* payload) override {
        if (enabled) {
            errno = EPROTO;
            return STEP_ERROR;
        }
        if (_version == 2) {
            // FrameCodec supplies the magic and two-byte total length.
            payload->assign(80, '\0');
            return STEP_OK;
        }
        urma::UrmaHello hello;
        hello.set_buffer_size(0);
        hello.set_recv_buffer_cnt(0);
        hello.set_jetty_id(0);
        hello.set_eid(std::string(16, '\0'));
        hello.set_uasid(0);
        hello.set_tp_type(0);
        hello.set_seg_eid(std::string(16, '\0'));
        hello.set_seg_uasid(0);
        hello.set_seg_va(0);
        hello.set_seg_len(0);
        hello.set_seg_token_id(0);
        return hello.SerializeToString(payload) ? STEP_OK : STEP_ERROR;
    }

private:
    int _version;
};

StepResult RunUrmaFallbackServerHandshake(butil::IOBuf* source, Socket* socket) {
    UrmaFallbackProtocol v2(2);
    UrmaFallbackProtocol v3(3);
    std::vector<HandshakeProtocol*> protocols{&v2, &v3};
    IOBufHandshakeInput input(source);
    FallbackHandshakeTransport transport;
    return AdapterTransport::Get(socket)->handshake_session()->RunServer(
        protocols, &input, &transport, false);
}

#if !BRPC_WITH_URMA
class UrmaFallbackServerAdapter : public StandardHandshakeAdapter {
protected:
    StepResult RunServerStep(butil::IOBuf* source, Socket* socket) override {
        return RunUrmaFallbackServerHandshake(source, socket);
    }
    HandshakeSession* GetSession(Socket* socket) const override {
        return AdapterTransport::Get(socket)->handshake_session();
    }
};

HandshakeAdapter* GetUrmaServerHandshakeAdapter() {
    static UrmaFallbackServerAdapter adapter;
    return &adapter;
}
#endif

}  // namespace handshake
}  // namespace brpc
