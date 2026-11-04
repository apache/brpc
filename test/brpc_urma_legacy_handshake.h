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

#ifndef BRPC_TEST_URMA_LEGACY_HANDSHAKE_H
#define BRPC_TEST_URMA_LEGACY_HANDSHAKE_H

#include "brpc/urma_transport.h"
#include "brpc/handshake/urma_handshake.h"
#include "brpc/input_messenger.h"
#include "brpc/urma/urma_handshake.pb.h"
#include "brpc/urma/urma_helper.h"
#include "bthread/butex.h"
#include "bthread/bthread.h"
#include "butil/sys_byteorder.h"
#include "butil/time.h"

// Independent legacy v2/v3 peer used by the interoperability tests.
namespace brpc {
namespace urma {

DECLARE_bool(urma_recv_zerocopy);
DECLARE_int32(urma_client_handshake_version);
class LegacyUrmaEndpoint;

class UrmaConnect : public AppConnect {
    friend class LegacyUrmaEndpoint;
public:
    void StartConnect(const Socket* socket,
                      void (*done)(int err, void* data), void* data) override;
    void StopConnect(Socket*) override;

    struct RunGuard {
        explicit RunGuard(UrmaConnect* rc) : this_rc(rc) {}
        ~RunGuard() { if (this_rc) this_rc->Run(); }
        UrmaConnect* this_rc;
    };

private:
    void Run();
    void (*_done)(int, void*){nullptr};
    void* _data{nullptr};
    int _error{0};
};

// Legacy TCP handshake strategy used by interoperability tests.
// v2 speaks binary; v3 speaks protobuf ("URM3").
class UrmaHandshake {
public:
    virtual ~UrmaHandshake() = default;
    virtual int ProtocolVersion() const = 0;

    // Send our local hello over the TCP fd held by the endpoint.
    // Returns 0 on success, -1 on failure (errno set).
    virtual int SendLocalHello() = 0;

    // Read the peer's hello and parse it into @out. Sets @negotiated to true
    // if the peer is URMA-capable and the message validates; false (with
    // return 0) to signal a graceful fall-back to TCP.
    // Returns -1 on IO error (errno set).
    virtual int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) = 0;
};

class LegacyUrmaEndpoint : public UrmaEndpoint {
public:
    explicit LegacyUrmaEndpoint(Socket* socket) : UrmaEndpoint(socket) {
        _read_butex = bthread::butex_create_checked<butil::atomic<int>>();
        _read_butex->store(0, butil::memory_order_relaxed);
    }
    ~LegacyUrmaEndpoint() override { bthread::butex_destroy(_read_butex); }

    int _handshake_version{0};

