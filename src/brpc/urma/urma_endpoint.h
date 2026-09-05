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

#ifndef BRPC_URMA_ENDPOINT_H
#define BRPC_URMA_ENDPOINT_H

#include <cstdint>
#include <functional>
#include <ostream>
#include <vector>

#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/synchronization/lock.h"
#include "bthread/types.h"

#include "brpc/socket.h"

#if BRPC_WITH_URMA

#include "urma_api.h"
#include "urma_types.h"
#include "brpc/urma/urma_handshake.h"
#include "brpc/urma/urma_handshake.pb.h"

namespace brpc {

class UrmaTransport;

namespace urma {

class UrmaHandshake;
struct ParsedHello;

DECLARE_bool(urma_use_polling);
DECLARE_int32(urma_poller_num);
DECLARE_bool(urma_disable_bthread);

// Per-connection application-level connect object. Returned by
// UrmaTransport::Connect(); its StartConnect spawns the client-side handshake
// bthread that drives the URMA negotiation over the already-connected TCP fd.
class UrmaConnect : public AppConnect {
    friend class UrmaEndpoint;
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

// POD holder for the URMA kernel objects backing one connection:
//   - the send jetty (JFS), shared-JFR jetty, JFR, JFC, and (event mode) JFCE
//   - the imported peer jetty and peer segment
// Mirrors the role of rdma::RdmaResource, but the URMA object graph is a
// little different (no separate QP/CQ split).
struct UrmaResource {
    UrmaResource* next{nullptr};  // singly-linked list for the prepared pool

    urma_jfc_t* jfc{nullptr};
    urma_jfce_t* jfce{nullptr};   // event mode only; null in polling mode
    urma_jfr_t* jfr{nullptr};
    urma_jetty_t* jetty{nullptr};
    // Imported peer objects (created per-connection, not pooled).
    urma_target_jetty_t* remote_jetty{nullptr};
    urma_target_seg_t* remote_seg{nullptr};

    UrmaResource() = default;
    ~UrmaResource();
    DISALLOW_COPY_AND_ASSIGN(UrmaResource);
};

// One per Socket. Carries the handshake state machine, the send/recv
// windows, and the registered buffer bookkeeping. Cache-line padded to avoid
// false sharing between connections.
class BAIDU_CACHELINE_ALIGNMENT UrmaEndpoint : public SocketUser {
    friend class UrmaConnect;
    friend class Socket;
    friend class UrmaTransport;
    friend class UrmaHandshakeClientV2;
    friend class UrmaHandshakeServerV2;
    friend class UrmaHandshakeClientV3;
    friend class UrmaHandshakeServerV3;
    friend int DrainBytes(UrmaEndpoint*, size_t);
    friend int ReadBodyAndNegotiate(UrmaEndpoint*, ParsedHello*, bool*);

public:
    explicit UrmaEndpoint(Socket* s);
    ~UrmaEndpoint() override;

    // ---- Global initialization / release ----
    // Pre-allocate the prepared Jetty pool. Called once at GlobalUrmaInitializeOrDie.
    static int GlobalInitialize();
    static void GlobalRelease();

    // ---- Per-connection lifecycle ----
    // Reset the endpoint back to UNINIT for reuse.
    void Reset();

    // ---- Data path (called by UrmaTransport) ----
    // Cut data from the IOBuf list and post it via URMA SEND.
    // Returns bytes sent, or -1 (errno set; EAGAIN when windows are full).
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can post more sends (both windows non-empty).
    bool IsWritable() const;

    // For debug: dump endpoint state to @os.
    void DebugInfo(std::ostream& os,
                   butil::StringPiece connector = "\n") const;

    // Edge-triggered callback installed on the TCP socket when the transport
    // is URMA. Dispatches on _state (see the .cpp for the full state machine):
    //   UNINIT (server)  -> start the server handshake bthread
    //   < ESTABLISHED    -> wake the handshake bthread parked in ReadFromFd
    //   FALLBACK_TCP     -> hand off to InputMessenger::OnNewMessages
    //   ESTABLISHED      -> drain stray TCP bytes
    static void OnNewDataFromTcp(Socket* m);

    // CQ completion poller: the edge-triggered callback for the CQ socket
    // (whose user is this endpoint). Drains urma_poll_jfc and feeds the
    // completions into HandleCompletion, then calls InputMessenger. Event mode
    // also drains the aggregated JFCE until urma_wait_jfc reports no event,
    // then calls Socket::MoreReadEvents so later JFCE edges can start the
    // callback again.
    static void PollCq(Socket* m);

