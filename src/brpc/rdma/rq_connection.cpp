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
#include <mutex>
#include <unordered_set>
#include <gflags/gflags.h>
#include "butil/fd_utility.h"
#include "butil/logging.h"                   // CHECK, LOG
#include "butil/sys_byteorder.h"             // HostToNet,NetToHost
#include "bthread/bthread.h"
#include "brpc/errno.pb.h"
#include "bthread/mutex.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/reloadable_flags.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rq_connection.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_handshake_constants.h"


DECLARE_int32(task_group_ntags);
namespace brpc {
namespace rdma {
extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*);
extern int (*IbvDestroyCompChannel)(ibv_comp_channel*);
extern int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**);
extern void (*IbvAckCqEvents)(ibv_cq*, unsigned int);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);
extern int (*IbvQueryEce)(ibv_qp*, ibv_ece*);
extern bool g_skip_rdma_init;

DEFINE_int32(rdma_sq_size, 128, "SQ size for RDMA");
DEFINE_int32(rdma_rq_size, 128, "RQ size for RDMA");
DEFINE_int32(rdma_cqe_poll_once, 32, "The maximum of cqe number polled once.");
DEFINE_int32(rdma_prepared_qp_size, 128, "SQ and RQ size for prepared QP.");
DEFINE_int32(rdma_prepared_qp_cnt, 1024, "Initial count of prepared QP.");
DEFINE_bool(rdma_use_polling, false, "Use polling mode for RDMA.");
DEFINE_int32(rdma_poller_num, 1, "Poller number in RDMA polling mode.");
DEFINE_bool(rdma_poller_yield, false, "Yield thread in RDMA polling mode.");
DECLARE_bool(rdma_recv_zerocopy);
DECLARE_int32(rdma_zerocopy_min_size);
DECLARE_bool(rdma_trace_verbose);
DECLARE_bool(rdma_disable_bthread);
DECLARE_bool(rdma_use_srq);

extern const size_t RESERVED_WR_NUM = 3;
// Inline size asked for at QP creation, and on Mellanox/NVIDIA NICs the
// largest message sent inline: 256 - 16 (ctrl) - 4 (inline header). A bigger
// inlined WQE no longer fits the 256-byte BlueFlame buffer of mlx5 NICs.
static const uint32_t BF_MAX_INLINE_DATA = 256 - 16 - 4;
static const uint32_t MELLANOX_VENDOR_ID = 0x02c9;
// The verbs API cannot report the inline data limit. Devices whose kernel
// driver has a fixed limit try it first: Intel irdma 216, 101 or 48 depending
// on the generation (101 on the E810, 48 on the X722, see
// drivers/infiniband/hw/irdma/ig3rdma_hw.h, user.h, i40iw_hw.h), Alibaba
// erdma 96 (drivers/infiniband/hw/erdma/erdma_verbs.h). Other devices start
// at BF_MAX_INLINE_DATA, and any refused size steps down by INLINE_DATA_STEP
// until one is accepted. libfabric's verbs provider also probes the limit
// (vrb_find_max_inline()).
static const struct {
    uint32_t vendor_id;
    uint32_t max_inline;
} KNOWN_INLINE_LIMITS[] = {
    {0x8086, 216},
    {0x8086, 101},
    {0x8086, 48},
    {0x1ded, 96}};
static const uint32_t INLINE_DATA_STEP = 16;

static bthread::Mutex* g_rdma_resource_mutex = nullptr;
static RdmaResource* g_rdma_resource_list = nullptr;

std::function<void()> RdmaResource::PrepareAsyncEvent(ibv_event_type type) {
    SocketId sid = socket_id.load(butil::memory_order_acquire);
    if (sid == INVALID_SOCKET_ID ||
        (type != IBV_EVENT_QP_FATAL && type != IBV_EVENT_QP_REQ_ERR &&
         type != IBV_EVENT_QP_ACCESS_ERR && type != IBV_EVENT_CQ_ERR)) {
        return {};
    }

    // No resource pointer survives the acknowledgement in the dispatcher.
    return [weak = connection] {
        // Releasing the Socket may destroy a QP/CQ and wait for further async
        // acknowledgements. Keep the event reader free to drain those events.
        auto task = std::make_unique<std::weak_ptr<RqConnection>>(weak);
        auto fail_socket = [](void* arg) -> void* {
            std::unique_ptr<std::weak_ptr<RqConnection>> task(
                static_cast<std::weak_ptr<RqConnection>*>(arg));
            auto connection = task->lock();
            if (connection != nullptr) {
                connection->FailFromAsyncEvent();
            }
            return nullptr;
        };

        bthread_t tid;
        if (bthread_start_background(&tid, nullptr, fail_socket, task.get()) != 0) {
            LOG(ERROR) << "Cannot schedule asynchronous RDMA connection failure";
            return;
        }
        task.release();
    };
}

static void DeleteRdmaResource(RdmaResource* resource) {
    // Stop all virtual callbacks and wait for in-flight callback capture before
    // entering the destructor. Unregistering each CQ only when it is destroyed
    // would allow PrepareAsyncEvent to race with object destruction.
    UnregisterRdmaAsyncEventContext(resource->qp);
    UnregisterRdmaAsyncEventContext(resource->polling_cq);
    UnregisterRdmaAsyncEventContext(resource->send_cq);
    UnregisterRdmaAsyncEventContext(resource->recv_cq);
    delete resource;
}