    static constexpr State C_ALLOC_RES = static_cast<State>(0x1);
    static constexpr State C_HELLO_SEND = static_cast<State>(0x2);
    static constexpr State C_HELLO_WAIT = static_cast<State>(0x3);
    static constexpr State C_IMPORT_PEER = static_cast<State>(0x4);
    static constexpr State C_ACK_SEND = static_cast<State>(0x5);
    static constexpr State S_HELLO_WAIT = static_cast<State>(0x11);
    static constexpr State S_ALLOC_RES = static_cast<State>(0x12);
    static constexpr State S_IMPORT_PEER = static_cast<State>(0x13);
    static constexpr State S_HELLO_SEND = static_cast<State>(0x14);
    static constexpr State S_ACK_WAIT = static_cast<State>(0x15);
    static void OnNewDataFromTcp(Socket* socket);
    static void* ProcessHandshakeAtClient(void* arg);
    static void* ProcessHandshakeAtServer(void* arg);
    int ReadFromFd(void* data, size_t len);
    void PushBackToReadBuf(const void* data, size_t len);
    int WriteToFd(void* data, size_t len);
    int WriteToFd(butil::IOBuf& data);
    void FillLocalHelloV2(v2_wire::HelloMessage* out) const;
    void FillLocalHelloV3(UrmaHello* out) const;
    int WriteHelloV3(const UrmaHello& message);
    int ReadAndParseHelloV3(ParsedHello* out, bool* negotiated);
    void TryReadOnTcp();
    void FallbackToTcp(UrmaTransport* transport, bool process_tcp);
    void FailHandshake(UrmaTransport* transport, int error, const char* reason);
private:
    butil::atomic<int>* _read_butex;
};

// v2 binary handshake (magic "URMA").
class UrmaHandshakeClientV2 : public UrmaHandshake {
public:
    explicit UrmaHandshakeClientV2(LegacyUrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 2; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    LegacyUrmaEndpoint* _ep;
};

class UrmaHandshakeServerV2 : public UrmaHandshake {
public:
    explicit UrmaHandshakeServerV2(LegacyUrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 2; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    LegacyUrmaEndpoint* _ep;
};

// v3 protobuf handshake (magic "URM3").
class UrmaHandshakeClientV3 : public UrmaHandshake {
public:
    explicit UrmaHandshakeClientV3(LegacyUrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 3; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    LegacyUrmaEndpoint* _ep;
};

class UrmaHandshakeServerV3 : public UrmaHandshake {
public:
    explicit UrmaHandshakeServerV3(LegacyUrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 3; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    LegacyUrmaEndpoint* _ep;
};

// Legacy client factory: picks v2/v3 via --urma_client_handshake_version.
UrmaHandshake* CreateClientHandshake(LegacyUrmaEndpoint* ep);

// Legacy server factory: dispatches on the first 4 bytes read from the TCP fd.
// Returns nullptr if the magic is not "URMA"/"URM3" (caller falls back to TCP).
// @magic is the first MAGIC_STR_LEN bytes already read from the fd.
UrmaHandshake* CreateServerHandshakeByMagic(LegacyUrmaEndpoint* ep,
                                             const uint8_t magic[v2_wire::MAGIC_STR_LEN]);

// Drain @n bytes from the TCP fd (used when msg_len > HELLO_MSG_LEN_MIN).
int DrainBytes(LegacyUrmaEndpoint* ep, size_t n);

// Read the body following the magic and translate to ParsedHello.
// Returns -1 on IO error; 0 with *negotiated=false on invalid
// (peer not URMA-capable); 0 with *negotiated=true on success.
int ReadBodyAndNegotiate(LegacyUrmaEndpoint* ep, ParsedHello* out, bool* negotiated);

static const int WAIT_TIMEOUT_MS = 50;
static const size_t HELLO_ACK_LEN = 4;
static const uint32_t HELLO_ACK_URMA_OK = 0x1;

int LegacyUrmaEndpoint::ReadFromFd(void* data, size_t len) {
    char* p = static_cast<char*>(data);
    size_t received = 0;
    while (received < len) {
        const int expected_val = _read_butex->load(butil::memory_order_acquire);
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        const int fd = _socket->fd();
        const ssize_t nr = read(fd, p + received, len - received);
        if (nr < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                int rc = bthread::butex_wait(_read_butex, expected_val, &duetime);
                if (rc < 0 && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        if (nr == 0) {
            errno = EEOF;
            return -1;
        }
        received += nr;
    }
    return 0;
}

void LegacyUrmaEndpoint::PushBackToReadBuf(const void* data, size_t len) {
    _socket->fd_input_processor().read_buf().append(data, len);
}

int LegacyUrmaEndpoint::WriteToFd(void* data, size_t len) {
    char* p = static_cast<char*>(data);
    size_t written = 0;
    while (written < len) {
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        const int fd = _socket->fd();
        const ssize_t nw = write(fd, p + written, len - written);
        if (nw >= 0) {
            written += nw;
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        if (_socket->WaitEpollOut(fd, true, &duetime) != 0 && errno != ETIMEDOUT) {
            return -1;
        }
    }
    return 0;
}

void LegacyUrmaEndpoint::FillLocalHelloV2(v2_wire::HelloMessage* out) const {
    *out = v2_wire::HelloMessage{};  // value-initialize
    out->msg_len = v2_wire::HELLO_PACKET_LEN;
    out->hello_ver = v2_wire::HELLO_V2_VERSION;
    out->impl_ver = v2_wire::IMPL_V2_VERSION;
    ParsedHello p;
    GetLocalConnectionInfo(&p);
    out->buffer_size = p.buffer_size;
    out->recv_buffer_cnt = p.recv_buffer_cnt;
    out->jetty_id = p.jetty_id;
    std::memcpy(out->eid, p.eid, 16);
    out->uasid = p.uasid;
    out->tp_type = p.tp_type;
    std::memcpy(out->seg_eid, p.seg_eid, 16);
    out->seg_uasid = p.seg_uasid;
    out->seg_va = p.seg_va;
    out->seg_len = p.seg_len;
    out->seg_token_id = p.seg_token_id;
}

void LegacyUrmaEndpoint::FillLocalHelloV3(UrmaHello* out) const {
    ParsedHello p;
    GetLocalConnectionInfo(&p);
    out->set_buffer_size(p.buffer_size);
    out->set_recv_buffer_cnt(p.recv_buffer_cnt);
    out->set_jetty_id(p.jetty_id);
    out->set_eid(p.eid, 16);
    out->set_uasid(p.uasid);
    out->set_tp_type(p.tp_type);
    out->set_seg_eid(p.seg_eid, 16);
    out->set_seg_uasid(p.seg_uasid);
    out->set_seg_va(p.seg_va);
    out->set_seg_len(p.seg_len);
    out->set_seg_token_id(p.seg_token_id);
}

int LegacyUrmaEndpoint::WriteHelloV3(const UrmaHello& msg) {
    butil::IOBuf packet;
    packet.append("URM3", 4);
    std::string body;
    if (!msg.SerializeToString(&body)) {
        LOG(ERROR) << "Fail to serialize UrmaHello";
        return -1;
    }
    uint32_t pb_size_be = butil::HostToNet32(static_cast<uint32_t>(body.size()));
    packet.append(&pb_size_be, sizeof(pb_size_be));
    packet.append(body);
    return WriteToFd(packet);
}

int LegacyUrmaEndpoint::WriteToFd(butil::IOBuf& data) {
    // Write out the IOBuf in a single WriteToFd-style loop.
    while (!data.empty()) {
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        const int fd = _socket->fd();
        const ssize_t nw = data.cut_into_file_descriptor(fd);
        if (nw >= 0) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        if (_socket->WaitEpollOut(fd, true, &duetime) != 0 && errno != ETIMEDOUT) {
            return -1;
        }
    }
    return 0;
}

int LegacyUrmaEndpoint::ReadAndParseHelloV3(ParsedHello* out, bool* negotiated) {
    *negotiated = false;
    uint32_t pb_size_be = 0;
    if (ReadFromFd(&pb_size_be, sizeof(pb_size_be)) < 0) {
        return -1;
    }
    const uint32_t pb_size = butil::NetToHost32(pb_size_be);
    if (pb_size == 0 || pb_size > 4096) {
        return 0;
    }
    std::string body(pb_size, '\0');
    if (ReadFromFd(&body[0], pb_size) < 0) {
        return -1;
    }
    UrmaHello msg;
    if (!msg.ParseFromArray(body.data(), static_cast<int>(body.size()))) {
        return 0;
    }
    if (msg.eid().size() != 16 || msg.seg_eid().size() != 16) {
        return 0;
    }
    out->buffer_size = msg.buffer_size();
    out->recv_buffer_cnt = msg.recv_buffer_cnt();
    out->jetty_id = msg.jetty_id();
    std::memcpy(out->eid, msg.eid().data(), 16);
    out->uasid = msg.uasid();
    out->tp_type = static_cast<uint8_t>(msg.tp_type());
    std::memcpy(out->seg_eid, msg.seg_eid().data(), 16);
    out->seg_uasid = msg.seg_uasid();
    out->seg_va = msg.seg_va();
    out->seg_len = msg.seg_len();
    out->seg_token_id = msg.seg_token_id();
    if (!ValidHello(*out)) {
        return 0;
    }
    *negotiated = true;
    return 0;
}

int ReadBodyAndNegotiate(LegacyUrmaEndpoint* ep, ParsedHello* out, bool* negotiated) {
    *negotiated = false;
    uint8_t body[v2_wire::HELLO_BODY_LEN];
    if (ep->ReadFromFd(body, v2_wire::HELLO_BODY_LEN) < 0) {
        return -1;
    }
    v2_wire::HelloMessage m;
    m.Deserialize(body);
    if (m.msg_len < v2_wire::HELLO_MSG_LEN_MIN ||
        m.msg_len > v2_wire::HELLO_MSG_LEN_MAX ||
        m.hello_ver != v2_wire::HELLO_V2_VERSION ||
        m.impl_ver != v2_wire::IMPL_V2_VERSION) { return 0; }
    ParsedHello p;
    p.buffer_size = m.buffer_size;
    p.recv_buffer_cnt = m.recv_buffer_cnt;
    p.jetty_id = m.jetty_id;
    std::memcpy(p.eid, m.eid, 16);
    p.uasid = m.uasid;
    p.tp_type = m.tp_type;
    std::memcpy(p.seg_eid, m.seg_eid, 16);
    p.seg_uasid = m.seg_uasid;
    p.seg_va = m.seg_va;
    p.seg_len = m.seg_len;
    p.seg_token_id = m.seg_token_id;
    if (!ValidHello(p)) {
        return 0;
    }
    // Drain trailing bytes if msg_len advertises more than the fixed body.
    if (m.msg_len > v2_wire::HELLO_PACKET_LEN) {
        if (DrainBytes(ep, m.msg_len - v2_wire::HELLO_PACKET_LEN) < 0) {
            return -1;
        }
    }
    *out = p;
    *negotiated = true;
    return 0;
}

int DrainBytes(LegacyUrmaEndpoint* ep, size_t n) {
    char buf[4096];
    while (n > 0) {
        size_t want = std::min(n, sizeof(buf));
        if (ep->ReadFromFd(buf, want) < 0) {
            return -1;
        }
        n -= want;
    }
    return 0;
}

// ============================================================================
// v2 client / server.
// ============================================================================

int UrmaHandshakeClientV2::SendLocalHello() {
    v2_wire::HelloMessage m;
    _ep->FillLocalHelloV2(&m);
    uint8_t packet[v2_wire::HELLO_PACKET_LEN];
    std::memcpy(packet, "URMA", 4);
    m.Serialize(packet + 4);
    return _ep->WriteToFd(packet, v2_wire::HELLO_PACKET_LEN);
}

int UrmaHandshakeClientV2::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    *negotiated = false;
    uint8_t magic[v2_wire::MAGIC_STR_LEN];
    if (_ep->ReadFromFd(magic, v2_wire::MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (std::memcmp(magic, "URMA", 4) != 0) {
        // Peer is not URMA-capable; push the magic back so the TCP input
        // messenger can re-parse it.
        _ep->PushBackToReadBuf(magic, v2_wire::MAGIC_STR_LEN);
        return 0;
    }
    return ReadBodyAndNegotiate(_ep, out, negotiated);
}

int UrmaHandshakeServerV2::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                       bool* negotiated) {
    return ReadBodyAndNegotiate(_ep, out, negotiated);
}

int UrmaHandshakeServerV2::SendLocalHello() {
    v2_wire::HelloMessage m;
    _ep->FillLocalHelloV2(&m);
    auto* tp = static_cast<UrmaTransport*>(_ep->_socket->_transport.get());
    if (tp->_urma_state.load(butil::memory_order_acquire) ==
        UrmaTransport::URMA_OFF) {
        // Tell the client we are not URMA-capable: zero the version fields so
        // the client's version check fails and it falls back to TCP.
        m.hello_ver = 0;
        m.impl_ver = 0;
        m.jetty_id = 0;
        m.buffer_size = 0;
    }
    uint8_t packet[v2_wire::HELLO_PACKET_LEN];
    std::memcpy(packet, "URMA", 4);
    m.Serialize(packet + 4);
    return _ep->WriteToFd(packet, v2_wire::HELLO_PACKET_LEN);
}

// ============================================================================
// v3 protobuf ("URM3"). The protobuf (de)serialization lives on the endpoint
// because it touches UrmaEndpoint private state; the classes here just call
// FillLocalHelloV3 / WriteHelloV3 / ReadAndParseHelloV3.
// ============================================================================

int UrmaHandshakeClientV3::SendLocalHello() {
    UrmaHello msg;
    _ep->FillLocalHelloV3(&msg);
    return _ep->WriteHelloV3(msg);
}

int UrmaHandshakeClientV3::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    *negotiated = false;
    uint8_t magic[v2_wire::MAGIC_STR_LEN];
    if (_ep->ReadFromFd(magic, v2_wire::MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (std::memcmp(magic, "URM3", 4) != 0) {
        _ep->PushBackToReadBuf(magic, v2_wire::MAGIC_STR_LEN);
        return 0;
    }
    return _ep->ReadAndParseHelloV3(out, negotiated);
}

int UrmaHandshakeServerV3::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    return _ep->ReadAndParseHelloV3(out, negotiated);
}

int UrmaHandshakeServerV3::SendLocalHello() {
    UrmaHello msg;
    _ep->FillLocalHelloV3(&msg);
    // v3 has no zero-out path; the client rejects jetty_id==0 via ValidHello.
    return _ep->WriteHelloV3(msg);
}

// ============================================================================
// Factories.
// ============================================================================

UrmaHandshake* CreateClientHandshake(LegacyUrmaEndpoint* ep) {
    switch (FLAGS_urma_client_handshake_version) {
    case 3: return new UrmaHandshakeClientV3(ep);
    case 2:
    default: return new UrmaHandshakeClientV2(ep);
    }
}

UrmaHandshake* CreateServerHandshakeByMagic(LegacyUrmaEndpoint* ep,
                                             const uint8_t magic[v2_wire::MAGIC_STR_LEN]) {
    if (std::memcmp(magic, "URMA", 4) == 0) {
        return new UrmaHandshakeServerV2(ep);
    }
    if (std::memcmp(magic, "URM3", 4) == 0) {
        return new UrmaHandshakeServerV3(ep);
    }
    return nullptr;
}

static void TryReadOnTcpDuringUrmaEst(Socket* socket);

void LegacyUrmaEndpoint::OnNewDataFromTcp(Socket* m) {
    auto* tp = static_cast<UrmaTransport*>(m->_transport.get());
    if (!tp) {
        return;
    }
    // TCP fallback sockets may have no URMA endpoint.
    auto* ep = static_cast<LegacyUrmaEndpoint*>(tp->_urma_ep);
    if (!ep) {
        // No URMA endpoint: pure TCP path.
        InputMessenger::OnNewMessages(m);
        return;
    }
    int progress = 0;
    while (true) {
        const State state =
            ep->_state.load(butil::memory_order_acquire);
        if (state == UNINIT) {
            if (!m->CreatedByConnect()) {
                // Server side: kick off the handshake bthread.
                if (!IsUrmaAvailable()) {
                    ep->_state = FALLBACK_TCP;
                    tp->_urma_state = UrmaTransport::URMA_OFF;
                    InputMessenger::OnNewMessages(m);
                    return;
                }
                SocketUniquePtr s;
                m->ReAddress(&s);
                ep->_state = S_HELLO_WAIT;
                bthread_t tid;
                bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
                bthread_attr_set_name(&attr, "UrmaServerHandshake");
                if (bthread_start_background(&tid, &attr,
                        ProcessHandshakeAtServer, ep) != 0) {
                    ep->_state = UNINIT;
                    LOG(FATAL) << "Fail to start UrmaServerHandshake bthread";
                } else {
                    s.release();
                }
                return;
            }
            // Client side: handled by ProcessHandshakeAtClient.
            return;
        } else if (state < ESTABLISHED) {
            // During handshake: wake the handshake bthread parked in ReadFromFd.
            ep->_read_butex->fetch_add(1, butil::memory_order_release);
            bthread::butex_wake(ep->_read_butex);
            return;
        } else if (state == FALLBACK_TCP) {
            InputMessenger::OnNewMessages(m);
            return;
        } else if (state == ESTABLISHED) {
            TryReadOnTcpDuringUrmaEst(m);
            return;
        }
        if (!m->MoreReadEvents(&progress)) {
            break;
        }
    }
}

inline void LegacyUrmaEndpoint::TryReadOnTcp() {
    if (_state.load(butil::memory_order_acquire) == FALLBACK_TCP) {
        InputMessenger::OnNewMessages(_socket);
    }
}

static void TryReadOnTcpDuringUrmaEst(Socket* socket) {
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        uint8_t byte = 0;
        const ssize_t nr = read(socket->fd(), &byte, 1);
        if (nr < 0) {
            if (errno != EAGAIN) {
                const int saved_errno = errno;
                socket->SetFailed(saved_errno, "Fail to read URMA TCP fd: %s",
                                  berror(saved_errno));
                return;
            }
            if (!socket->MoreReadEvents(&progress)) {
                return;
            }
        } else if (nr == 0) {
            socket->SetEOF();
            return;
        } else {
            socket->SetFailed(
                EPROTO, "Unexpected TCP data after URMA was established");
            return;
        }
    }
}

void LegacyUrmaEndpoint::FallbackToTcp(UrmaTransport* transport, bool process_tcp) {
    transport->_urma_state = UrmaTransport::URMA_OFF;
    _state.store(FALLBACK_TCP, butil::memory_order_release);
    Reset();
    _state.store(FALLBACK_TCP, butil::memory_order_release);
    if (process_tcp) {
        TryReadOnTcp();
    }
}

void LegacyUrmaEndpoint::FailHandshake(UrmaTransport* transport, int error,
                                 const char* reason) {
    LOG(ERROR) << "URMA handshake failed in state=" << GetStateStr()
               << " on " << _socket->description()
               << ": " << reason << ", error=" << error
               << " (" << berror(error) << ')';
    transport->_urma_state = UrmaTransport::URMA_OFF;
    _state.store(FAILED, butil::memory_order_release);
    DeallocateResources();
    auto* connect =
        static_cast<UrmaConnect*>(_socket->_app_connect.get());
    if (connect) {
        connect->_error = error;
    }
    _socket->SetFailed(error, "URMA handshake failed: %s", reason);
}

// ============================================================================
// Handshake state machines (client / server). Run in a background bthread.
// ============================================================================

void* LegacyUrmaEndpoint::ProcessHandshakeAtClient(void* arg) {
    auto* ep = static_cast<LegacyUrmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    auto* tp = static_cast<UrmaTransport*>(s->_transport.get());
    UrmaConnect::RunGuard guard(static_cast<UrmaConnect*>(s->_app_connect.get()));
    if (!IsUrmaAvailable()) {
        ep->FallbackToTcp(tp, true);
        return nullptr;
    }
    ep->_state = C_ALLOC_RES;
    if (ep->AllocateResources() < 0) {
        ep->FallbackToTcp(tp, true);
        return nullptr;
    }
    // Prepost the shared JFR before sending the client hello so the peer sees
    // a ready receive queue as soon as its import completes.
    if (ep->PostRecv(ep->_rq_size, FLAGS_urma_recv_zerocopy) < 0) {
        ep->FallbackToTcp(tp, true);
        return nullptr;
    }
    ep->_state = C_HELLO_SEND;
    std::unique_ptr<UrmaHandshake> hs(CreateClientHandshake(ep));
    ep->_handshake_version = hs->ProtocolVersion();
    if (hs->SendLocalHello() < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "send client hello");
        return nullptr;
    }
    ep->_state = C_HELLO_WAIT;
    ParsedHello remote;
    bool negotiated = false;
    if (hs->ReceiveAndParseRemoteHello(&remote, &negotiated) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "read server hello");
        return nullptr;
    }
    if (!negotiated) {
        ep->FallbackToTcp(tp, true);
        return nullptr;
    }
    ep->ApplyRemoteInfo(remote);
    ep->_state = C_IMPORT_PEER;
    if (ep->ImportPeer(remote) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "import server resources");
        return nullptr;
    }
    ep->_state = C_ACK_SEND;
    uint32_t flags = HELLO_ACK_URMA_OK;
    uint32_t flags_be = butil::HostToNet32(flags);
    if (ep->WriteToFd(&flags_be, HELLO_ACK_LEN) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "send client ack");
        return nullptr;
    }
    tp->_urma_state = UrmaTransport::URMA_ON;
    ep->_state = ESTABLISHED;
    if (ep->StartCqEvents() < 0) {
        ep->FailHandshake(tp, errno, "start client CQ events");
        return nullptr;
    }
    return nullptr;
}

void* LegacyUrmaEndpoint::ProcessHandshakeAtServer(void* arg) {
    auto* ep = static_cast<LegacyUrmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    auto* tp = static_cast<UrmaTransport*>(s->_transport.get());
    UrmaConnect::RunGuard guard(static_cast<UrmaConnect*>(s->_app_connect.get()));
    ep->_state = S_HELLO_WAIT;
    uint8_t magic[v2_wire::MAGIC_STR_LEN];
    if (ep->ReadFromFd(magic, v2_wire::MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "read client magic");
        return nullptr;
    }
    std::unique_ptr<UrmaHandshake> hs(CreateServerHandshakeByMagic(ep, magic));
    if (!hs) {
        // Not an URMA peer: push the magic back and fall back to TCP.
        ep->PushBackToReadBuf(magic, v2_wire::MAGIC_STR_LEN);
        ep->FallbackToTcp(tp, true);
        return nullptr;
    }
    ep->_handshake_version = hs->ProtocolVersion();
    ParsedHello remote;
    bool negotiated = false;
    if (hs->ReceiveAndParseRemoteHello(&remote, &negotiated) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "read client hello");
        return nullptr;
    }
    if (!negotiated) {
        ep->FailHandshake(tp, EPROTO, "invalid client hello");
        return nullptr;
    }
    ep->_state = S_ALLOC_RES;
    if (ep->AllocateResources() < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "allocate server resources");
        return nullptr;
    }
    const bool bonding = IsUrmaBondingDevice();
    if (!bonding &&
        ep->PostRecv(ep->_rq_size, FLAGS_urma_recv_zerocopy) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "post server receives");
        return nullptr;
    }
    ep->ApplyRemoteInfo(remote);
    ep->_state = S_IMPORT_PEER;
    if (ep->ImportPeer(remote) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "import client resources");
        return nullptr;
    }
    if (bonding &&
        ep->PostRecv(ep->_rq_size, FLAGS_urma_recv_zerocopy) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "post server receives");
        return nullptr;
    }
    ep->_state = S_HELLO_SEND;
    if (hs->SendLocalHello() < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "send server hello");
        return nullptr;
    }
    ep->_state = S_ACK_WAIT;
    uint32_t flags_be = 0;
    if (ep->ReadFromFd(&flags_be, HELLO_ACK_LEN) < 0) {
        const int saved_errno = errno ? errno : EIO;
        ep->FailHandshake(tp, saved_errno, "read client ack");
        return nullptr;
    }
    uint32_t flags = butil::NetToHost32(flags_be);
    bool client_ack_ok = (flags & HELLO_ACK_URMA_OK) != 0;
    if (client_ack_ok) {
        if (tp->_urma_state.load(butil::memory_order_acquire) ==
            UrmaTransport::URMA_OFF) {
            // Protocol breakdown: client wants URMA but we already fell back.
            ep->FailHandshake(tp, EPROTO, "client ack mismatch");
            return nullptr;
        }
        tp->_urma_state = UrmaTransport::URMA_ON;
        ep->_state = ESTABLISHED;
        if (ep->StartCqEvents() < 0) {
            ep->FailHandshake(tp, errno, "start server CQ events");
            return nullptr;
        }
    } else {
        ep->FallbackToTcp(tp, true);
    }
    return nullptr;
}

