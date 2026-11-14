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

#if BRPC_WITH_RDMA
#include "brpc/rdma/rdma_connection.h"
#include <algorithm>
#include <gflags/gflags.h>
#include "brpc/errno.pb.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rq_connection.h"
#include "brpc/rdma/srq_worker.h"
#include "brpc/socket.h"

namespace brpc {
namespace rdma {
DECLARE_int32(rdma_sq_size);
DECLARE_int32(rdma_rq_size);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryEce)(ibv_qp*, ibv_ece*);
extern int (*IbvSetEce)(ibv_qp*, ibv_ece*);
extern const size_t IOBUF_BLOCK_HEADER_LEN = 32;
extern const uint16_t MIN_QP_SIZE = 16;
extern const uint16_t MAX_QP_SIZE = 4096;
extern const uint16_t MIN_BLOCK_SIZE = 1024;
uint32_t g_rdma_recv_block_size = 0;
static const uint8_t MAX_HOP_LIMIT = 16;
static const uint8_t TIMEOUT = 14;
static const uint8_t RETRY_CNT = 7;

RdmaLocalInfo DefaultRdmaLocalInfo(RdmaQueueMode mode) {
    RdmaLocalInfo info;
    info.block_size = g_rdma_recv_block_size;
    if (mode == RdmaQueueMode::SHARED_RQ) {
        info.sq_size = FLAGS_rdma_srq_sq_size;
        info.srq_protocol = 1;
        info.fc_window = FLAGS_rdma_srq_window;
    } else {
        info.sq_size = std::max<int>(MIN_QP_SIZE,
            std::min<int>(MAX_QP_SIZE, FLAGS_rdma_sq_size));
        info.rq_size = std::max<int>(MIN_QP_SIZE,
            std::min<int>(MAX_QP_SIZE, FLAGS_rdma_rq_size));
    }
    return info;
}

std::shared_ptr<RdmaConnection> CreateRdmaConnection(Socket* socket,
                                                    RdmaQueueMode mode, bthread_tag_t tag) {
    if (mode == RdmaQueueMode::SHARED_RQ) {
        return SrqConnection::Create(socket, tag,
                                     FLAGS_rdma_srq_sq_size);
    }
    return RqConnection::Create(socket);
}

int InitializeRcQp(ibv_qp* qp, const ParsedHello& peer, bool* use_ece) {
    ibv_qp_attr attr = {};

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;  // TODO: support more pkey use in future
    attr.port_num = GetRdmaPortNum();
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
    int err = IbvModifyQp(qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_PKEY_INDEX |
                IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS));
    if (err != 0) {
        errno = err;
        LOG(WARNING) << "Fail to modify QP from RESET to INIT: " << berror(err);
        return -1;
    }

    // ECE negotiation, done while the QP is in INIT state (must be set
    // before the RTR transition).
    //
    // End-to-end model:
    //   Server: `remote->ece' is the client's queried ECE. Set it here,
    //           then after RTS we query the reduced/negotiated ECE and
    //           send it back in the server hello.
    //   Client: `remote->ece' is the server's reduced ECE.
    //           Just set it here.
    *use_ece = true;
    if (IbvSetEce != nullptr && peer.ece.has_value()) {
        ibv_ece ece = *peer.ece;
        int err = IbvSetEce(qp, &ece);
        if (err != 0) {
            *use_ece = false;
            LOG(WARNING) << "Fail to IbvSetEce, continue without ECE: "
                         << berror(err);
        }
    }

    return 0;
}

QpActivationStage ConnectRcQp(ibv_qp* qp, const ParsedHello& peer,
                             bool is_server, uint8_t min_rnr_timer,
                             uint8_t rnr_retry) {
    ibv_qp_attr attr = {};
    attr.qp_state = IBV_QPS_RTR;
    // Both receive backends use the MTU advertised by the handshake. The
    // server takes the minimum of the local and client active MTUs. The client
    // receives the negotiated value. Without `peer.path_mtu`, retain the
    // legacy 1024-byte cap while respecting the local port limit.
    uint32_t local_mtu = GetRdmaActiveMtu();
    uint32_t path_mtu = std::min(static_cast<uint32_t>(IBV_MTU_1024), local_mtu);
    if (peer.path_mtu.has_value()) {
        path_mtu = is_server ? std::min(local_mtu, *peer.path_mtu)
                             : *peer.path_mtu;
    }
    attr.path_mtu = static_cast<ibv_mtu>(path_mtu);
    attr.ah_attr.grh.dgid = peer.gid;
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.sgid_index = GetRdmaGidIndex();
    attr.ah_attr.grh.hop_limit = MAX_HOP_LIMIT;
    attr.ah_attr.grh.traffic_class = 0;
    attr.ah_attr.dlid = peer.lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.static_rate = 0;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.port_num = GetRdmaPortNum();
    attr.dest_qp_num = peer.qp_num;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 0;
    attr.min_rnr_timer = min_rnr_timer;
    int err = IbvModifyQp(qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_PATH_MTU |
                IBV_QP_MIN_RNR_TIMER |
                IBV_QP_AV |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN));
    if (err != 0) {
        errno = err;
        LOG(WARNING) << "Fail to modify QP from INIT to RTR: " << berror(err);
        return QpActivationStage::NONE;
    }

    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = TIMEOUT;
    attr.retry_cnt = RETRY_CNT;
    attr.rnr_retry = rnr_retry;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 0;
    err = IbvModifyQp(qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_RNR_RETRY |
                IBV_QP_RETRY_CNT |
                IBV_QP_TIMEOUT |
                IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC));
    if (err != 0) {
        errno = err;
        LOG(WARNING) << "Fail to modify QP from RTR to RTS: " << berror(err);
        return QpActivationStage::RTR;
    }
    return QpActivationStage::RTS;
}