RdmaResource::~RdmaResource() {
    if (qp != nullptr) {
        int error = IbvDestroyQp(qp);
        LOG_IF(WARNING, error != 0) << "Fail to destroy QP: " << berror(error);
    }
    for (auto* cq : {polling_cq, send_cq, recv_cq}) {
        if (cq) {
            int error = IbvDestroyCq(cq);
            LOG_IF(WARNING, error != 0) << "Fail to destroy CQ: " << berror(error);
        }
    }
    if (comp_channel != nullptr) {
        int error = IbvDestroyCompChannel(comp_channel);
        LOG_IF(WARNING, error != 0)
            << "Fail to destroy CQ channel: " << berror(error);
    }
}

RqConnection::RqConnection(Socket* s)
    : _socket(s)
    , _sid(s->id())
    , _resource(nullptr)
    , _send_cq_events(0)
    , _recv_cq_events(0)
    , _cq_sid(INVALID_SOCKET_ID)
    , _sq_size(FLAGS_rdma_sq_size)
    , _rq_size(FLAGS_rdma_rq_size)
    , _remote_recv_block_size(0)
    , _accumulated_ack(0)
    , _unsolicited(0)
    , _unsolicited_bytes(0)
    , _sq_current(0)
    , _sq_unsignaled(0)
    , _sq_sent(0)
    , _rq_received(0)
    , _local_window_capacity(0)
    , _remote_window_capacity(0)
    , _sq_imm_window_size(0)
    , _remote_rq_window_size(0)
    , _sq_window_size(0)
    , _new_rq_wrs(0)
{
    if (_sq_size < MIN_QP_SIZE) {
        _sq_size = MIN_QP_SIZE;
    }
    if (_sq_size > MAX_QP_SIZE) {
        _sq_size = MAX_QP_SIZE;
    }
    if (_rq_size < MIN_QP_SIZE) {
        _rq_size = MIN_QP_SIZE;
    }
    if (_rq_size > MAX_QP_SIZE) {
        _rq_size = MAX_QP_SIZE;
    }
}


std::shared_ptr<RqConnection> RqConnection::Create(Socket* socket) {
    auto connection = std::shared_ptr<RqConnection>(new RqConnection(socket));
    if (connection->Initialize() != 0) {
        const int saved_errno = errno;
        connection.reset();
        errno = saved_errno;
        return {};
    }
    return connection;
}

RqConnection::~RqConnection() {
    Close();
}

void RqConnection::GetLocalInfo(RdmaLocalInfo* info) const {
    *info = DefaultRdmaLocalInfo(RdmaQueueMode::PRIVATE_RQ);
    info->sq_size = _sq_size;
    info->rq_size = _rq_size;
    info->qp_num = _resource && _resource->qp ? _resource->qp->qp_num : 0;
}

int RqConnection::QueryEce(ibv_ece* ece) {
    return _resource && _resource->qp && IbvQueryEce ?
           IbvQueryEce(_resource->qp, ece) : ENOTCONN;
}

int RqConnection::Activate(const ParsedHello& peer, bool is_server,
                           butil::optional<ibv_ece>* outgoing_ece) {
    if (!_resource || !_resource->qp) {
        errno = ENOTCONN;
        return -1;
    }

    _remote_recv_block_size = peer.block_size;
    _local_window_capacity = std::min(_sq_size, peer.rq_size) - RESERVED_WR_NUM;
    _remote_window_capacity = std::min(_rq_size, peer.sq_size) - RESERVED_WR_NUM;
    _sq_imm_window_size = RESERVED_WR_NUM;
    _remote_rq_window_size.store(_local_window_capacity, butil::memory_order_relaxed);
    _sq_window_size.store(_local_window_capacity, butil::memory_order_relaxed);
    bool use_ece;
    if (InitializeRcQp(_resource->qp, peer, &use_ece) != 0 ||
        PostRecv(_rq_size, true) != 0) {
        return -1;
    }
    if (ConnectRcQp(_resource->qp, peer, is_server, 0, 0) != QpActivationStage::RTS) {
        return -1;
    }
    QueryNegotiatedEce(_resource->qp, peer, is_server, use_ece, outgoing_ece);
    return 0;
}

void RqConnection::Close(int error) {
    _closed.store(true, butil::memory_order_release);
    DeallocateResources();
    _sbuf.clear();
    _rbuf.clear();
    _rbuf_data.clear();
    _received.clear();
    if (error) {
        SocketUniquePtr socket;
        if (Socket::Address(_sid, &socket) == 0) {
            socket->SetFailed(error, "Private RQ connection closed: %s", berror(error));
        }
    }
}

void RqConnection::FailFromAsyncEvent() {
    SocketUniquePtr socket;
    if (Socket::Address(_sid, &socket) != 0) {
        return;
    }
    // Address first so health-check Reset cannot pass its reference barrier
    // between checking `_closed` and failing the Socket. Every Reset closes
    // the old connection before reviving the same SocketId.
    // Do not read the endpoint's `_connection` here. Async errors can also
    // arrive during Initialize, before the handshake publishes that pointer.
    if (!_closed.load(butil::memory_order_acquire)) {
        socket->SetFailed(ERDMA, "RDMA QP/CQ asynchronous error");
    }
}

bool RqConnection::IsWritable() {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        return false;
    }

    return _resource && _remote_rq_window_size.load(butil::memory_order_relaxed) > 0 &&
           _sq_window_size.load(butil::memory_order_relaxed) > 0;
}

