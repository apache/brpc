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

#include "brpc/urma/urma_endpoint.h"

#if BRPC_WITH_URMA

#include <sys/resource.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/time.h"
#include "bthread/bthread.h"

#include "urma_api.h"

#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/urma/urma_bonding.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_stream_trace.h"
#include "brpc/urma_transport.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
namespace urma {

// Flags used here are declared in urma_endpoint.h (urma_use_polling,
// urma_poller_num, urma_disable_bthread). Declare the rest here.
DECLARE_int32(urma_sq_size);
DECLARE_int32(urma_rq_size);
DECLARE_int32(urma_cqe_poll_once);
DECLARE_bool(urma_recv_zerocopy);
DECLARE_int32(urma_zerocopy_min_size);
DECLARE_int32(urma_prepared_jetty_cnt);
DECLARE_int32(urma_bonding_mode);
DECLARE_bool(urma_poller_yield);
DEFINE_bool(urma_trace_stream, false,
            "Capture the last 32 URMA stream events per connection and dump on "
            "parse/completion/post failure or teardown. Includes payload prefixes; "
            "enable before creating connections only for controlled diagnostics.");

static const size_t IOBUF_BLOCK_HEADER_LEN = sizeof(butil::IOBuf::Block);
// Match UMDK perftest's CTP SEND ceiling. This is a per-WR payload limit,
// independent of the pool block size and the device's generic max_msg_size.
static const size_t MAX_CTP_SEND_SIZE = 4096;

// ---- Globals: prepared jetty pool + poller groups ----
struct PreparedJetty {
    UrmaResource* res;
};
static butil::Mutex g_prepared_mutex;
static UrmaResource* g_prepared_list = nullptr;  // singly-linked
static int g_prepared_cnt = 0;

static int PreparedJettyCount() {
    const int requested =
        std::max(0, std::min(FLAGS_urma_prepared_jetty_cnt, 1024));
    if (requested == 0) {
        return 0;
    }

    struct rlimit nofile;
    if (getrlimit(RLIMIT_NOFILE, &nofile) != 0 ||
        nofile.rlim_cur == RLIM_INFINITY) {
        return requested;
    }

    // In event mode each prepared JFCE consumes a file descriptor. Keep room
    // for one TCP fd per future URMA connection and for brpc/system internals.
    static const rlim_t kReservedFdCount = 64;
    const rlim_t max_prepared =
        nofile.rlim_cur > kReservedFdCount
            ? (nofile.rlim_cur - kReservedFdCount) / 2
            : 0;
    if (max_prepared >= static_cast<rlim_t>(requested)) {
        return requested;
    }

    LOG(WARNING) << "Cap URMA prepared jetty count from " << requested
                 << " to " << max_prepared
                 << " due to RLIMIT_NOFILE=" << nofile.rlim_cur;
    return static_cast<int>(max_prepared);
}

std::vector<UrmaEndpoint::PollerGroup> UrmaEndpoint::_poller_groups;

// ============================================================================
// UrmaResource lifecycle.
// ============================================================================

UrmaResource::~UrmaResource() {
    if (remote_jetty) {
        urma_unimport_jetty(remote_jetty);
    }
    if (remote_seg) {
        urma_unimport_seg(remote_seg);
    }
    if (jetty) {
        urma_delete_jetty(jetty);
    }
    if (jfr) {
        urma_delete_jfr(jfr);
    }
    if (jfc) {
        urma_delete_jfc(jfc);
    }
    if (jfce) {
        urma_delete_jfce(jfce);
    }
}

// ============================================================================
// Constructor / destructor / Reset.
// ============================================================================

UrmaEndpoint::UrmaEndpoint(Socket* s)
    : _socket(s),
      _state(UNINIT),
      _resource(nullptr),
      _cq_lifetime(std::make_shared<CqLifetime>(s->id())) {
    _sq_size = static_cast<uint16_t>(
        std::max(16, std::min(4096, static_cast<int>(FLAGS_urma_sq_size))));
    _rq_size = static_cast<uint16_t>(
        std::max(16, std::min(4096, static_cast<int>(FLAGS_urma_rq_size))));
    _input_processor.Init(s, InputMessengerProcessor::STREAM_URMA_JETTY);
    if (FLAGS_urma_trace_stream) {
        _stream_trace.reset(new UrmaStreamTrace);
    }
}

UrmaEndpoint::~UrmaEndpoint() {
    DeallocateResources();
}

void UrmaEndpoint::Reset() {
    DeallocateResources();
    _stream_trace.reset(FLAGS_urma_trace_stream ? new UrmaStreamTrace : nullptr);
    _state = UNINIT;
    _remote_recv_block_size = 0;
    _local_window_capacity = 0;
    _remote_window_capacity = 0;
    _remote_rq_window_size.store(0, butil::memory_order_relaxed);
    _sq_window_size.store(0, butil::memory_order_relaxed);
    _new_rq_wrs.store(0, butil::memory_order_relaxed);
    _sq_imm_window_size = 0;
    _sq_current = 0;
    _sq_sent = 0;
    _rq_received = 0;
    _pending_received_bytes.store(0, butil::memory_order_relaxed);
    _input_processor.Reset();
    _sbuf.clear();
    _rbuf.clear();
    _rbuf_data.clear();
    _recv_completions.clear();
    _recv_completed.clear();
}

// ============================================================================
// Local connection parameters.
// ============================================================================

void UrmaEndpoint::GetLocalConnectionInfo(UrmaConnectionInfo* out) const {
    *out = UrmaConnectionInfo{};
    out->buffer_size = static_cast<uint32_t>(GetUrmaRecvBlockSize());
    out->recv_buffer_cnt = _rq_size - 1;
    if (_resource && _resource->jetty) {
        out->jetty_id = _resource->jetty->jetty_id.id;
        out->uasid = _resource->jetty->jetty_id.uasid;
        const urma_eid_t* local_eid = GetUrmaLocalEid();
        const uint8_t* advertised_eid =
            local_eid != nullptr
                ? local_eid->raw
                : _resource->jetty->jetty_id.eid.raw;
        std::memcpy(out->eid, advertised_eid, 16);
    }
    out->tp_type = static_cast<uint8_t>(URMA_CTP);
    // Pool segment: flatten g_pool_seg's seg fields.
    urma_target_seg_t* pool = GetPoolSegFor(nullptr);
    if (pool) {
        std::memcpy(out->seg_eid, pool->seg.ubva.eid.raw, 16);
        out->seg_uasid = pool->seg.ubva.uasid;
        out->seg_va = pool->seg.ubva.va;
        out->seg_len = pool->seg.len;
        out->seg_token_id = pool->seg.token_id;
    }
}

// ============================================================================
// Allocate / deallocate per-connection resources.
// ============================================================================

int UrmaEndpoint::AllocateResources() {
    if (_resource) {
        return 0;
    }
    urma_context_t* ctx = GetUrmaContext();
    if (!ctx) {
        errno = ENODEV;
        return -1;
    }

    _resource = new (std::nothrow) UrmaResource();
    if (!_resource) {
        return -1;
    }

    // Try the prepared pool first (sized sq/rq match).
    if (_sq_size <= static_cast<uint16_t>(FLAGS_urma_sq_size) &&
        _rq_size <= static_cast<uint16_t>(FLAGS_urma_rq_size)) {
        BAIDU_SCOPED_LOCK(g_prepared_mutex);
        if (g_prepared_list) {
            UrmaResource* next = g_prepared_list->next;
            delete _resource;
            _resource = g_prepared_list;
            g_prepared_list = next;
            _resource->next = nullptr;
            --g_prepared_cnt;
        }
    }

    if (!_resource->jfc) {
        // The SDK requires every JFC to reference a JFCE. Polling mode does
        // not arm or consume it, but still supplies the required object.
        _resource->jfce = urma_create_jfce(ctx);
        if (!_resource->jfce ||
            (!FLAGS_urma_use_polling && _resource->jfce->fd < 0)) {
            LOG(ERROR) << "Fail to create a usable URMA JFCE";
            errno = ENODEV;
            return -1;
        }

        urma_jfc_cfg_t jfc_cfg{};
        jfc_cfg.depth = static_cast<uint32_t>(_sq_size + _rq_size);
        jfc_cfg.jfce = _resource->jfce;
        _resource->jfc = urma_create_jfc(ctx, &jfc_cfg);
        if (!_resource->jfc) {
            PLOG(ERROR) << "urma_create_jfc";
            return -1;
        }

        urma_jfr_cfg_t jfr_cfg{};
        jfr_cfg.depth = static_cast<uint32_t>(_rq_size);
        jfr_cfg.trans_mode = URMA_TM_RM;
        jfr_cfg.max_sge = 1;
        jfr_cfg.min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER;
        jfr_cfg.jfc = _resource->jfc;
        _resource->jfr = urma_create_jfr(ctx, &jfr_cfg);
        if (!_resource->jfr) {
            PLOG(ERROR) << "urma_create_jfr";
            return -1;
        }

        urma_jetty_cfg_t jetty_cfg{};
        jetty_cfg.flag.bs.share_jfr = 1;
        jetty_cfg.jfs_cfg.depth = static_cast<uint32_t>(_sq_size);
        jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
        // WR placement/completion ordering requires an in-order JFS.
        jetty_cfg.jfs_cfg.flag.bs.outorder_comp = 0;
        jetty_cfg.jfs_cfg.priority = GetUrmaJettyPriority();
        jetty_cfg.jfs_cfg.max_sge =
            static_cast<uint8_t>(GetUrmaMaxSge());
        jetty_cfg.jfs_cfg.rnr_retry = URMA_TYPICAL_RNR_RETRY;
        jetty_cfg.jfs_cfg.err_timeout = URMA_TYPICAL_ERR_TIMEOUT;
        jetty_cfg.jfs_cfg.jfc = _resource->jfc;
        jetty_cfg.shared.jfr = _resource->jfr;
        jetty_cfg.shared.jfc = _resource->jfc;
        _resource->jetty = urma_create_jetty(ctx, &jetty_cfg);
        if (!_resource->jetty) {
            PLOG(ERROR) << "urma_create_jetty";
            return -1;
        }
    }

    _sbuf.resize(_sq_size - RESERVED_WR_NUM);
    _rbuf.resize(_rq_size);
    _rbuf_data.resize(_rq_size, nullptr);
    _recv_completions.resize(_rq_size);
    _recv_completed.assign(_rq_size, false);

    // Arm before advertising receives. Completions may queue during the TCP
    // handshake, but must not be dispatched until its parser has returned.
    if (!FLAGS_urma_use_polling && ReqNotifyCq() != 0) {
        return -1;
    }
    return 0;
}

int UrmaEndpoint::StartCqEvents() {
    std::unique_lock<bthread::Mutex> lock(_cq_lifetime->mutex);
    if (_socket->parsing_stream_type() != InputMessengerProcessor::STREAM_NONE) {
        errno = EPROTO;
        return -1;
    }
    if (_cq_sid != INVALID_SOCKET_ID) {
        return 0;
    }
    if (!_resource || !_resource->jfc) {
        errno = ENODEV;
        return -1;
    }
    const bool polling = FLAGS_urma_use_polling;
    if (!polling) {
        if (!_resource->jfce || _resource->jfce->fd < 0) {
            LOG(ERROR) << "Prepared URMA resource has no usable JFCE";
            errno = ENODEV;
            return -1;
        }
    }
    // Install the owned user only after Create succeeds, and before publishing
    // the fd/id to a dispatcher. Create failures then cannot leak/double-delete
    // the user or close the provider-owned JFCE fd.
    SocketOptions options;
    options.keytable_pool = _socket->keytable_pool();
    options.on_edge_triggered_events = PollCq;
    if (Socket::Create(options, &_cq_sid) < 0) {
        PLOG(ERROR) << "Fail to create CQ socket";
        return -1;
    }
    SocketUniquePtr carrier;
    CHECK_EQ(0, Socket::Address(_cq_sid, &carrier));
    carrier->_user = new CqSocketUser(_cq_lifetime, this, polling);
    if (!polling && carrier->ResetFileDescriptor(_resource->jfce->fd) != 0) {
        const int saved_errno = errno;
        carrier->SetFailed();
        _cq_sid = INVALID_SOCKET_ID;
        errno = saved_errno;
        return -1;
    }
    _cq_lifetime->closed = false;
    if (polling) {
        PollerAddCqSid();
    }
    return 0;
}

void UrmaEndpoint::DeallocateResources() {
    std::unique_lock<bthread::Mutex> lock(_cq_lifetime->mutex);
    _cq_lifetime->closed = true;
    ++_cq_lifetime->generation;
    if (!_resource) {
        return;
    }
    DumpStreamTrace("connection teardown");

    // epoll DEL and poller REMOVE do not drain callbacks. The lifetime mutex
    // waits for an active callback; the closed generation rejects queued ones.
    if (_cq_sid != INVALID_SOCKET_ID) {
        SocketUniquePtr s;
        if (Socket::AddressFailedAsWell(_cq_sid, &s) >= 0) {
            if (static_cast<CqSocketUser*>(s->user())->polling) {
                PollerRemoveCqSid();
            }
            if (s->fd() >= 0) {
                s->_io_event.RemoveConsumer(s->_fd);
            }
            s->_fd = -1;  // Already removed fd from epoll.
            s->SetFailed();
        }
        _cq_sid = INVALID_SOCKET_ID;
    }

    // Reusing a Jetty requires a driver-supported RESET plus a complete JFC
    // drain. Until that lifecycle is implemented, prepared resources are
    // one-shot: they accelerate connection setup but are destroyed on close.
    delete _resource;
    _resource = nullptr;
}

// ============================================================================
// ImportPeer: the critical import_seg-before-import_jetty sequence.
// ============================================================================

int UrmaEndpoint::ImportPeer(const UrmaConnectionInfo& peer) {
    urma_context_t* ctx = GetUrmaContext();
    if (!ctx) {
        errno = ENODEV;
        return -1;
    }

    // 1. urma_import_seg FIRST so the kernel establishes TP routing for the
    //    remote EID. Without this the first SEND is rejected by hardware with
    //    URMA_CR_RNR_RETRY_CNT_EXC_ERR.
    urma_seg_t peer_seg{};
    std::memcpy(peer_seg.ubva.eid.raw, peer.seg_eid, 16);
    peer_seg.ubva.uasid = peer.seg_uasid;
    peer_seg.ubva.va = peer.seg_va;
    peer_seg.len = peer.seg_len;
    peer_seg.token_id = peer.seg_token_id;
    urma_token_t seg_token{};
    urma_import_seg_flag_t seg_flag{};
    seg_flag.bs.cacheable = URMA_NON_CACHEABLE;
    seg_flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;
    seg_flag.bs.mapping = URMA_SEG_NOMAP;
    _resource->remote_seg = urma_import_seg(ctx, &peer_seg, &seg_token, 0, seg_flag);
    if (!_resource->remote_seg) {
        PLOG(ERROR) << "urma_import_seg failed";
        return -1;
    }

    // 2. urma_import_jetty.
    urma_rjetty_t remote{};
    std::memcpy(remote.jetty_id.eid.raw, peer.eid, 16);
    remote.jetty_id.uasid = peer.uasid;
    remote.jetty_id.id = peer.jetty_id;
    remote.trans_mode = URMA_TM_RM;
    remote.type = URMA_JETTY;
    if (peer.tp_type > static_cast<uint8_t>(URMA_UTP)) {
        errno = EPROTO;
        return -1;
    }
    remote.tp_type = static_cast<urma_tp_type_t>(peer.tp_type);

    urma_token_t token{};
    const bool use_bonding_extension =
        NeedsUrmaBondingJettyExtension(IsUrmaBondingDevice(),
                                       FLAGS_urma_bonding_mode) &&
        remote.trans_mode == URMA_TM_RM;
    errno = 0;
    if (use_bonding_extension) {
#if BRPC_URMA_HAS_BONDING_EXT
        // Non-standalone bonding needs the local jetty to associate its send
        // path with the imported target. A plain import may return success
        // without setting that association, leaving traffic one-way only.
        bondp_rjetty_t bonding_remote{};
        bonding_remote.base = remote;
        bonding_remote.base.flag.bs.has_drv_ext = 1;
        bonding_remote.jetty = _resource->jetty;
        _resource->remote_jetty =
            urma_import_jetty(ctx, &bonding_remote.base, &token);
#else
        LOG(ERROR) << "Bonding remote jetty import requires provider header "
                      "urma_ubagg.h";
        errno = ENOTSUP;
#endif
    } else {
        _resource->remote_jetty = urma_import_jetty(ctx, &remote, &token);
    }
    if (!_resource->remote_jetty) {
        if (errno == 0) {
            errno = EIO;
        }
        char remote_eid[URMA_EID_STR_LEN + 1] = {};
        std::snprintf(remote_eid, sizeof(remote_eid), EID_FMT,
                      EID_RAW_ARGS(peer.eid));
        PLOG(ERROR) << "urma_import_jetty failed"
                    << " remote_eid=" << remote_eid
                    << " remote_uasid=" << peer.uasid
                    << " remote_jetty_id=" << peer.jetty_id
                    << " trans_mode=" << remote.trans_mode
                    << " tp_type=" << remote.tp_type
                    << " bonding_extension=" << use_bonding_extension;
        return -1;
    }
    return 0;
}

// ============================================================================
// Send / recv data path.
// ============================================================================

// Private IOBuf accessor mirroring RdmaIOBuf: reach into IOBuf block refs to
// build a urma_sge_t directly, without memcpy.
class UrmaIOBuf : private butil::IOBuf {
    friend class ::brpc::urma::UrmaEndpoint;
public:
    using butil::IOBuf::_ref_num;
    using butil::IOBuf::_ref_at;
    using butil::IOBuf::fetch1;
    using butil::IOBuf::get_first_data_meta;
    using butil::IOBuf::cutn;
    // Build the SGE for the current head block.
    // Returns bytes added, or -1 (errno set).
    ssize_t cut_into_sglist(urma_sge_t* sglist, size_t* sge_index,
                            butil::IOBuf* to, size_t max_sge,
                            size_t max_len) {
        size_t len = 0;
        while (*sge_index < max_sge && len < max_len && _ref_num() != 0) {
            butil::IOBuf::BlockRef const& r = _ref_at(0);
            const void* start = fetch1();
            urma_target_seg_t* tseg =
                GetPoolSegFor(const_cast<void*>(start));
            if (!tseg) {
                // User-registered memory: look up the seg handle.
                uint64_t meta = get_first_data_meta();
                if (meta != 0) {
                    tseg = reinterpret_cast<urma_target_seg_t*>(
                        static_cast<uintptr_t>(meta));
                }
            }
            if (!tseg) {
                errno = ERDMAMEM;
                return -1;
            }
            size_t this_len = r.length;
            if (len + this_len > max_len) {
                this_len = max_len - len;
            }
            sglist[*sge_index].addr = reinterpret_cast<uint64_t>(start);
            sglist[*sge_index].len = static_cast<uint32_t>(this_len);
            sglist[*sge_index].tseg = tseg;
            sglist[*sge_index].user_tseg = nullptr;
            cutn(to, this_len);
            len += this_len;
            (*sge_index)++;
        }
        return static_cast<ssize_t>(len);
    }
};

ssize_t UrmaEndpoint::CutFromIOBufList(butil::IOBuf** from, size_t ndata) {
    if (!_resource || !_resource->jetty || !_resource->remote_jetty) {
        errno = ENOTCONN;
        return -1;
    }
    int max_sge = GetUrmaMaxSge();
    if (max_sge < 1) {
        max_sge = 1;
    }

    urma_sge_t* sglist = static_cast<urma_sge_t*>(
        alloca(sizeof(urma_sge_t) * max_sge));
    if (!sglist) {
        errno = ENOMEM;
        return -1;
    }

    size_t current = 0;
    ssize_t total_len = 0;
    while (current < ndata) {
        uint16_t remote_wnd = _remote_rq_window_size.load(butil::memory_order_relaxed);
        uint16_t sq_wnd = _sq_window_size.load(butil::memory_order_acquire);
        if (remote_wnd == 0 || sq_wnd == 0) {
            if (total_len > 0) {
                break;
            }
            errno = EAGAIN;
            return -1;
        }
        butil::IOBuf* to = &_sbuf[_sq_current];
        size_t sge_index = 0;
        size_t this_len = 0;
        size_t max_len = _remote_recv_block_size > 0
                             ? _remote_recv_block_size
                             : GetUrmaRecvBlockSize();
        // UrmaTransport advertises CTP in both hello versions. Limit the sum
        // of all SGEs, not each SGE, while retaining larger receive buffers.
        max_len = std::min(max_len, MAX_CTP_SEND_SIZE);
        while (sge_index < static_cast<size_t>(max_sge) &&
               this_len < max_len && current < ndata) {
            auto* data = reinterpret_cast<UrmaIOBuf*>(from[current]);
            if (data->empty()) {
                ++current;
                continue;
            }
            ssize_t n = data->cut_into_sglist(sglist, &sge_index, to,
                                              max_sge, max_len - this_len);
            if (n < 0) {
                return -1;
            }
            this_len += n;
        }
        if (sge_index == 0) {
            break;
        }

        urma_sg_t sg{sglist, static_cast<uint32_t>(sge_index)};
        urma_jfs_wr_t wr{};
        std::memset(&wr, 0, sizeof(wr));
        // Send payload with URMA_OPC_SEND. Receive credits are flushed
        // separately by SendImm() after SendAck() reaches its threshold.
        // Piggybacking credits turns every payload into SEND_IMM and can
        // produce asymmetric completions with the bonding provider.
        wr.opcode = URMA_OPC_SEND;
        // A WR may end in the middle of an RPC (e.g. a 229-byte prefix
        // followed by a 2-byte payload). RM reliability alone is not a byte
        // stream ordering contract: zero place_order/comp_order permits the
        // later WR to be observed first. Keep placement and completions in
        // posting order, including the intervening credit-only SEND_IMM WRs.
        wr.flag.bs.place_order = URMA_STRONG_ORDER;
        wr.flag.bs.comp_order = 1;
        wr.flag.bs.complete_enable = 1;
        wr.tjetty = _resource->remote_jetty;
        wr.send.src = sg;
        wr.user_ctx = 1;
        urma_jfs_wr_t* bad_wr = nullptr;
        const uint16_t sq_slot = _sq_current;
        const uint32_t local_jetty_id = _resource->jetty->jetty_id.id;
        const uint32_t remote_jetty_id = _resource->remote_jetty->id.id;
        for (size_t i = 0; i < sge_index; ++i) {
            const urma_sge_t& sge = sglist[i];
            const uint64_t seg_base = sge.tseg->seg.ubva.va;
            const uint64_t seg_len = sge.tseg->seg.len;
            const bool in_range = sge.addr >= seg_base &&
                sge.len <= seg_len && sge.addr - seg_base <= seg_len - sge.len;
            VLOG(1) << "URMA send SGE: index=" << i
                    << " addr=" << reinterpret_cast<const void*>(sge.addr)
                    << " len=" << sge.len
                    << " tseg=" << static_cast<const void*>(sge.tseg)
                    << " seg_base=" << reinterpret_cast<const void*>(seg_base)
                    << " seg_len=" << seg_len
                    << " in_range=" << in_range
                    << " from_pool=" << (GetPoolSegFor(
                        reinterpret_cast<void*>(sge.addr)) == sge.tseg)
                    << " context_matches="
                    << (sge.tseg->urma_ctx == GetUrmaContext())
                    << " local_jetty_id=" << local_jetty_id
                    << " sq_slot=" << sq_slot;
        }

        // Reserve both credits before making the WR visible to the provider.
        // In polling mode a completion (and even the peer's receive-credit
        // ACK) can be processed by another thread before post_send returns.
        // Decrementing after post therefore creates a transient capacity + 1
        // window and makes the strict credit check tear down a healthy
        // connection.
        _remote_rq_window_size.fetch_sub(1, butil::memory_order_relaxed);
        _sq_window_size.fetch_sub(1, butil::memory_order_relaxed);
        if (_stream_trace) {
            _stream_trace->Record(UrmaStreamTrace::TX, sq_slot, wr.user_ctx, 0,
                                  sglist, sge_index);
        }
        int rc = urma_post_jetty_send_wr(_resource->jetty, &wr, &bad_wr);
        if (rc != URMA_SUCCESS) {
            const int provider_errno = errno;
            _remote_rq_window_size.fetch_add(1, butil::memory_order_relaxed);
            _sq_window_size.fetch_add(1, butil::memory_order_relaxed);
            LOG(WARNING) << "urma_post_jetty_send_wr failed: " << rc
                         << ", provider_errno=" << provider_errno
                         << " (" << berror(provider_errno) << ')'
                         << ", bad_wr=" << static_cast<const void*>(bad_wr)
                         << ", bad_is_current=" << (bad_wr == &wr)
                         << ", sq_slot=" << sq_slot
                         << ", local_jetty_id=" << local_jetty_id
                         << ", remote_jetty_id=" << remote_jetty_id
                         << ", state=" << GetStateStr()
                         << ", sq_window=" << sq_wnd
                         << ", remote_rq_window=" << remote_wnd
                         << ", num_sge=" << sge_index
                         << ", configured_max_sge=" << GetUrmaMaxSge()
                         << ", payload_size=" << this_len
                         << " on " << _socket->description();
            errno = rc;
            DumpStreamTrace("data post failed");
            return -1;
        }
        _sq_current = (_sq_current + 1) % (_sq_size - RESERVED_WR_NUM);
        total_len += static_cast<ssize_t>(this_len);
    }
    return total_len;
}

bool UrmaEndpoint::IsWritable() const {
    return _remote_rq_window_size.load(butil::memory_order_relaxed) > 0 &&
           _sq_window_size.load(butil::memory_order_acquire) > 0;
}

// ============================================================================
// Recv path.
// ============================================================================

int UrmaEndpoint::DoPostRecv(void* block, size_t block_size) {
    urma_target_seg_t* tseg = GetPoolSegFor(block);
    if (!tseg) {
        errno = ERDMAMEM;
        return -1;
    }
    urma_sge_t sge{reinterpret_cast<uint64_t>(block),
                   static_cast<uint32_t>(block_size), tseg, nullptr};
    urma_sg_t sg{&sge, 1};
    urma_jfr_wr_t wr{sg, static_cast<uint64_t>(_rq_received) + 1, nullptr};
    urma_jfr_wr_t* bad = nullptr;
    // Use the shared-JFR path on every device, including bonding. The bonding
    // provider owns physical receive scheduling for the JFR; a local
    // jetty-to-target association is not part of the RM receive API.
    const urma_status_t status =
        urma_post_jfr_wr(_resource->jfr, &wr, &bad);
    if (status != URMA_SUCCESS) {
        LOG(WARNING) << "Failed to post URMA receive WR: status=" << status
                     << " bonding=" << IsUrmaBondingDevice()
                     << " bad_wr=" << static_cast<const void*>(bad)
                     << " bad_is_current=" << (bad == &wr)
                     << " local_jetty_id=" << _resource->jetty->jetty_id.id
                     << " provider_associated_remote="
                     << static_cast<const void*>(
                            _resource->jetty->remote_jetty)
                     << " state=" << GetStateStr()
                     << " on " << _socket->description();
        errno = status;
        return -1;
    }
    return 0;
}

int UrmaEndpoint::PostRecv(uint32_t num, bool zerocopy) {
    for (uint32_t i = 0; i < num; ++i) {
        size_t block_size = GetUrmaRecvBlockSize();
        if (zerocopy) {
            _rbuf[_rq_received].clear();
            butil::IOBufAsZeroCopyOutputStream zcis(
                &_rbuf[_rq_received], block_size + IOBUF_BLOCK_HEADER_LEN);
            void* data = nullptr;
            int size = 0;
            if (!zcis.Next(&data, &size) || !data ||
                size < static_cast<int>(block_size)) {
                errno = ENOMEM;
                return -1;
            }
            _rbuf_data[_rq_received] = data;
            if (DoPostRecv(data, block_size) < 0) {
                return -1;
            }
        } else {
            if (_rbuf_data[_rq_received] == nullptr) {
                _rbuf[_rq_received].clear();
                butil::IOBufAsZeroCopyOutputStream zcos(
                    &_rbuf[_rq_received],
                    block_size + IOBUF_BLOCK_HEADER_LEN);
                void* data = nullptr;
                int size = 0;
                if (!zcos.Next(&data, &size) || !data ||
                    size < static_cast<int>(block_size)) {
                    errno = ENOMEM;
                    return -1;
                }
                _rbuf_data[_rq_received] = data;
            }
            if (DoPostRecv(_rbuf_data[_rq_received], block_size) < 0) {
                return -1;
            }
        }
        _rq_received = (_rq_received + 1) % _rq_size;
    }
    return 0;
}

int UrmaEndpoint::SendImm(uint32_t imm) {
    if (imm == 0) {
        return 0;
    }
    if (!_resource || !_resource->jetty || !_resource->remote_jetty) {
        errno = ENOTCONN;
        return -1;
    }
    if (_sq_imm_window_size == 0) {
        errno = EAGAIN;
        return -1;
    }
    // Empty-payload SEND_IMM flushes peer-side receive credit. Connection
    // lifetime is owned by the TCP fd, so this is not an EOF marker.
    urma_jfs_wr_t wr{};
    std::memset(&wr, 0, sizeof(wr));
    wr.opcode = URMA_OPC_SEND_IMM;
    // ACKs consume the same receive queue as data. They must participate in
    // its ordering contract rather than allowing a later payload to overtake.
    wr.flag.bs.place_order = URMA_STRONG_ORDER;
    wr.flag.bs.comp_order = 1;
    wr.flag.bs.complete_enable = 1;
    wr.flag.bs.solicited_enable = 1;
    wr.tjetty = _resource->remote_jetty;
    wr.send.imm_data = imm;
    wr.user_ctx = 0;  // 0 == pure ack (HandleCompletion reuses budget).
    urma_jfs_wr_t* bad = nullptr;
    // Reserve the ACK-only SQ slot before posting for the same reason as the
    // data windows in CutFromIOBufList: polling may observe its completion as
    // soon as the provider accepts the WR.
    --_sq_imm_window_size;
    if (_stream_trace) {
        _stream_trace->Record(UrmaStreamTrace::TX, 0, wr.user_ctx, imm, nullptr, 0);
    }
    const urma_status_t status =
        urma_post_jetty_send_wr(_resource->jetty, &wr, &bad);
    if (status != URMA_SUCCESS) {
        const int provider_errno = errno;
        ++_sq_imm_window_size;
        _new_rq_wrs.fetch_add(imm, butil::memory_order_relaxed);
        LOG(WARNING) << "Failed to post URMA credit ACK: status=" << status
                     << " provider_errno=" << provider_errno
                     << " (" << berror(provider_errno) << ')'
                     << " bad_wr=" << static_cast<const void*>(bad)
                     << " bad_is_current=" << (bad == &wr)
                     << " imm=" << imm
                     << " local_jetty_id="
                     << _resource->jetty->jetty_id.id
                     << " remote_jetty_id="
                     << _resource->remote_jetty->id.id
                     << " state=" << GetStateStr()
                     << " on " << _socket->description();
        errno = status;
        DumpStreamTrace("credit ACK post failed");
        return -1;
    }
    return 0;
}

int UrmaEndpoint::SendAck(int num) {
    const uint16_t old =
        _new_rq_wrs.fetch_add(num, butil::memory_order_relaxed);
    if (old + num > _remote_window_capacity / 2 &&
        _sq_imm_window_size > 0) {
        return SendImm(_new_rq_wrs.exchange(0, butil::memory_order_relaxed));
    }
    return 0;
}

ssize_t UrmaEndpoint::HandleCompletion(const urma_cr_t& cr) {
    if (cr.status != URMA_CR_SUCCESS) {
        LOG(WARNING) << "URMA completion failed, status=" << cr.status
                     << " kind=" << (cr.flag.bs.s_r == 0 ? "send" : "recv")
                     << " user_ctx=" << cr.user_ctx
                     << " completion_len=" << cr.completion_len
                     << " local_id=" << cr.local_id
                     << " state=" << GetStateStr()
                     << " on " << _socket->description();
        errno = EIO;
        return -1;
    }
    if (cr.flag.bs.s_r == 0) {
        // Send completion: reclaim SQ window and wake the writer.
        if (cr.user_ctx == 0) {
            // Pure-ack WR: just replenish the imm budget.
            if (_sq_imm_window_size >= RESERVED_WR_NUM) {
                LOG(WARNING)
                    << "URMA credit-ACK completion exceeds reserved SQ "
                       "window: current="
                    << _sq_imm_window_size
                    << " capacity=" << RESERVED_WR_NUM
                    << " on " << _socket->description();
                errno = EPROTO;
                return -1;
            }
            _sq_imm_window_size += 1;
            SendAck(0);
            return 0;
        }
        uint16_t wnd = 1;  // We signal every WR (complete_enable=1).
        const uint16_t old =
            _sq_window_size.load(butil::memory_order_relaxed);
        if (old >= _local_window_capacity) {
            LOG(WARNING)
                << "URMA send completion exceeds SQ window: old=" << old
                << " increment=" << wnd
                << " capacity=" << _local_window_capacity
                << " user_ctx=" << cr.user_ctx
                << " on " << _socket->description();
            errno = EPROTO;
            return -1;
        }
        for (uint16_t i = 0; i < wnd; ++i) {
            _sbuf[_sq_sent].clear();
            _sq_sent = (_sq_sent + 1) % (_sq_size - RESERVED_WR_NUM);
        }
        // Only the serialized CQ consumer returns credits. Reclaim the slot
        // before publishing it to the writer; its acquire load must observe
        // clear() before appending another payload to the same IOBuf.
        _sq_window_size.fetch_add(wnd, butil::memory_order_release);
        if (_remote_rq_window_size.load(butil::memory_order_relaxed) >=
            _local_window_capacity / 8) {
            _socket->WakeAsEpollOut();
        }
        return 0;
    }
    // Identify the posted buffer rather than assuming CQ order equals RQ
    // order. Otherwise an ACK completion can advance the ring past a data WR
    // whose completion is still pending, corrupting the RPC byte stream.
    if (cr.user_ctx == 0 || cr.user_ctx > _rq_size) {
        LOG(WARNING) << "Invalid URMA receive slot: " << cr.user_ctx;
        errno = EPROTO;
        return -1;
    }
    const size_t slot = cr.user_ctx - 1;
    if (_stream_trace && cr.completion_len <= GetUrmaRecvBlockSize()) {
        urma_sge_t sge{reinterpret_cast<uint64_t>(_rbuf_data[slot]),
                       cr.completion_len, nullptr, nullptr};
        _stream_trace->Record(UrmaStreamTrace::RX_CQ, slot, cr.user_ctx,
                              cr.opcode == URMA_CR_OPC_SEND_WITH_IMM ? cr.imm_data : 0,
                              &sge, 1);
    }
    if (_recv_completed[slot]) {
        LOG(WARNING) << "Duplicate URMA receive completion: slot=" << slot;
        errno = EPROTO;
        return -1;
    }
    _recv_completions[slot] = cr;
    _recv_completed[slot] = true;
    if (slot != _rq_received) {
        VLOG(1) << "URMA deferred receive completion: slot=" << slot
                << " expected_slot=" << _rq_received
                << " completion_len=" << cr.completion_len
                << " on " << _socket->description();
    }
    ssize_t bytes = 0;
    while (_recv_completed[_rq_received]) {
        const urma_cr_t next = _recv_completions[_rq_received];
        _recv_completed[_rq_received] = false;
        const ssize_t received = HandleRecvCompletion(next);
        if (received < 0) {
            return -1;
        }
        bytes += received;
    }
    return bytes;
}

ssize_t UrmaEndpoint::HandleRecvCompletion(const urma_cr_t& cr) {
    bool zerocopy = FLAGS_urma_recv_zerocopy;
    if (cr.opcode == URMA_CR_OPC_SEND_WITH_IMM && cr.imm_data > 0) {
        if (cr.imm_data > _local_window_capacity) {
            LOG(WARNING) << "Invalid URMA receive credit: " << cr.imm_data;
            errno = EPROTO;
            return -1;
        }
        const uint16_t acks = static_cast<uint16_t>(cr.imm_data);
        uint16_t old =
            _remote_rq_window_size.load(butil::memory_order_relaxed);
        while (true) {
            if (old > _local_window_capacity - acks) {
                LOG(WARNING)
                    << "URMA receive credit exceeds window: old=" << old
                    << " credit=" << acks
                    << " capacity=" << _local_window_capacity
                    << " imm=" << cr.imm_data
                    << " remote_window_capacity="
                    << _remote_window_capacity
                    << " on " << _socket->description();
                errno = EPROTO;
                return -1;
            }
            if (_remote_rq_window_size.compare_exchange_weak(
                    old, static_cast<uint16_t>(old + acks),
                    butil::memory_order_relaxed)) {
                break;
            }
        }
        if (_sq_window_size.load(butil::memory_order_relaxed) > 0) {
            _socket->WakeAsEpollOut();
        }
    } else if (cr.completion_len == 0) {
        LOG(WARNING) << "Zero-length URMA receive without immediate credit";
        errno = EPROTO;
        return -1;
    }
    if (cr.completion_len > GetUrmaRecvBlockSize()) {
        LOG(WARNING) << "URMA completion exceeds receive buffer: "
                     << cr.completion_len;
        errno = EPROTO;
        return -1;
    }
    if (cr.completion_len < static_cast<uint32_t>(FLAGS_urma_zerocopy_min_size)) {
        zerocopy = false;
    }
    butil::IOPortal& read_buf = _input_processor.read_buf();
    if (_stream_trace) {
        urma_sge_t sge{reinterpret_cast<uint64_t>(_rbuf_data[_rq_received]),
                       cr.completion_len, nullptr, nullptr};
        _stream_trace->Record(UrmaStreamTrace::RX_USE, _rq_received,
                              cr.user_ctx,
                              cr.opcode == URMA_CR_OPC_SEND_WITH_IMM ? cr.imm_data : 0,
                              &sge, 1);
    }
    if (zerocopy) {
        _rbuf[_rq_received].cutn(&read_buf, cr.completion_len);
    } else {
        read_buf.append(_rbuf_data[_rq_received], cr.completion_len);
    }
    if (PostRecv(1, zerocopy) < 0) {
        return -1;
    }
    if (cr.completion_len > 0) {
        SendAck(1);
    }
    return static_cast<ssize_t>(cr.completion_len);
}

void UrmaEndpoint::DispatchReceivedBytes(SocketUniquePtr& s, ssize_t bytes) {
    int64_t pending = _pending_received_bytes.load(butil::memory_order_relaxed);
    if (bytes > 0) {
        pending = _pending_received_bytes.fetch_add(
                      bytes, butil::memory_order_acq_rel) + bytes;
    }

    const State state = _state.load(butil::memory_order_acquire);
    if (state != ESTABLISHED) {
        return;
    }

    // PollCq and the handshake bthread can both reach this method when the
    // state changes to ESTABLISHED. Serialize them so each byte added to the
    // URMA input processor is reported to InputMessenger exactly once.
    std::unique_lock<butil::Mutex> dispatch_lock(_dispatch_mutex);
    if (_state.load(butil::memory_order_acquire) != ESTABLISHED) {
        return;
    }
    pending = _pending_received_bytes.exchange(
        0, butil::memory_order_acq_rel);
    if (pending <= 0 || s->Failed()) {
        return;
    }

    if (!s->user()) {
        LOG(ERROR) << "URMA socket has no InputMessenger: "
                   << s->description();
        return;
    }

    const int64_t received_us = butil::cpuwide_time_us();
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    InputMessageClosure last_msg;
    const int result = _input_processor.ProcessNewMessage(
        static_cast<ssize_t>(pending), false, received_us, base_realtime, last_msg);
    if (result < 0 && _stream_trace) {
        char prefix[UrmaStreamTrace::PREFIX_SIZE];
        const size_t copied = _input_processor.read_buf().copy_to(prefix, sizeof(prefix));
        DumpStreamTrace("RPC parse failed", UrmaStreamTrace::HexPrefix(prefix, copied),
                        _input_processor.read_buf().size());
    }
}

void UrmaEndpoint::DumpStreamTrace(const char* reason, const std::string& prefix,
                                  size_t buffered) {
    if (!_stream_trace) {
        return;
    }
    const int saved_errno = errno;
    const std::string records = _stream_trace->DumpOnce();
    if (!records.empty()) {
        LOG(WARNING) << "URMA_TRACE reason=" << reason
                     << " buffered=" << buffered << " parse_prefix=" << prefix
                     << " on " << _socket->description() << '\n' << records;
    }
    errno = saved_errno;
}

void UrmaEndpoint::PollCq(Socket* m) {
    // The caller pins the carrier, not the endpoint. Its user/context remains
    // alive even when the main Socket has already been recycled.
    auto* user = static_cast<CqSocketUser*>(m->user());
    if (!user) {
        return;
    }
    const auto lifetime = user->lifetime;
    SocketUniquePtr s;
    if (Socket::Address(lifetime->socket_id, &s) != 0) {
        return;
    }
    // Declare the lock after s: unlock before releasing the last main Socket
    // reference, which can synchronously call DeallocateResources again.
    std::unique_lock<bthread::Mutex> lock(lifetime->mutex);
    if (lifetime->closed || user->generation != lifetime->generation ||
        s->Failed()) {
        return;
    }
    auto* ep = user->endpoint;

    const bool event_mode = !user->polling;
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        urma_jfc_t* event_jfc = nullptr;
        if (event_mode) {
            const int event_count = ep->WaitCqEvent(s, &event_jfc);
            if (event_count < 0) {
                return;
            }
            if (event_count == 0) {
                if (!m->MoreReadEvents(&progress)) {
                    return;
                }
                continue;
            }
        }

        ssize_t bytes = 0;
        auto drain_cq = [&]() -> int {
            while (true) {
                const int n =
                    std::max(1, std::min<int>(FLAGS_urma_cqe_poll_once, 32));
                urma_cr_t crs[32];
                const int cnt =
                    urma_poll_jfc(ep->_resource->jfc, n, crs);
                if (cnt < 0) {
                    return EIO;
                }
                if (cnt == 0) {
                    return 0;
                }
                for (int i = 0; i < cnt; ++i) {
                    if (s->Failed()) {
                        return ECANCELED;
                    }
                    const ssize_t nr = ep->HandleCompletion(crs[i]);
                    if (nr < 0) {
                        return errno ? errno : EIO;
                    }
                    bytes += nr;
                }
            }
        };

        int completion_error = drain_cq();
        if (event_mode) {
            // The bonding provider records which physical JFCs produced CRs
            // while bondp_poll_jfc drains the virtual JFC.
            // bondp_rearm_jfc consumes that mask, so rearming before the drain
            // leaves those physical JFCs unarmed.
            uint32_t nevents = 1;
            urma_ack_jfc(&event_jfc, &nevents, 1);
            if (completion_error == 0) {
                if (ep->ReqNotifyCq() != 0) {
                    return;
                }

                // Close the drain/rearm race. A completion that arrived while
                // the JFC was unarmed may not produce an edge on every
                // provider. The JFC is armed now, so a final nonblocking drain
                // is safe.
                completion_error = drain_cq();
            }
        }

        if (completion_error != 0) {
            ep->DumpStreamTrace("completion processing failed");
            if (!s->Failed()) {
                s->SetFailed(completion_error, "URMA completion error");
            }
            return;
        }
        ep->DispatchReceivedBytes(s, bytes);

        if (!event_mode) {
            return;
        }
        // The bonding JFCE fd is itself an epoll fd aggregating physical
        // JFCEs, while brpc watches it with EPOLLET. urma_wait_jfc(..., 1, ...)
        // consumes only one aggregated event. Keep draining the inner JFCE
        // until it reports no event; otherwise another physical event can
        // leave the fd continuously readable and never create a new outer
        // edge. The event_count == 0 branch above resets _nevent only after
        // the inner queue is empty.
    }
}