    // Initialize the per-tag polling infrastructure (polling mode).
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void()> callback,
                                     std::function<void()> init_fn,
                                     std::function<void()> release_fn);
    static void PollingModeRelease(bthread_tag_t tag);

    // ---- Handshake IO helpers (also used by urma_handshake.cpp) ----
    // Read at most @len bytes from the TCP fd into @data; waits on _read_butex
    // on EAGAIN. Returns 0 on success, -1 on IO error (errno set).
    int ReadFromFd(void* data, size_t len);
    // Push @len bytes back into _socket->_read_buf so the TCP input messenger
    // can re-parse them (used on fallback when the magic is not "URMA").
    void PushBackToReadBuf(const void* data, size_t len);
    // Write at most @len bytes from @data to the TCP fd; waits on
    // _epollout_butex on EAGAIN. Returns 0 on success, -1 on IO error.
    int WriteToFd(void* data, size_t len);
    // Write an IOBuf to the TCP fd (used by v3 protobuf handshake).
    int WriteToFd(butil::IOBuf& data);

    // ---- Hello builders/parsers (used by urma_handshake.cpp) ----
    // Fill the v2 binary HelloMessage with this endpoint's local params.
    void FillLocalHelloV2(v2_wire::HelloMessage* out) const;
    // Fill the v3 protobuf UrmaHello with this endpoint's local params.
    void FillLocalHelloV3(UrmaHello* out) const;
    // Write "URM3" + 4B big-endian pb_size + protobuf bytes.
    int WriteHelloV3(const UrmaHello& msg);
    // Read pb_size (4B) + protobuf bytes; parse; validate. Sets *negotiated
    // to false (returns 0) if the message is invalid.
    int ReadAndParseHelloV3(ParsedHello* out, bool* negotiated);

    // Apply the peer's negotiated parameters (window sizes, peer jetty/seg).
    void ApplyRemoteHello(const ParsedHello& remote);

private:
    enum State {
        UNINIT = 0x0,
        C_ALLOC_RES   = 0x1,  // client: allocate Jetty/JFC/JFR
        C_HELLO_SEND  = 0x2,
        C_HELLO_WAIT  = 0x3,
        C_IMPORT_PEER = 0x4,  // urma_import_seg then urma_import_jetty
        C_ACK_SEND    = 0x5,
        S_HELLO_WAIT  = 0x11,
        S_ALLOC_RES   = 0x12,
        S_IMPORT_PEER = 0x13,
        S_HELLO_SEND  = 0x14,
        S_ACK_WAIT    = 0x15,
        ESTABLISHED   = 0x100,
        FALLBACK_TCP  = 0x200,
        FAILED        = 0x300
    };

    // Process handshake at the client / server (run in a background bthread).
    static void* ProcessHandshakeAtClient(void* arg);
    static void* ProcessHandshakeAtServer(void* arg);

    // Allocate / deallocate the per-connection URMA resources (JFCE/JFC/JFR/
    // Jetty, plus the CQ socket). Returns 0 on success.
    int AllocateResources();
    void DeallocateResources();

    // Import the peer's jetty and buffer-pool segment. CRITICAL:
    // urma_import_seg is called BEFORE urma_import_jetty, otherwise the kernel
    // does not establish the transport-path routing for the remote EID and
    // the first SEND is rejected with URMA_CR_RNR_RETRY_CNT_EXC_ERR.
    // Returns 0 on success.
    int ImportPeer(const ParsedHello& peer);

    // Post @num recv WRs into the local JFR. If @zerocopy, use a fresh pool
    // buffer; otherwise reuse the fixed _rbuf slot. Returns 0 on success.
    int PostRecv(uint32_t num, bool zerocopy);

    // Post a single recv WR pointing at @block of @block_size.
    int DoPostRecv(void* block, size_t block_size);

    // Send a pure-ACK URMA_OPC_SEND_IMM WR with no payload. @imm carries the
    // number of receive WRs reposted for peer-side flow-control credit.
    int SendImm(uint32_t imm);
    // Batched credit ack: if _new_rq_wrs is above the threshold, flush it via
    // a standalone SendImm. @num is the number of new recv WRs to add.
    int SendAck(int num);

    // Handle one completion record. For SEND completions: reclaim the SQ
    // window and wake the writer. For RECV completions: cut the payload into
    // _socket->_read_buf, repost the recv WR, and ack. Returns bytes received
    // (0 for send completions), or -1 on error (errno set).
    ssize_t HandleCompletion(const urma_cr_t& cr);