ssize_t RqConnection::Send(butil::IOBuf** from, size_t ndata) {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        errno = EAGAIN;
        return -1;
    }

    if (!_resource) {
        errno = ENOTCONN;
        return -1;
    }

    CHECK(from != nullptr);
    CHECK(ndata > 0);

    size_t total_len = 0;
    size_t current = 0;
    uint32_t remote_rq_window_size =
        _remote_rq_window_size.load(butil::memory_order_relaxed);
    uint32_t sq_window_size =
        _sq_window_size.load(butil::memory_order_relaxed);
    ibv_send_wr wr;
    int max_sge = GetRdmaMaxSge();
    ibv_sge sglist[max_sge];
    while (current < ndata) {
        if (remote_rq_window_size == 0 || sq_window_size == 0) {
            // There is no space left in SQ or remote RQ.
            if (total_len > 0) {
                break;
            } else {
                errno = EAGAIN;
                return -1;
            }
        }
        butil::IOBuf* to = &_sbuf[_sq_current];
        size_t this_len = 0;

        memset(&wr, 0, sizeof(wr));
        wr.sg_list = sglist;
        wr.opcode = IBV_WR_SEND_WITH_IMM;

        butil::IOBuf* data = from[current];
        size_t sge_index = 0;
        bool in_pool = true;
        while (sge_index < (uint32_t)max_sge &&
                this_len < _remote_recv_block_size) {
            if (data->empty()) {
                // The current IOBuf is empty, find next one
                ++current;
                if (current == ndata) {
                    break;
                }
                data = from[current];
                continue;
            }

            ssize_t len = CutRdmaSges(data, sglist, &sge_index, to, max_sge,
                                      _remote_recv_block_size - this_len, &in_pool);
            if (len < 0) {
                return -1;
            }
            CHECK(len > 0);
            this_len += len;
            total_len += len;
        }
        if (this_len == 0) {
            continue;
        }

        wr.num_sge = sge_index;
        if (ShouldPostInline(in_pool, this_len, _resource->max_inline_data)) {
            wr.send_flags |= IBV_SEND_INLINE;
        }

        uint32_t imm = _new_rq_wrs.exchange(0, butil::memory_order_relaxed);
        wr.imm_data = butil::HostToNet32(imm);
        // Avoid too much recv completion event to reduce the cpu overhead
        bool solicited = false;
        if (remote_rq_window_size == 1 || sq_window_size == 1 || current + 1 >= ndata) {
            // Only last message in the write queue or last message in the
            // current window will be flagged as solicited.
            solicited = true;
        } else {
            if (_unsolicited > _local_window_capacity / 4) {
                // Make sure the recv side can be signaled to return ack
                solicited = true;
            } else if (_accumulated_ack > _remote_window_capacity / 4) {
                // Make sure the recv side can be signaled to handle ack
                solicited = true;
            } else if (_unsolicited_bytes > 1048576) {
                // Make sure the recv side can be signaled when it receives enough data
                solicited = true;
            } else {
                ++_unsolicited;
                _unsolicited_bytes += this_len;
                _accumulated_ack += imm;
            }
        }
        if (solicited) {
            wr.send_flags |= IBV_SEND_SOLICITED;
            _unsolicited = 0;
            _unsolicited_bytes = 0;
            _accumulated_ack = 0;
        }

        // Avoid too much send completion event to reduce the CPU overhead
        ++_sq_unsignaled;
        if (_sq_unsignaled >= _local_window_capacity / 4) {
            // Refer to:
            // http::www.rdmamojo.com/2014/06/30/working-unsignaled-completions/
            wr.send_flags |= IBV_SEND_SIGNALED;
            wr.wr_id = _sq_unsignaled;
            _sq_unsignaled = 0;
        }

        ibv_send_wr* bad = nullptr;
        int err = ibv_post_send(_resource->qp, &wr, &bad);
        if (err != 0) {
            // We use other way to guarantee the Send Queue is not full.
            // So we just consider this error as an unrecoverable error.
            std::ostringstream oss;
            Debug(oss, ", ");
            LOG(WARNING) << "Fail to ibv_post_send: " << berror(err) << " " << oss.str();
            errno = err;
            return -1;
        }

        ++_sq_current;
        if (_sq_current == _sq_size - RESERVED_WR_NUM) {
            _sq_current = 0;
        }

        // Update `_remote_rq_window_size' and `_sq_window_size'. Note that
        // `_remote_rq_window_size' and `_sq_window_size' will never be negative.
        // Because there is at most one thread can enter this function for each
        // Socket, and the other thread of HandleCompletion can only add these
        // counters.
        remote_rq_window_size =
            _remote_rq_window_size.fetch_sub(1, butil::memory_order_relaxed) - 1;
        sq_window_size = _sq_window_size.fetch_sub(1, butil::memory_order_relaxed) - 1;
    }

    return total_len;
}

int RqConnection::SendAck(int num) {
    if (_new_rq_wrs.fetch_add(num, butil::memory_order_relaxed) > _remote_window_capacity / 2 &&
        _sq_imm_window_size > 0) {
        return SendImm(_new_rq_wrs.exchange(0, butil::memory_order_relaxed));
    }
    return 0;
}

int RqConnection::SendImm(uint32_t imm) {
    if (imm == 0) {
        return 0;
    }

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.imm_data = butil::HostToNet32(imm);
    wr.send_flags |= IBV_SEND_SOLICITED | IBV_SEND_SIGNALED;
    wr.wr_id = 0;

    ibv_send_wr* bad = nullptr;
    int err = ibv_post_send(_resource->qp, &wr, &bad);
    if (err != 0) {
        std::ostringstream oss;
        Debug(oss, ", ");
        // We use other way to guarantee the Send Queue is not full.
        // So we just consider this error as an unrecoverable error.
        LOG(WARNING) << "Fail to ibv_post_send: " << berror(err) << " " << oss.str();
        return -1;
    }

    // `_sq_imm_window_size' will never be negative.
    // Because IMM can only be sent if
    // `_sq_imm_window_size` is greater than 0.
    _sq_imm_window_size -= 1;
    return 0;
}