// ============================================================================
// ApplyRemoteInfo: size the send/recv windows from peer connection parameters.
// ============================================================================

void UrmaEndpoint::ApplyRemoteInfo(const UrmaConnectionInfo& remote) {
    _remote_recv_block_size = remote.buffer_size;
    const uint32_t peer_rq_size = remote.recv_buffer_cnt + 1;
    const uint32_t local_capacity =
        std::min<uint32_t>(_sq_size, peer_rq_size);
    _local_window_capacity = static_cast<uint16_t>(
        local_capacity > RESERVED_WR_NUM
            ? local_capacity - RESERVED_WR_NUM
            : 0);
    _remote_window_capacity =
        _rq_size > RESERVED_WR_NUM ? _rq_size - RESERVED_WR_NUM : 0;
    _sq_imm_window_size = RESERVED_WR_NUM;
    _remote_rq_window_size.store(_local_window_capacity,
                                 butil::memory_order_relaxed);
    _sq_window_size.store(_local_window_capacity, butil::memory_order_relaxed);
}

// ============================================================================
// Debug / polling-mode stubs.
// ============================================================================

std::string UrmaEndpoint::GetStateStr() const {
    switch (_state.load(butil::memory_order_acquire)) {
    case UNINIT:        return "UNINIT";
    case ESTABLISHED:   return "ESTABLISHED";
    case FALLBACK_TCP:  return "FALLBACK_TCP";
    case FAILED:        return "FAILED";
    }
    return "UNKNOWN";
}

