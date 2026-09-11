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

#ifndef BRPC_HANDSHAKE_RDMA_HANDSHAKE_H
#define BRPC_HANDSHAKE_RDMA_HANDSHAKE_H

#include "brpc/handshake/handshake_adapter.h"

namespace brpc {
namespace handshake {

// Returns the RDMA adapter used by policy::ParseTransportHandshake. The
// concrete type is private to the implementation; callers only learn the
// common HandshakeAdapter interface.
HandshakeAdapter* GetRdmaServerHandshakeAdapter();

}  // namespace handshake
}  // namespace brpc

#if BRPC_WITH_RDMA

#include <memory>
#include <string>
#include <vector>

#include <infiniband/verbs.h>

#include "butil/containers/optional.h"
#include "butil/macros.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/handshake/rdma_handshake_constants.h"
#include "brpc/transport_handshake.h"

namespace brpc {
namespace rdma {

using ParsedHello = RdmaConnectionInfo;

namespace v2_wire {

struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(const void* data);

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint32_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    uint16_t lid;
    ibv_gid gid;
    uint32_t qp_num;
};

}  // namespace v2_wire

// RDMA adapters implement only protocol fields. HandshakeSession owns frame
// I/O, length validation, ACK exchange and resource callback ordering.
class RdmaHandshakeAdapter {
public:
    RdmaHandshakeAdapter(RdmaEndpoint* ep, int version)
        : _ep(ep), _version(version) {}
    virtual ~RdmaHandshakeAdapter() = default;

    int ProtocolVersion() const { return _version; }
    handshake::HandshakeCodec MakeCodec(ParsedHello* remote);

    virtual const handshake::FrameSpec& HelloFrameSpec() const = 0;
    virtual handshake::StepResult BuildLocalHello(
        bool enabled, std::string* payload) = 0;
    virtual handshake::StepResult ParseRemoteHello(
        const std::string& payload, ParsedHello* remote) = 0;

protected:
    void FillLocalHello(ParsedHello* local) const;
    void PrepareClientEce();

    RdmaEndpoint* _ep;
    int _version;

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaHandshakeAdapter);
};

class RdmaClientHandshakeAdapterV2 : public RdmaHandshakeAdapter {
public:
    explicit RdmaClientHandshakeAdapterV2(RdmaEndpoint* ep)
        : RdmaHandshakeAdapter(ep, 2) {}
    const handshake::FrameSpec& HelloFrameSpec() const override;
    handshake::StepResult BuildLocalHello(
        bool enabled, std::string* payload) override;
    handshake::StepResult ParseRemoteHello(
        const std::string& payload, ParsedHello* remote) override;
};

class RdmaServerHandshakeAdapterV2 : public RdmaHandshakeAdapter {
public:
    explicit RdmaServerHandshakeAdapterV2(RdmaEndpoint* ep)
        : RdmaHandshakeAdapter(ep, 2) {}
    const handshake::FrameSpec& HelloFrameSpec() const override;
    handshake::StepResult BuildLocalHello(
        bool enabled, std::string* payload) override;
    handshake::StepResult ParseRemoteHello(
        const std::string& payload, ParsedHello* remote) override;
};

class RdmaClientHandshakeAdapterV3 : public RdmaHandshakeAdapter {
public:
    explicit RdmaClientHandshakeAdapterV3(RdmaEndpoint* ep)
        : RdmaHandshakeAdapter(ep, 3) {}
    const handshake::FrameSpec& HelloFrameSpec() const override;
    handshake::StepResult BuildLocalHello(
        bool enabled, std::string* payload) override;
    handshake::StepResult ParseRemoteHello(
        const std::string& payload, ParsedHello* remote) override;
};

class RdmaServerHandshakeAdapterV3 : public RdmaHandshakeAdapter {
public:
    explicit RdmaServerHandshakeAdapterV3(RdmaEndpoint* ep)
        : RdmaHandshakeAdapter(ep, 3) {}
    const handshake::FrameSpec& HelloFrameSpec() const override;
    handshake::StepResult BuildLocalHello(
        bool enabled, std::string* payload) override;
    handshake::StepResult ParseRemoteHello(
        const std::string& payload, ParsedHello* remote) override;
};

std::unique_ptr<RdmaHandshakeAdapter> CreateClientHandshakeAdapter(
    RdmaEndpoint* ep);

std::vector<std::unique_ptr<RdmaHandshakeAdapter> >
CreateServerHandshakeAdapters(RdmaEndpoint* ep);

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA
#endif  // BRPC_HANDSHAKE_RDMA_HANDSHAKE_H
