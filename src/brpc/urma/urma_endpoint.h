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
#include <memory>
#include <ostream>
#include <vector>

#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/synchronization/lock.h"
#include "bthread/mutex.h"
#include "bthread/types.h"

#include "brpc/socket.h"

#if BRPC_WITH_URMA

#include "urma_api.h"
#include "urma_types.h"

namespace brpc {

class UrmaTransport;

namespace urma {

class UrmaStreamTrace;
// Connection parameters consumed by URMA resource setup.
struct UrmaConnectionInfo {
    uint32_t buffer_size = 0;      // peer recv buffer size in bytes (per WR)
    uint32_t recv_buffer_cnt = 0;  // peer recv buffer count (RQ depth - 1)
    uint32_t jetty_id = 0;         // peer jetty id
    uint8_t eid[16] = {0};          // peer EID (network order)
    uint32_t uasid = 0;             // peer uasid
    uint8_t tp_type = 0;            // urma_tp_type_t: URMA_RTP / URMA_CTP / URMA_UTP

    // Flattened peer buffer-pool segment.
    uint8_t seg_eid[16] = {0};      // EID owning the peer segment
    uint32_t seg_uasid = 0;         // uasid owning the peer segment
    uint64_t seg_va = 0;            // segment virtual address
    uint64_t seg_len = 0;          // segment length in bytes
    uint32_t seg_token_id = 0;      // segment token id
};

DECLARE_bool(urma_use_polling);
DECLARE_int32(urma_poller_num);
DECLARE_bool(urma_disable_bthread);

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

// Per-Socket URMA resources, send/recv windows and registered buffers.
// AdapterTransport drives the connection handshake. Cache-line aligned.
class BAIDU_CACHELINE_ALIGNMENT UrmaEndpoint : public SocketUser {
    friend class Socket;
    friend class ::brpc::UrmaTransport;

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

    // CQ completion poller: the edge-triggered callback for the CQ socket
    // (whose user owns a CQ lifetime context). Drains urma_poll_jfc and feeds the
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

    // Read local connection parameters independently of the wire version.
    void GetLocalConnectionInfo(UrmaConnectionInfo* out) const;

    // Apply the peer's negotiated parameters (window sizes, peer jetty/seg).
    void ApplyRemoteInfo(const UrmaConnectionInfo& remote);

private:
    enum State {
        UNINIT = 0x0,
        ESTABLISHED   = 0x100,
        FALLBACK_TCP  = 0x200,
        FAILED        = 0x300
    };

    // Allocate / deallocate the per-connection URMA resources (JFCE/JFC/JFR/
    // Jetty, plus the CQ socket). Returns 0 on success.
    int AllocateResources();
    // Register CQ callbacks only after the TCP handshake parser has returned.
    int StartCqEvents();
    void DeallocateResources();

    // Import the peer's jetty and buffer-pool segment. CRITICAL:
    // urma_import_seg is called BEFORE urma_import_jetty, otherwise the kernel
    // does not establish the transport-path routing for the remote EID and
    // the first SEND is rejected with URMA_CR_RNR_RETRY_CNT_EXC_ERR.
    // Returns 0 on success.
    int ImportPeer(const UrmaConnectionInfo& peer);

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
    // the URMA input processor, repost the recv WR, and ack. Returns bytes
    // received
    // (0 for send completions), or -1 on error (errno set).
    ssize_t HandleCompletion(const urma_cr_t& cr);
    ssize_t HandleRecvCompletion(const urma_cr_t& cr);

    // Queue received bytes for InputMessenger after ESTABLISHED. Hardware can
    // complete receives during the handshake, but StartCqEvents defers their
    // processing until the TCP parser has returned.
    void DispatchReceivedBytes(SocketUniquePtr& s, ssize_t bytes);
    void DumpStreamTrace(const char* reason, const std::string& prefix = "",
                         size_t buffered = 0);

    // Consume one async event from the JFCE fd (event mode only). The caller
    // drains completions before acknowledge/rearm because the bonding provider
    // uses poll_jfc to record which physical JFCs must be rearmed.
    int WaitCqEvent(SocketUniquePtr& s, urma_jfc_t** event_jfc);

    // Request completion notification (event mode only).
    int ReqNotifyCq();

    // Add/remove the CQ socket id to/from the poller (polling mode only).
    void PollerAddCqSid();
    void PollerRemoveCqSid();

    std::string GetStateStr() const;

    // Not owned.
    Socket* _socket;

    // Data-path availability.
    butil::atomic<State> _state{UNINIT};
    butil::atomic<int64_t> _pending_received_bytes{0};
    butil::Mutex _dispatch_mutex;

    // The URMA resources (jetty / jfc / jfr / jfce / imported peer objects).
    UrmaResource* _resource{nullptr};
    std::unique_ptr<UrmaStreamTrace> _stream_trace;

    struct CqLifetime {
        explicit CqLifetime(SocketId id) : socket_id(id) {}
        const SocketId socket_id;
        bthread::Mutex mutex;  // Serializes CQ dispatch, start and teardown.
        uint64_t generation{0};
        bool closed{true};
    };
    struct CqSocketUser : SocketUser {
        CqSocketUser(const std::shared_ptr<CqLifetime>& lifetime,
                     UrmaEndpoint* ep, bool use_polling)
            : lifetime(lifetime), endpoint(ep),
              generation(lifetime->generation), polling(use_polling) {}
        void BeforeRecycle(Socket*) override { delete this; }
        const std::shared_ptr<CqLifetime> lifetime;
        UrmaEndpoint* const endpoint;  // Use only with main Socket pinned + mutex.
        const uint64_t generation;
        const bool polling;
    };
    // Shared with old carriers so queued callbacks can reject a closed/reset
    // generation without dereferencing an endpoint that may already be gone.
    const std::shared_ptr<CqLifetime> _cq_lifetime;
    // Wraps the JFCE fd (event mode) or a synthetic carrier (polling mode).
    SocketId _cq_sid{INVALID_SOCKET_ID};

    // ---- Send / recv window bookkeeping ----
    uint16_t _sq_size{0};   // local JFS depth
    uint16_t _rq_size{0};   // local JFR depth

    // The input stream carried by the URMA jetty.
    InputMessengerProcessor _input_processor;

    // Per-WR send buffers (own the IOBuf until the SEND completes).
    std::vector<butil::IOBuf> _sbuf;
    // Per-WR recv buffers (zero-copy targets).
    std::vector<butil::IOBuf> _rbuf;
    std::vector<void*> _rbuf_data;
    // A provider may report completions in a different order from posted
    // receive WRs. Keep each completion with its owning slot and consume the
    // stream only once all preceding receive WRs have completed.
    std::vector<urma_cr_t> _recv_completions;
    std::vector<bool> _recv_completed;
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