void UrmaEndpoint::DebugInfo(std::ostream& os, butil::StringPiece) const {
    os << "state=" << GetStateStr()
       << " sq_size=" << _sq_size << " rq_size=" << _rq_size
       << " remote_recv_block_size=" << _remote_recv_block_size
       << " sq_window=" << _sq_window_size.load(butil::memory_order_relaxed)
       << " remote_rq_window=" << _remote_rq_window_size.load(butil::memory_order_relaxed)
       << " read_buf=" << _input_processor.read_buf().size();
}

int UrmaEndpoint::WaitCqEvent(SocketUniquePtr& s,
                              urma_jfc_t** event_jfc) {
    if (!_resource || !_resource->jfce || !_resource->jfc) {
        errno = ENODEV;
        return -1;
    }
    *event_jfc = nullptr;
    int count = urma_wait_jfc(_resource->jfce, 1, 0, event_jfc);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        const int saved_errno = errno;
        PLOG(ERROR) << "Fail to wait URMA JFC event from "
                    << s->description();
        s->SetFailed(saved_errno, "Fail to wait URMA JFC event: %s",
                     berror(saved_errno));
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (*event_jfc != _resource->jfc) {
        LOG(ERROR) << "Unexpected URMA JFC event on " << s->description();
        errno = EPROTO;
        s->SetFailed(EPROTO, "Unexpected URMA JFC event");
        return -1;
    }
    return 1;
}