ssize_t RqConnection::HandleCompletion(ibv_wc& wc) {
    bool zerocopy = FLAGS_rdma_recv_zerocopy;
    switch (wc.opcode) {
    case IBV_WC_SEND: {  // send completion
        if (wc.wr_id == 0) {
            _sq_imm_window_size += 1;
            // If there are any unacknowledged recvs, send an ack.
            SendAck(0);
            return 0;
        }
        // Update SQ window.
        uint16_t wnd_to_update = wc.wr_id;
        for (uint16_t i = 0; i < wnd_to_update; ++i) {
            _sbuf[_sq_sent++].clear();
            if (_sq_sent == _sq_size - RESERVED_WR_NUM) {
                _sq_sent = 0;
            }
        }
        butil::subtle::MemoryBarrier();

        _sq_window_size.fetch_add(wnd_to_update, butil::memory_order_relaxed);
        if (_remote_rq_window_size.load(butil::memory_order_relaxed) >=
            _local_window_capacity / 8) {
            // Do not wake up writing thread right after polling IBV_WC_SEND.
            // Otherwise the writing thread may switch to background too quickly.
            _socket->WakeAsEpollOut();
        }
        return 0;
    }
    case IBV_WC_RECV: {  // recv completion
        // Please note that only the first wc.byte_len bytes is valid
        if (wc.byte_len > 0) {
            if (wc.byte_len < (uint32_t)FLAGS_rdma_zerocopy_min_size) {
                zerocopy = false;
            }
            CHECK(_receiving_enabled);
            butil::IOBuf& read_buf = _received;
            if (zerocopy) {
                _rbuf[_rq_received].cutn(&read_buf, wc.byte_len);
            } else {
                // Copy data when the receive data is really small
                read_buf.append(_rbuf_data[_rq_received], wc.byte_len);
            }
        }
        if ((wc.wc_flags & IBV_WC_WITH_IMM) != 0 && wc.imm_data > 0) {
            // Update window
            uint32_t acks = butil::NetToHost32(wc.imm_data);
            uint32_t wnd_thresh = _local_window_capacity / 8;
            uint32_t remote_rq_window_size =
                _remote_rq_window_size.fetch_add(acks, butil::memory_order_relaxed);
            if (_sq_window_size.load(butil::memory_order_relaxed) > 0 &&
                (remote_rq_window_size >= wnd_thresh || acks >= wnd_thresh)) {
                // Do not wake up writing thread right after _remote_rq_window_size > 0.
                // Otherwise the writing thread may switch to background too quickly.
                _socket->WakeAsEpollOut();
            }
        }
        // We must re-post recv WR
        if (PostRecv(1, zerocopy) < 0) {
            return -1;
        }
        if (wc.byte_len > 0) {
            SendAck(1);
        }
        return wc.byte_len;
    }
    default:
        // Some driver bugs may lead to unexpected completion opcode.
        // If this happens, please update your driver.
        CHECK(false) << "This should not happen. Got a completion with opcode="
                     << wc.opcode;
        return -1;
    }
    return 0;
}

int RqConnection::DoPostRecv(void* block, size_t block_size) {
    ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));
    ibv_sge sge;
    sge.addr = (uint64_t)block;
    sge.length = block_size;
    sge.lkey = GetRegionId(block);
    wr.num_sge = 1;
    wr.sg_list = &sge;

    ibv_recv_wr* bad = nullptr;
    int err = ibv_post_recv(_resource->qp, &wr, &bad);
    if (err != 0) {
        LOG(WARNING) << "Fail to ibv_post_recv: " << berror(err);
        return -1;
    }
    return 0;
}

int RqConnection::PostRecv(uint32_t num, bool zerocopy) {
    // We do the post repeatedly from the _rbuf[_rq_received].
    while (num > 0) {
        if (zerocopy) {
            _rbuf[_rq_received].clear();
            butil::IOBufAsZeroCopyOutputStream os(
                &_rbuf[_rq_received], g_rdma_recv_block_size + IOBUF_BLOCK_HEADER_LEN);
            int size = 0;
            if (!os.Next(&_rbuf_data[_rq_received], &size)) {
                // Memory is not enough for preparing a block
                PLOG(WARNING) << "Fail to allocate rbuf";
                return -1;
            } else {
                CHECK_EQ(static_cast<uint32_t>(size), g_rdma_recv_block_size);
            }
        }
        if (DoPostRecv(_rbuf_data[_rq_received], g_rdma_recv_block_size) < 0) {
            _rbuf[_rq_received].clear();
            return -1;
        }
        --num;
        ++_rq_received;
        if (_rq_received == _rq_size) {
            _rq_received = 0;
        }
    };
    return 0;
}

// On the first try, cap the requested inline size at the vendor's first known
// limit. After a refusal, use the next smaller known limit if available, or
// reduce the size by one step otherwise. Exposed for unit tests.
uint32_t InlineDataToRequest(uint32_t vendor_id, uint32_t size, bool refused) {
    for (const auto& known : KNOWN_INLINE_LIMITS) {
        if (known.vendor_id == vendor_id &&
            (!refused || known.max_inline < size)) {
            return std::min(known.max_inline, size);
        }
    }
    if (refused) {
        size = size > INLINE_DATA_STEP ? size - INLINE_DATA_STEP : 0;
    }
    return size;
}