void QueryNegotiatedEce(ibv_qp* qp, const ParsedHello& peer,
                        bool is_server, bool use_ece,
                        butil::optional<ibv_ece>* outgoing_ece) {
    // On the server side, now that the QP reached RTS, query the reduced/negotiated
    // ECE (the subset of enhancements supported by both peers) so it can be returned
    // to the client in the server hello.
    if (is_server && use_ece && IbvQueryEce != nullptr && peer.ece.has_value()) {
        ibv_ece ece;
        int qerr = IbvQueryEce(qp, &ece);
        if (qerr == 0) {
            *outgoing_ece = ece;
        } else {
            LOG(WARNING) << "Fail to IbvQueryEce(negotiated), "
                            "continue without ECE: " << berror(qerr);
        }
    }

}

// RdmaIOBuf inherits from IOBuf to provide a new function.
// The reason is that we need to use some protected member function of IOBuf.
class RdmaIOBuf : public butil::IOBuf {
public:
    // Cut the current IOBuf to ibv_sge list and `to' for at most first max_sge
    // blocks or first max_len bytes. `*in_pool' is cleared if any block is
    // outside the block pool (user registered memory).
    // Return: the bytes included in the sglist, or -1 if failed
    ssize_t cut_into_sglist_and_iobuf(ibv_sge* sglist, size_t* sge_index,
                                      butil::IOBuf* to, size_t max_sge,
                                      size_t max_len, bool* in_pool,
                                      bool* has_user_data = nullptr) {
        size_t len = 0;
        while (*sge_index < max_sge) {
            if (len == max_len || _ref_num() == 0) {
                break;
            }
            butil::IOBuf::BlockRef const& r = _ref_at(0);
            if (has_user_data &&
                (r.block->flags & butil::IOBUF_BLOCK_FLAGS_USER_DATA)) {
                *has_user_data = true;
            }
            CHECK(r.length > 0);
            const void* start = fetch1();
            uint32_t lkey = GetRegionId(start);
            if (lkey == 0) {  // get lkey for user registered memory
                *in_pool = false;
                uint64_t meta = get_first_data_meta();
                if (meta <= UINT_MAX) {
                    lkey = (uint32_t)meta;
                }
            }
            if (BAIDU_UNLIKELY(lkey == 0)) {  // only happens when meta is not specified
                lkey = GetLKey((char*)start - r.offset);
            }
            if (lkey == 0) {
                LOG(WARNING) << "Memory not registered for rdma. "
                             << "Is this iobuf allocated before calling "
                             << "GlobalRdmaInitializeOrDie? Or just forget to "
                             << "call RegisterMemoryForRdma for your own buffer?";
                errno = ERDMAMEM;
                return -1;
            }
            size_t i = *sge_index;
            if (len + r.length > max_len) {
                // Split the block to comply with size for receiving
                sglist[i].length = max_len - len;
                len = max_len;
            } else {
                sglist[i].length = r.length;
                len += r.length;
            }
            sglist[i].addr = (uint64_t)start;
            sglist[i].lkey = lkey;
            cutn(to, sglist[i].length);
            (*sge_index)++;
        }
        return len;
    }
};

ssize_t CutRdmaSges(butil::IOBuf* from, ibv_sge* sges, size_t* index,
                   butil::IOBuf* keepalive, size_t max_sge,
                   size_t max_bytes, bool* in_pool, bool* has_user_data) {
    return static_cast<RdmaIOBuf*>(from)->cut_into_sglist_and_iobuf(
        sges, index, keepalive, max_sge, max_bytes, in_pool, has_user_data);
}

// Whether to post a message of len bytes inline. Only a message from the block
// pool (host memory) that fits the inline size the QP was granted is copied
// into the WQE. User registered memory may be device memory, which the CPU
// cannot copy from, so it is never inlined. Not static: exposed for UT.
bool ShouldPostInline(bool in_pool, size_t len, uint32_t max_inline_data) {
    return in_pool && len <= max_inline_data;
}

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