int UrmaEndpoint::ReqNotifyCq() {
    if (!_resource || !_resource->jfc) {
        errno = ENODEV;
        return -1;
    }
    const int rc = urma_rearm_jfc(_resource->jfc, false);
    if (rc != URMA_SUCCESS) {
        errno = rc;
        PLOG(WARNING) << "Fail to rearm URMA JFC";
        _socket->SetFailed(rc, "Fail to rearm URMA JFC: %s", berror(rc));
        return -1;
    }
    return 0;
}

int UrmaEndpoint::PollingModeInitialize(
        bthread_tag_t tag, std::function<void()> callback,
        std::function<void()> init_fn, std::function<void()> release_fn) {
    if (!FLAGS_urma_use_polling) {
        return 0;
    }
    if (tag >= _poller_groups.size() ||
        _poller_groups[tag].pollers.empty()) {
        errno = EINVAL;
        return -1;
    }
    auto& group = _poller_groups[tag];
    bool expected = false;
    if (!group.running.compare_exchange_strong(expected, true)) {
        return 0;
    }
    struct FnArgs {
        Poller* poller;
        butil::atomic<bool>* running;
    };
    auto fn = [](void* p) -> void* {
        std::unique_ptr<FnArgs> args(static_cast<FnArgs*>(p));
        Poller* poller = args->poller;
        butil::atomic<bool>* running = args->running;
        std::unordered_set<SocketId> cq_sids;
        CqSidOp op;

        if (poller->init_fn) {
            poller->init_fn();
        }
        while (running->load(butil::memory_order_relaxed)) {
            while (poller->op_queue.Dequeue(op)) {
                if (op.type == CqSidOp::ADD) {
                    cq_sids.emplace(op.sid);
                } else {
                    cq_sids.erase(op.sid);
                }
            }
            for (SocketId sid : cq_sids) {
                SocketUniquePtr s;
                if (Socket::Address(sid, &s) == 0) {
                    PollCq(s.get());
                }
            }
            if (poller->callback) {
                poller->callback();
            }
            if (FLAGS_urma_poller_yield || cq_sids.empty()) {
                bthread_yield();
            }
        }
        if (poller->release_fn) {
            poller->release_fn();
        }
        return nullptr;
    };

    auto& pollers = group.pollers;
    for (size_t i = 0; i < pollers.size(); ++i) {
        pollers[i].callback = callback;
        pollers[i].init_fn = init_fn;
        pollers[i].release_fn = release_fn;
        std::unique_ptr<FnArgs> args(new (std::nothrow)
                                        FnArgs{&pollers[i], &group.running});
        if (!args) {
            group.running.store(false, butil::memory_order_relaxed);
            for (size_t j = 0; j < i; ++j) {
                bthread_join(pollers[j].tid, nullptr);
                pollers[j].tid = INVALID_BTHREAD;
            }
            errno = ENOMEM;
            return -1;
        }
        bthread_attr_t attr = FLAGS_urma_disable_bthread
                                  ? BTHREAD_ATTR_PTHREAD
                                  : BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "UrmaPolling");
        const int rc = bthread_start_background(
            &pollers[i].tid, &attr, fn, args.get());
        if (rc != 0) {
            group.running.store(false, butil::memory_order_relaxed);
            for (size_t j = 0; j < i; ++j) {
                bthread_join(pollers[j].tid, nullptr);
                pollers[j].tid = INVALID_BTHREAD;
            }
            errno = rc;
            return -1;
        }
        args.release();
    }
    return 0;
}