// The inline size the last QP was created with (UINT32_MAX: none yet). All
// QPs use the same device and attributes apart from their queue sizes, so
// later QPs start here instead of repeating the step-down. Not static:
// exposed for UT.
butil::atomic<uint32_t> g_inline_data_request(UINT32_MAX);

// Creates a QP with attr, asking for the inline size the last QP was created
// with (at first BF_MAX_INLINE_DATA, capped by the vendor's known limit), or
// less if the device refuses that size, and sets *max_inline_data to the
// largest message to post inline on it. Not static: exposed for UT.
ibv_qp* CreateQpWithInlineData(ibv_pd* pd, ibv_qp_init_attr* attr,
                               uint32_t vendor_id, uint32_t* max_inline_data) {
    uint32_t inline_size =
        g_inline_data_request.load(butil::memory_order_relaxed);
    if (inline_size == UINT32_MAX) {
        inline_size = InlineDataToRequest(vendor_id, BF_MAX_INLINE_DATA, false);
    }
    attr->cap.max_inline_data = inline_size;
    ibv_qp* qp = IbvCreateQp(pd, attr);
    // A refused inline size fails with EINVAL. Any other failure is left to
    // the caller, which reports its errno.
    while (qp == nullptr && errno == EINVAL && inline_size > 0) {
        uint32_t next = InlineDataToRequest(vendor_id, inline_size, true);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "ibv_create_qp refused " << inline_size
            << " bytes of inline data, trying " << next;
        inline_size = next;
        attr->cap.max_inline_data = inline_size;
        qp = IbvCreateQp(pd, attr);
    }
    if (qp != nullptr) {
        g_inline_data_request.store(inline_size, butil::memory_order_relaxed);
        // ibv_create_qp writes the granted inline data size back into attr
        *max_inline_data = attr->cap.max_inline_data;
        if (vendor_id == MELLANOX_VENDOR_ID) {
            *max_inline_data = std::min(*max_inline_data, BF_MAX_INLINE_DATA);
        }
    }
    return qp;
}

static ibv_qp* AllocateQp(ibv_cq* send_cq, ibv_cq* recv_cq, uint32_t sq_size,
                          uint32_t rq_size, uint32_t* max_inline_data,
                          RdmaAsyncEventContext* context = nullptr) {
    ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_context = context;
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.cap.max_send_wr = sq_size;
    attr.cap.max_recv_wr = rq_size;
    attr.cap.max_send_sge = GetRdmaMaxSge();
    attr.cap.max_recv_sge = 1;
    attr.qp_type = IBV_QPT_RC;
    return CreateQpWithInlineData(GetRdmaPd(), &attr, GetRdmaVendorId(),
                                  max_inline_data);
}

static RdmaResource* AllocateQpCq(uint16_t sq_size, uint16_t rq_size) {
    std::unique_ptr<RdmaResource, decltype(&DeleteRdmaResource)> resource(
        new RdmaResource, DeleteRdmaResource);
    auto* context = static_cast<RdmaAsyncEventContext*>(resource.get());
    if (!FLAGS_rdma_use_polling) {
        resource->comp_channel = IbvCreateCompChannel(GetRdmaContext());
        if (resource->comp_channel == nullptr) {
            PLOG(WARNING) << "Fail to create comp channel for CQ";
            return nullptr;
        }

        if (butil::make_close_on_exec(resource->comp_channel->fd) < 0) {
            PLOG(WARNING) << "Fail to set comp channel close-on-exec";
            return nullptr;
        }
        if (butil::make_non_blocking(resource->comp_channel->fd) < 0) {
            PLOG(WARNING) << "Fail to set comp channel nonblocking";
            return nullptr;
        }

        resource->send_cq = IbvCreateCq(GetRdmaContext(), sq_size,
                                        context, resource->comp_channel, GetRdmaCompVector());
        if (resource->send_cq == nullptr) {
            PLOG(WARNING) << "Fail to create send CQ";
            return nullptr;
        }
        RegisterRdmaAsyncEventContext(resource->send_cq, context);

        resource->recv_cq = IbvCreateCq(GetRdmaContext(), rq_size,
                                        context, resource->comp_channel, GetRdmaCompVector());
        if (resource->recv_cq == nullptr) {
            PLOG(WARNING) << "Fail to create recv CQ";
            return nullptr;
        }
        RegisterRdmaAsyncEventContext(resource->recv_cq, context);

        resource->qp = AllocateQp(resource->send_cq, resource->recv_cq, sq_size, rq_size,
                                  &resource->max_inline_data, context);
        if (resource->qp == nullptr) {
            PLOG(WARNING) << "Fail to create QP";
            return nullptr;
        }
    } else {
        resource->polling_cq =
            IbvCreateCq(GetRdmaContext(), sq_size + rq_size, context, nullptr, 0);
        if (resource->polling_cq == nullptr) {
            PLOG(WARNING) << "Fail to create polling CQ";
            return nullptr;
        }
        RegisterRdmaAsyncEventContext(resource->polling_cq, context);
        resource->qp = AllocateQp(resource->polling_cq,
                                  resource->polling_cq,
                                  sq_size, rq_size,
                                  &resource->max_inline_data, context);
        if (resource->qp == nullptr) {
            PLOG(WARNING) << "Fail to create QP";
            return nullptr;
        }
    }

    RegisterRdmaAsyncEventContext(resource->qp, context);
    return resource.release();
}