    // Queue received bytes for InputMessenger. CQ receive completions can
    // arrive while the server is still waiting for the final TCP handshake
    // ACK. Keep those bytes in _socket->_read_buf and dispatch them only after
    // ESTABLISHED, matching the connection-ready boundary seen by user code.
    void DispatchReceivedBytes(SocketUniquePtr& s, ssize_t bytes);

    // Consume one async event from the JFCE fd (event mode only). The caller
    // drains completions before acknowledge/rearm because the bonding provider
    // uses poll_jfc to record which physical JFCs must be rearmed.
    int WaitCqEvent(SocketUniquePtr& s, urma_jfc_t** event_jfc);

    // Request completion notification (event mode only).
    int ReqNotifyCq();

    // Add/remove the CQ socket id to/from the poller (polling mode only).
    void PollerAddCqSid();
    void PollerRemoveCqSid();

    inline void TryReadOnTcp();
    void FallbackToTcp(UrmaTransport* transport, bool process_tcp);
    void FailHandshake(UrmaTransport* transport, int error,
                       const char* reason);

    // Construct a ParsedHello from local state (used by SendLocalHello).
    void MakeLocalParsedHello(ParsedHello* out) const;

    std::string GetStateStr() const;

    // Not owned.
    Socket* _socket;

    // Handshake state.
    butil::atomic<State> _state{UNINIT};
    int _handshake_version{0};  // 0 = unnegotiated; 2 = v2; 3 = v3
    butil::atomic<int64_t> _pending_received_bytes{0};
    butil::Mutex _dispatch_mutex;

    // The URMA resources (jetty / jfc / jfr / jfce / imported peer objects).
    UrmaResource* _resource{nullptr};

    // The SocketId wrapping the JFCE fd (event mode) or a synthetic carrier
    // (polling mode). PollCq is the edge-triggered callback on this socket.
    SocketId _cq_sid{INVALID_SOCKET_ID};

    // ---- Send / recv window bookkeeping ----
    uint16_t _sq_size{0};   // local JFS depth
    uint16_t _rq_size{0};   // local JFR depth

    // Per-WR send buffers (own the IOBuf until the SEND completes).
    std::vector<butil::IOBuf> _sbuf;
    // Per-WR recv buffers (zero-copy targets).
    std::vector<butil::IOBuf> _rbuf;
    std::vector<void*> _rbuf_data;
    // Peer's advertised recv buffer size (caps each WR's payload).
    uint32_t _remote_recv_block_size{0};

    // Flow-control windows (atomic; producer decrements, completion handler
    // increments).
    uint16_t _local_window_capacity{0};
    uint16_t _remote_window_capacity{0};
    butil::atomic<uint16_t> _remote_rq_window_size{0};  // WRs we can send
    butil::atomic<uint16_t> _sq_window_size{0};          // WRs we can post
    butil::atomic<uint16_t> _new_rq_wrs{0};              // new recv WRs (to ack)
    uint16_t _sq_imm_window_size{0};                     // budget for pure-ack WRs

    // SQ producer / consumer indices.
    uint16_t _sq_current{0};
    uint16_t _sq_sent{0};
    // RQ consumer index.
    uint16_t _rq_received{0};

    // Butex for waking the handshake bthread parked in ReadFromFd.
    butil::atomic<int>* _read_butex{nullptr};

    // Reserved WR slots for pure-ack IMM WRs.
    static constexpr uint16_t RESERVED_WR_NUM = 3;

    // Cq socket id operation (polling mode registration queue).
    struct CqSidOp {
        enum OpType { ADD, REMOVE } type;
        SocketId sid;
    };
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
        butil::MPSCQueue<CqSidOp, butil::ObjectPoolAllocator<CqSidOp>> op_queue;
        std::function<void()> callback;
        std::function<void()> init_fn;
        std::function<void()> release_fn;
    };
    struct BAIDU_CACHELINE_ALIGNMENT PollerGroup {
        PollerGroup() : pollers(FLAGS_urma_poller_num), running(false) {}
        std::vector<Poller> pollers;
        butil::atomic<bool> running;
    };
    static std::vector<PollerGroup> _poller_groups;
    bthread_tag_t _poller_tag{0};

    DISALLOW_COPY_AND_ASSIGN(UrmaEndpoint);
};

}  // namespace urma
}  // namespace brpc

#else  // BRPC_WITH_URMA

namespace brpc {
namespace urma {
class UrmaEndpoint {};
}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA

#endif  // BRPC_URMA_ENDPOINT_H