void UrmaEndpoint::PollingModeRelease(bthread_tag_t tag) {
    if (!FLAGS_urma_use_polling || tag >= _poller_groups.size()) {
        return;
    }
    auto& group = _poller_groups[tag];
    group.running.store(false, butil::memory_order_relaxed);
    for (auto& poller : group.pollers) {
        if (poller.tid != INVALID_BTHREAD) {
            bthread_join(poller.tid, nullptr);
            poller.tid = INVALID_BTHREAD;
        }
    }
}

void UrmaEndpoint::PollerAddCqSid() {
    if (_cq_sid == INVALID_SOCKET_ID || _poller_groups.empty()) {
        return;
    }
    _poller_tag = bthread_self_tag();
    if (_poller_tag >= _poller_groups.size()) {
        return;
    }
    auto& pollers = _poller_groups[_poller_tag].pollers;
    if (pollers.empty()) {
        return;
    }
    const size_t index =
        butil::fmix32(_cq_sid) % pollers.size();
    pollers[index].op_queue.Enqueue(
        CqSidOp{CqSidOp::ADD, _cq_sid});
}

void UrmaEndpoint::PollerRemoveCqSid() {
    if (_cq_sid == INVALID_SOCKET_ID || _poller_groups.empty() ||
        _poller_tag >= _poller_groups.size()) {
        return;
    }
    auto& pollers = _poller_groups[_poller_tag].pollers;
    if (pollers.empty()) {
        return;
    }
    const size_t index =
        butil::fmix32(_cq_sid) % pollers.size();
    pollers[index].op_queue.Enqueue(
        CqSidOp{CqSidOp::REMOVE, _cq_sid});
}