int RqConnection::Initialize() {
    if (_sq_size <= FLAGS_rdma_prepared_qp_size &&
        _rq_size <= FLAGS_rdma_prepared_qp_size) {
        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        if (g_rdma_resource_list) {
            _resource = g_rdma_resource_list;
            g_rdma_resource_list = g_rdma_resource_list->next;
        }
    }
    if (_resource == nullptr) {
        _resource = AllocateQpCq(_sq_size, _rq_size);
    } else {
        _resource->next = nullptr;
    }
    if (_resource == nullptr) {
        return -1;
    }
    _resource->connection = shared_from_this();
    _resource->socket_id.store(_socket->id(), butil::memory_order_release);

    if (!FLAGS_rdma_use_polling) {
        if (ReqNotifyCq(true, false) != 0) {
            return -1;
        }
        if (ReqNotifyCq(false, false) != 0) {
            return -1;
        }
    }

    _sbuf.resize(_sq_size - RESERVED_WR_NUM);
    if (_sbuf.size() != _sq_size - RESERVED_WR_NUM) {
        return -1;
    }
    _rbuf.resize(_rq_size);
    if (_rbuf.size() != _rq_size) {
        return -1;
    }
    _rbuf_data.resize(_rq_size, nullptr);
    if (_rbuf_data.size() != _rq_size) {
        return -1;
    }

    return 0;
}

int RqConnection::EnableReceive() {
    if (_cq_sid != INVALID_SOCKET_ID) {
        // Already started.
        return 0;
    }
    if (_resource == nullptr) {
        if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
            // For UT: AllocateResources() succeeds without allocating anything.
            return 0;
        }

        LOG(WARNING) << "No RDMA resource to start CQ events on, " << *_socket;
        errno = ERDMA;
        return -1;
    }

    // Attach the owning context before publishing the CQ fd. A failed
    // Socket::Create must never ambiguously own/delete the context.
    SocketOptions options;
    options.keytable_pool = _socket->_keytable_pool;
    options.bthread_tag = _socket->_io_event.bthread_tag();
    options.on_edge_triggered_events = PollCq;
    if (Socket::Create(options, &_cq_sid) < 0) {
        PLOG(WARNING) << "Fail to create socket for cq";
        return -1;
    }
    SocketUniquePtr cq_socket;
    if (Socket::Address(_cq_sid, &cq_socket) != 0) {
        errno = ENOTCONN;
        return -1;
    }
    cq_socket->set_user(new CqContext(shared_from_this()));
    _receiving_enabled = true;
    if (!FLAGS_rdma_use_polling &&
        cq_socket->ResetFileDescriptor(_resource->comp_channel->fd) != 0) {
        return -1;
    }
    if (FLAGS_rdma_use_polling) {
        PollerAddCqSid();
    }

    return 0;
}

void RqConnection::DeallocateResources() {
    if (_resource == nullptr) {
        return;
    }
    if (FLAGS_rdma_use_polling) {
        PollerRemoveCqSid();
    }
    // Stop publishing callbacks before destroying the channel fd. Queued
    // callbacks still own their CqContext and reject the detached connection.
    SocketUniquePtr cq_socket;
    if (_cq_sid != INVALID_SOCKET_ID &&
        Socket::AddressFailedAsWell(_cq_sid, &cq_socket) >= 0) {
        if (!FLAGS_rdma_use_polling) {
            cq_socket->_io_event.RemoveConsumer(cq_socket->_fd);
        }
        cq_socket->_fd = -1;  // The verbs channel owns and closes this fd.
        cq_socket->SetFailed();
    }
    _cq_sid = INVALID_SOCKET_ID;
    if (_resource->send_cq != nullptr) {
        IbvAckCqEvents(_resource->send_cq, _send_cq_events);
    }
    if (_resource->recv_cq != nullptr) {
        IbvAckCqEvents(_resource->recv_cq, _recv_cq_events);
    }

    // Destroy activated QPs directly. RESET plus an empty CQ cannot prove that
    // async events have drained, so an old QP must not be rebound to another
    // SocketId. The preallocated pool keeps only unused resources.
    DeleteRdmaResource(_resource);
    _resource = nullptr;

    // Detach everything from this connection so that the function is
    // idempotent: it is called both when the endpoint is reset/destroyed
    // and when AllocateResources() fails halfway.
    _receiving_enabled = false;
    _cq_sid = INVALID_SOCKET_ID;
    _send_cq_events = 0;
    _recv_cq_events = 0;
}

static const int MAX_CQ_EVENTS = 128;

int RqConnection::GetAndAckEvents(SocketUniquePtr& s) {
    void* context = nullptr;
    ibv_cq* cq = nullptr;
    while (true) {
        if (IbvGetCqEvent(_resource->comp_channel, &cq, &context) != 0) {
            if (errno != EAGAIN) {
                const int saved_errno = errno;
                PLOG(ERROR) << "Fail to get cq event from " << s->description();
                s->SetFailed(saved_errno, "Fail to get cq event from %s: %s",
                             s->description().c_str(), berror(saved_errno));
                return -1;
            }
            break;
        }
        if (cq == _resource->send_cq) {
            ++_send_cq_events;
        } else if (cq == _resource->recv_cq) {
            ++_recv_cq_events;
        } else {
            // Unexpected CQ event that does not belong to
            // this endpoint's send/recv CQs.
            LOG(WARNING) << "Unexpected CQ event from cq=" << cq
                         << " of " << s->description();
            // Acknowledge this single event immediately
            // to avoid leaking unacknowledged events.
            IbvAckCqEvents(cq, 1);
        }
    }
    if (_send_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(_resource->send_cq, _send_cq_events);
        _send_cq_events = 0;
    }
    if (_recv_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(_resource->recv_cq, _recv_cq_events);
        _recv_cq_events = 0;
    }
    return 0;
}

