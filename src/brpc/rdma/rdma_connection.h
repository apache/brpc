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

#ifndef BRPC_RDMA_RDMA_CONNECTION_H
#define BRPC_RDMA_RDMA_CONNECTION_H
#if BRPC_WITH_RDMA
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <ostream>
#include <infiniband/verbs.h>
#include "butil/containers/optional.h"
#include "butil/iobuf.h"
#include "bthread/types.h"

namespace brpc {
class Socket;

namespace rdma {
extern const size_t IOBUF_BLOCK_HEADER_LEN;
extern const uint16_t MIN_QP_SIZE;
extern const uint16_t MAX_QP_SIZE;
extern uint32_t g_rdma_recv_block_size;

struct ParsedHello;

enum class RdmaQueueMode { PRIVATE_RQ, SHARED_RQ };

// Value snapshot for hello encoding. Contains no borrowed verbs pointers.
struct RdmaLocalInfo {
    // Maximum payload bytes accepted by one receive WR.
    uint32_t block_size{0};
    // Effective local send queue depth advertised to the peer.
    uint16_t sq_size{0};
    // Private receive queue depth. Zero in shared receive mode.
    uint16_t rq_size{0};
    // Local QP number. Zero when no usable QP is attached.
    uint32_t qp_num{0};
    // Wire protocol selector: zero for private RQ, one for SRQ flow control.
    uint32_t srq_protocol{0};
    // Initial/restored peer data-send window, advertised only in SRQ mode.
    uint32_t fc_window{0};
};

// A selected RDMA data path. The endpoint owns a reference while attached.
// Implementation-specific completion tasks may retain it after Close().
class RdmaConnection {
public:
    virtual ~RdmaConnection() = default;
    virtual void GetLocalInfo(RdmaLocalInfo* info) const = 0;
    virtual int QueryEce(ibv_ece* ece) = 0;
    virtual int Activate(const ParsedHello& peer, bool is_server,
                         butil::optional<ibv_ece>* outgoing_ece) = 0;
    // Enable parser delivery only after the TCP handshake parser has exited.
    virtual int EnableReceive() = 0;
    virtual ssize_t Send(butil::IOBuf** data, size_t count) = 0;
    virtual bool IsWritable() = 0;
    // Idempotent. Stops new work. Final DMA retirement may be asynchronous.
    virtual void Close(int error = 0) = 0;
    virtual void Debug(std::ostream& out, butil::StringPiece connector = "\n") const = 0;
};

RdmaLocalInfo DefaultRdmaLocalInfo(RdmaQueueMode mode);
std::shared_ptr<RdmaConnection>
CreateRdmaConnection(Socket* socket, RdmaQueueMode mode, bthread_tag_t tag);

// Shared RC setup helpers. Callers own the QP and serialize its lifecycle.
int InitializeRcQp(ibv_qp* qp, const ParsedHello& peer, bool* use_ece);

// The last activation stage reached successfully, not the current hardware QP
// state. Retained after close to determine the required teardown barriers.
enum class QpActivationStage : uint8_t {
    NONE, // Never reached RTR. Close need not wait for LAST_WQE.
    RTR, // Receive-capable. Must drain safely even if RTS fails.
    RTS, // Fully activated. Can send until closed.
};

// Returns the activation stage reached. errno preserves the verbs error if RTS
// was not reached.
QpActivationStage ConnectRcQp(ibv_qp* qp, const ParsedHello& peer,
                             bool is_server, uint8_t min_rnr_timer,
                             uint8_t rnr_retry);
void QueryNegotiatedEce(ibv_qp* qp, const ParsedHello& peer,
                        bool is_server, bool use_ece,
                        butil::optional<ibv_ece>* outgoing_ece);

// Registered-memory SGE conversion shared by both data paths.
ssize_t CutRdmaSges(butil::IOBuf* from, ibv_sge* sges, size_t* index,
                   butil::IOBuf* keepalive, size_t max_sge, size_t max_bytes,
                   bool* in_pool, bool* has_user_data = nullptr);
bool ShouldPostInline(bool in_pool, size_t length, uint32_t inline_size);
}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
#endif  // BRPC_RDMA_RDMA_CONNECTION_H