// ============================================================================
// UrmaConnect: drives the client handshake bthread.
// ============================================================================

void UrmaConnect::StartConnect(const Socket* socket,
                               void (*done)(int, void*), void* data) {
    SocketUniquePtr s;
    if (Socket::Address(socket->id(), &s) != 0) {
        return;
    }
    _done = done;
    _data = data;
    _error = 0;
    auto* tp = static_cast<UrmaTransport*>(socket->_transport.get());
    if (!tp) {
        Run();
        return;
    }
    if (!tp->_urma_ep || !IsUrmaAvailable()) {
        // Fall back to TCP immediately.
        if (tp->_urma_ep) {
            tp->_urma_ep->_state = LegacyUrmaEndpoint::FALLBACK_TCP;
        }
        tp->_urma_state = UrmaTransport::URMA_OFF;
        Run();
        return;
    }
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "UrmaClientHandshake");
    if (bthread_start_background(&tid, &attr,
            LegacyUrmaEndpoint::ProcessHandshakeAtClient,
            tp->_urma_ep) != 0) {
        tp->_urma_ep->_state = LegacyUrmaEndpoint::FALLBACK_TCP;
        tp->_urma_state = UrmaTransport::URMA_OFF;
        Run();
    } else {
        // ProcessHandshakeAtClient adopts this reference in its
        // SocketUniquePtr constructor.
        s.release();
    }
}

void UrmaConnect::StopConnect(Socket*) {}

void UrmaConnect::Run() {
    if (_done) {
        auto cb = _done;
        _done = nullptr;
        cb(_error, _data);
    }
}


}  // namespace urma
}  // namespace brpc

#endif  // BRPC_TEST_URMA_LEGACY_HANDSHAKE_H