int RqConnection::ReqNotifyCq(bool send_cq, bool fatal_on_error) {
    const int err = ibv_req_notify_cq(
        send_cq ? _resource->send_cq : _resource->recv_cq,
        send_cq ? 0 : 1);
    if (err != 0) {
        errno = err;
        PLOG(WARNING) << "Fail to arm " << (send_cq ? "send" : "recv")
                      << " CQ comp channel from " << _socket->description();
        if (fatal_on_error) {
            _socket->SetFailed(err, "Fail to arm %s CQ channel from %s: %s",
                               send_cq ? "send" : "recv", _socket->description().c_str(),
                               berror(err));
        }
        // The logging and SetFailed() above may clobber errno.
        errno = err;
        return -1;
    }

    return 0;
}

void RqConnection::PollCq(Socket* m) {
    auto* context = static_cast<CqContext*>(m->user());
    if (!context) {
        return;
    }
    auto connection = context->connection;
    auto* c = connection.get();
    SocketUniquePtr s;
    if (Socket::Address(c->_sid, &s) < 0) {
        return;
    }
    auto* transport = static_cast<RdmaTransport*>(s->_transport.get());
    RdmaEndpoint* ep = transport->GetRdmaEp();
    if (m->id() != c->_cq_sid || !ep->OwnsConnection(c)) {
        return;
    }
    CHECK(c->_receiving_enabled);

    bool send = false;
    ibv_cq* cq = c->_resource->recv_cq;

    if (!FLAGS_rdma_use_polling) {
        if (c->GetAndAckEvents(s) < 0) {
            return;
        }
    } else {
        // Polling is considered as non-send, so no need to change `send'.
        // Only need to poll polling_cq.
        cq = c->_resource->polling_cq;
    }

    int progress = Socket::PROGRESS_INIT;
    bool notified = false;
    InputMessageClosure last_msg;
    ibv_wc wc[FLAGS_rdma_cqe_poll_once];
    while (true) {
        int cnt = ibv_poll_cq(cq, FLAGS_rdma_cqe_poll_once, wc);
        if (cnt < 0) {
            const int saved_errno = errno;
            PLOG(WARNING) << "Fail to poll cq: " << s->description();
            s->SetFailed(saved_errno, "Fail to poll cq from %s: %s",
                    s->description().c_str(), berror(saved_errno));
            return;
        }
        if (cnt == 0) {
            if (FLAGS_rdma_use_polling) {
                return;
            }

            if (!send) {
                // It's send cq's turn.
                send = true;
                cq = c->_resource->send_cq;
                continue;
            }
            // `recv_cq' and `send_cq' have been polled.
            if (!notified) {
                // Since RDMA only provides one shot event, we have to call the
                // notify function every time. Because there is a possibility
                // that the event arrives after the poll but before the notify,
                // we should re-poll the CQ once after the notify to check if
                // there is an available CQE.
                // The connection is already working in RDMA mode here, a
                // failed re-arm means no more CQ event will be reported,
                // which is fatal for this connection.
                if (c->ReqNotifyCq(true, true) != 0) {
                    return;
                }
                if (c->ReqNotifyCq(false, true) != 0) {
                    return;
                }
                notified = true;
                // Both CQs have just been re-armed, thus both of them must be
                // re-polled. Note that `cq' is `send_cq' here, so we have to
                // switch back to `recv_cq' explicitly. Otherwise only
                // `send_cq' would be re-polled, and a recv CQE arriving in
                // the window between the poll and the notify of `recv_cq'
                // would be left in the CQ without any following event
                // (one shot notification is not triggered by the CQE which
                // is already in the CQ before the arming), which stalls the
                // connection until the next CQE happens to come.
                send = false;
                cq = c->_resource->recv_cq;
                continue;
            }
            if (!m->MoreReadEvents(&progress)) {
                break;
            }

            if (c->GetAndAckEvents(s) != 0) {
                return;
            }

            // Restart polling from `recv_cq'.
            send = false;
            cq = c->_resource->recv_cq;
            notified = false;
            continue;
        }
        notified = false;

        ssize_t bytes = 0;
        for (int i = 0; i < cnt; ++i) {
            if (s->Failed()) {
                return;
            }

            if (wc[i].status != IBV_WC_SUCCESS) {
                PLOG(WARNING) << "Fail to handle RDMA completion, error status("
                              << wc[i].status << "): " << s->description();
                s->SetFailed(ERDMA, "RDMA completion error(%d) from %s: %s",
                             wc[i].status, s->description().c_str(), berror(ERDMA));
                continue;
            }

            ssize_t nr = c->HandleCompletion(wc[i]);
            if (nr < 0) {
                const int saved_errno = errno;
                PLOG(WARNING) << "Fail to handle RDMA completion: " << s->description();
                s->SetFailed(saved_errno, "Fail to handle rdma completion from %s: %s",
                             s->description().c_str(), berror(saved_errno));
            } else if (nr > 0) {
                bytes += nr;
            }
        }
        // Send CQE has no messages to process.
        if (send) {
            continue;
        }

        // Just call PrcessNewMessage once for all of these CQEs.
        // Otherwise it may call too many bthread_flush to affect performance.
        if (ep->Deliver(c, &c->_received, bytes, last_msg) < 0) {
            return;
        }
    }
}

void RqConnection::Debug(std::ostream& os, butil::StringPiece connector) const {
    os << "rdma_private_rq=ON"
       << connector << "rdma_sq_imm_window_size=" << _sq_imm_window_size
       << connector << "rdma_remote_rq_window_size=" << _remote_rq_window_size.load(butil::memory_order_relaxed)
       << connector << "rdma_sq_window_size=" << _sq_window_size.load(butil::memory_order_relaxed)
       << connector << "rdma_local_window_capacity=" << _local_window_capacity
       << connector << "rdma_remote_window_capacity=" << _remote_window_capacity
       << connector << "rdma_sbuf_head=" << _sq_current
       << connector << "rdma_sbuf_tail=" << _sq_sent
       << connector << "rdma_rbuf_head=" << _rq_received
       << connector << "rdma_unacked_rq_wr=" << _new_rq_wrs.load(butil::memory_order_relaxed)
       << connector << "rdma_received_ack=" << _accumulated_ack
       << connector << "rdma_unsolicited_sent=" << _unsolicited
       << connector << "rdma_unsignaled_sq_wr=" << _sq_unsignaled
       << connector << "rdma_pending_receive_bytes=" << _received.size();
}

int RqConnection::GlobalInitialize() {
    g_rdma_resource_mutex = new bthread::Mutex;
    for (int i = 0; !FLAGS_rdma_use_srq && i < FLAGS_rdma_prepared_qp_cnt; ++i) {
        RdmaResource* res = AllocateQpCq(FLAGS_rdma_prepared_qp_size,
                                         FLAGS_rdma_prepared_qp_size);
        if (!res) {
            return -1;
        }
        res->next = g_rdma_resource_list;
        g_rdma_resource_list = res;
    }

    if (FLAGS_rdma_use_polling) {
        _poller_groups = std::vector<PollerGroup>(FLAGS_task_group_ntags);
    }

    return 0;
}

void RqConnection::GlobalRelease() {
    if (g_rdma_resource_mutex) {
        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        while (g_rdma_resource_list) {
            RdmaResource* res = g_rdma_resource_list;
            g_rdma_resource_list = g_rdma_resource_list->next;
            DeleteRdmaResource(res);
        }
    }
    // release polling mode at exit or call RqConnection::PollingModeRelease
    // explicitly
    if (FLAGS_rdma_use_polling) {
        for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
            PollingModeRelease(i);
        }
    }
}

std::vector<RqConnection::PollerGroup> RqConnection::_poller_groups;

int RqConnection::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    if (!FLAGS_rdma_use_polling) {
        return 0;
    }
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return 0;
    }
    struct FnArgs {
        Poller* poller;
        butil::atomic<bool>* running;
    };
    auto fn = [](void* p) -> void* {
        std::unique_ptr<FnArgs> args(static_cast<FnArgs*>(p));
        auto poller = args->poller;
        auto running = args->running;
        std::unordered_set<SocketId> cq_sids;
        CqSidOp op;

        if (poller->init_fn) {
            poller->init_fn();
        }

        while (running->load(butil::memory_order_relaxed)) {
            while (poller->op_queue.Dequeue(op)) {
                if (op.type == CqSidOp::ADD) {
                    cq_sids.emplace(op.sid);
                } else if (op.type == CqSidOp::REMOVE) {
                    cq_sids.erase(op.sid);
                }
            }
            for (auto sid : cq_sids) {
                SocketUniquePtr s;
                if (Socket::Address(sid, &s) < 0) {
                    continue;
                }
                PollCq(s.get());
            }
            if (poller->callback) {
                poller->callback();
            }
            if (FLAGS_rdma_poller_yield) {
                bthread_yield();
            }
        }

        if (poller->release_fn) {
            poller->release_fn();
        }

        return nullptr;
    };
    for (int i = 0; i < FLAGS_rdma_poller_num; ++i) {
        auto args = new FnArgs{&pollers[i], &running};
        auto attr = FLAGS_rdma_disable_bthread ? BTHREAD_ATTR_PTHREAD
                                               : BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "RdmaPolling");
        pollers[i].callback = callback;
        pollers[i].init_fn = init_fn;
        pollers[i].release_fn = release_fn;
        auto rc = bthread_start_background(&pollers[i].tid, &attr, fn, args);
        if (rc != 0) {
            LOG(ERROR) << "Fail to start rdma polling bthread";
            return -1;
        }
    }
    return 0;
}

void RqConnection::PollingModeRelease(bthread_tag_t tag) {
    if (!FLAGS_rdma_use_polling) {
        return;
    }
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    running.store(false, butil::memory_order_relaxed);
    for (int i = 0; i < FLAGS_rdma_poller_num; ++i) {
        bthread_join(pollers[i].tid, nullptr);
    }
}

void RqConnection::PollerAddCqSid() {
    if (_cq_sid == INVALID_SOCKET_ID) {
        return;
    }

    _poller_tag = bthread_self_tag();
    auto index = butil::fmix32(_cq_sid) % FLAGS_rdma_poller_num;
    auto& group = _poller_groups[_poller_tag];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    poller.op_queue.Enqueue(CqSidOp{_cq_sid, CqSidOp::ADD});
}

void RqConnection::PollerRemoveCqSid() {
    if (_cq_sid == INVALID_SOCKET_ID || _poller_tag == BTHREAD_TAG_INVALID) {
        return;
    }

    auto index = butil::fmix32(_cq_sid) % FLAGS_rdma_poller_num;
    auto& group = _poller_groups[_poller_tag];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    poller.op_queue.Enqueue(CqSidOp{_cq_sid, CqSidOp::REMOVE});
    _poller_tag = BTHREAD_TAG_INVALID;
}

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