int UrmaEndpoint::GlobalInitialize() {
    // Pre-allocate the prepared jetty pool. Skipped if URMA init is skipped
    // (unit-test mode).
    if (FLAGS_urma_use_polling && _poller_groups.empty()) {
        if (FLAGS_urma_poller_num <= 0) {
            LOG(ERROR) << "urma_poller_num must be positive";
            errno = EINVAL;
            return -1;
        }
        size_t ntags = static_cast<size_t>(FLAGS_task_group_ntags);
        if (ntags == 0) {
            ntags = 1;
        }
        _poller_groups = std::vector<PollerGroup>(ntags);
    }
    if (g_prepared_cnt > 0) {
        return 0;
    }
    urma_context_t* ctx = GetUrmaContext();
    if (!ctx) {
        return 0;
    }
    const int prepared_jetty_count = PreparedJettyCount();
    for (int i = 0; i < prepared_jetty_count; ++i) {
        auto* r = new (std::nothrow) UrmaResource();
        if (!r) {
            break;
        }
        r->jfce = urma_create_jfce(ctx);
        if (!r->jfce ||
            (!FLAGS_urma_use_polling && r->jfce->fd < 0)) {
            delete r;
            break;
        }
        urma_jfc_cfg_t jfc_cfg{};
        jfc_cfg.depth = static_cast<uint32_t>(FLAGS_urma_sq_size + FLAGS_urma_rq_size);
        jfc_cfg.jfce = r->jfce;
        r->jfc = urma_create_jfc(ctx, &jfc_cfg);
        if (!r->jfc) {
            delete r;
            break;
        }
        urma_jfr_cfg_t jfr_cfg{};
        jfr_cfg.depth = static_cast<uint32_t>(FLAGS_urma_rq_size);
        jfr_cfg.trans_mode = URMA_TM_RM;
        jfr_cfg.max_sge = 1;
        jfr_cfg.min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER;
        jfr_cfg.jfc = r->jfc;
        r->jfr = urma_create_jfr(ctx, &jfr_cfg);
        if (!r->jfr) {
            delete r;
            break;
        }
        urma_jetty_cfg_t jetty_cfg{};
        jetty_cfg.flag.bs.share_jfr = 1;
        jetty_cfg.jfs_cfg.depth = static_cast<uint32_t>(FLAGS_urma_sq_size);
        jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
        jetty_cfg.jfs_cfg.flag.bs.outorder_comp = 0;
        jetty_cfg.jfs_cfg.priority = GetUrmaJettyPriority();
        jetty_cfg.jfs_cfg.max_sge =
            static_cast<uint8_t>(GetUrmaMaxSge());
        jetty_cfg.jfs_cfg.rnr_retry = URMA_TYPICAL_RNR_RETRY;
        jetty_cfg.jfs_cfg.err_timeout = URMA_TYPICAL_ERR_TIMEOUT;
        jetty_cfg.jfs_cfg.jfc = r->jfc;
        jetty_cfg.shared.jfr = r->jfr;
        jetty_cfg.shared.jfc = r->jfc;
        r->jetty = urma_create_jetty(ctx, &jetty_cfg);
        if (!r->jetty) {
            delete r;
            break;
        }
        r->next = g_prepared_list;
        g_prepared_list = r;
        ++g_prepared_cnt;
    }
    return 0;
}

void UrmaEndpoint::GlobalRelease() {
    {
        BAIDU_SCOPED_LOCK(g_prepared_mutex);
        while (g_prepared_list) {
            UrmaResource* next = g_prepared_list->next;
            delete g_prepared_list;
            g_prepared_list = next;
        }
        g_prepared_cnt = 0;
    }
    for (size_t tag = 0; tag < _poller_groups.size(); ++tag) {
        PollingModeRelease(static_cast<bthread_tag_t>(tag));
    }
}

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA
