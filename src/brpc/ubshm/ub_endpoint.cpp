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

#if BRPC_WITH_UBRING

#include <errno.h>

#include "brpc/errno.pb.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/ub_endpoint.h"
#include "brpc/ubshm/ub_helper.h"
#include "brpc/ubshm/ubr_trx.h"
#include "brpc/ubshm_transport.h"
#include "bthread/bthread.h"
#include "butil/logging.h" // CHECK, LOG
#include <gflags/gflags.h>


DECLARE_int32(task_group_ntags);

namespace brpc {
DECLARE_bool(log_connection_close);
namespace ubring {

extern bool g_skip_ub_init;
DEFINE_int32(ub_poller_num, 1, "Poller number in ub polling mode.");
DEFINE_bool(ub_poller_yield, false, "Yield thread in RDMA polling mode.");
DEFINE_bool(ub_edisp_unsched, false, "Disable event dispatcher schedule");
DEFINE_bool(ub_disable_bthread, false, "Disable bthread in RDMA");

static const size_t MIN_ONCE_READ = 4096;
static const size_t MAX_ONCE_READ = 524288;
static const size_t IOBUF_IOV_MAX = 256;

static butil::Mutex *g_ubring_resource_mutex = NULL;

UBShmEndpoint::UBShmEndpoint(Socket* s)
    : _socket(s), _socket_id(s ? s->id() : INVALID_SOCKET_ID),
      _ub_ring(nullptr), _poller_sid(INVALID_SOCKET_ID) {}

UBShmEndpoint::~UBShmEndpoint() { Reset(); }

void UBShmEndpoint::Reset() {
    DeallocateResources();

    delete _ub_ring;
    _ub_ring = nullptr;
    _poller_sid = INVALID_SOCKET_ID;
}

bool UBShmEndpoint::IsWritable() const {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // Just for UT
        return false;
    }
    auto ret = _ub_ring->IsUbrTrxWriteable(EPOLLET);
    if (ret == 0) {
        return true;
    }
    return false;
}

ssize_t UBShmEndpoint::CutFromIOBufList(butil::IOBuf** from, size_t ndata) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // Just for UT
        errno = EAGAIN;
        return -1;
    }
    if (BAIDU_UNLIKELY(ndata == 0)) {
        return 0;
    }
    struct iovec vec[IOBUF_IOV_MAX];
    size_t nvec = 0;
    for (size_t i = 0; i < ndata; ++i) {
        const butil::IOBuf* p = from[i];
        const size_t nref = p->backing_block_num();
        for (size_t j = 0; j < nref && nvec < IOBUF_IOV_MAX; ++j, ++nvec) {
            butil::StringPiece sp = p->backing_block(j);
            vec[nvec].iov_base = const_cast<char*>(sp.data());
            vec[nvec].iov_len = sp.size();
        }
    }

    ssize_t nw = 0;
    errno = 0;
    nw = _ub_ring->UbrTrxWritev(vec, nvec);
    if (UNLIKELY(nw == -1)) {
        if (errno == EMSGSIZE) {
      LOG(ERROR) << "Non-blocking send msg failed, message is larger than "
                    "ubring capacity.";
        } else {
      LOG(ERROR)
          << "Non-blocking send msg in failed, connection has been closed.";
            errno = EPIPE;
        }
    } else if (UNLIKELY(nw == UBRING_RETRY)) {
        errno = EAGAIN;
        nw = -1;
    }
    if (nw <= 0) {
        return nw;
    }
    size_t npop_all = nw;
    for (size_t i = 0; i < ndata; ++i) {
        npop_all -= from[i]->pop_front(npop_all);
        if (npop_all == 0) {
            break;
        }
    }
    return nw;
}

int UBShmEndpoint::AllocateClientResources(ubring::SHM *local_trx_shm,
                                           const char *shm_name) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // For UT
        return 0;
    }

    CHECK(_ub_ring == nullptr);
    // TODO: Pooling management
    _ub_ring = new UBRing();

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _socket->_keytable_pool;
    if (Socket::Create(options, &_poller_sid) < 0) {
    const int saved_errno = errno;
        PLOG(WARNING) << "Fail to create socket for UBRing poller";
    delete _ub_ring;
    _ub_ring = NULL;
    _poller_sid = INVALID_SOCKET_ID;
    errno = saved_errno;
        return -1;
    }
    int ret = _ub_ring->UbrAllocateLocalShm(local_trx_shm, shm_name);
    if (ret != 0) {
    const int saved_errno = errno;
    DeallocateResources();
    delete _ub_ring;
    _ub_ring = NULL;
    _poller_sid = INVALID_SOCKET_ID;
    errno = saved_errno;
        return ret;
    }
    PollerRegisterEvent(PollerSidOp::ADD, EPOLLIN);
    return 0;
}

int UBShmEndpoint::AllocateServerResources(ubring::SHM *remote_trx_shm,
                                           ubring::SHM *local_trx_shm) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // For UT
        return 0;
    }

    CHECK(_ub_ring == nullptr);
    // TODO: Pooling management
    _ub_ring = new UBRing();

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _socket->_keytable_pool;
    if (Socket::Create(options, &_poller_sid) < 0) {
    const int saved_errno = errno;
        PLOG(WARNING) << "Fail to create socket for UBRing poller";
    delete _ub_ring;
    _ub_ring = NULL;
    _poller_sid = INVALID_SOCKET_ID;
    errno = saved_errno;
        return -1;
    }
    int ret = _ub_ring->UbrAllocateServerShm(remote_trx_shm, local_trx_shm);
    if (ret != 0) {
    const int saved_errno = errno;
    DeallocateResources();
    delete _ub_ring;
    _ub_ring = NULL;
    _poller_sid = INVALID_SOCKET_ID;
    errno = saved_errno;
        return ret;
    }
    PollerRegisterEvent(PollerSidOp::ADD, EPOLLIN);
    return ret;
}

void UBShmEndpoint::DeallocateResources() {
    if (!_ub_ring) {
        return;
    }
    PollerRegisterEvent(PollerSidOp::REMOVE);
    _ub_ring->UbrTrxClose();
    if (INVALID_SOCKET_ID != _poller_sid) {
        SocketUniquePtr s;
        if (Socket::Address(_poller_sid, &s) == 0) {
            s->_user = nullptr;
            s->_fd = -1;
            s->SetFailed();
        }
    }
}

void UBShmEndpoint::PollIn(UBShmEndpoint* ep, uint32_t ep_event) {
    SocketUniquePtr s;
    if (Socket::Address(ep->_socket_id, &s) < 0) {
        return;
    }
  UBShmTransport *ub_transport = UBShmTransport::Get(s.get());
    CHECK(ep == ub_transport->_ub_ep);

    InputMessageClosure last_msg;
    while (true) {
        int ret = ep->_ub_ring->IsUbrTrxReadable(ep_event);
        if (ret < 0) {
            return;
        }

        InputMessengerProcessor& processor = s->fd_input_processor();
        bool read_eof = false;
        while (!read_eof) {
            const int64_t received_us = butil::cpuwide_time_us();
            const int64_t base_realtime = butil::gettimeofday_us() - received_us;

            const ssize_t nr = processor.read_buf().append_from_reader(
                ep->_ub_ring, processor.OnceReadSize());
            if (nr <= 0) {
                if (0 == nr) {
                    // Set `read_eof' flag and proceed to feed EOF into `Protocol'
          // (implied by an empty processor.read_buf()), which may produce a new
          // `InputMessageBase' under some protocols such as HTTP
          LOG_IF(WARNING, FLAGS_log_connection_close)
              << *s << " was closed by remote side";
                    read_eof = true;
                } else if (errno != EAGAIN) {
                    if (errno == EINTR) {
                        continue;
                    }
                    const int saved_errno = errno;
                    PLOG(WARNING) << "Fail to read from " << *s;
                    s->SetFailed(saved_errno, "Fail to read from %s: %s",
                                 s->description().c_str(), berror(saved_errno));
                    return;
                } else {
                    return;
                }
            }

            if (processor.ProcessNewMessage(nr, read_eof, received_us,
                                            base_realtime, last_msg) < 0) {
                return;
            }
        }

        if (read_eof) {
            s->SetEOF();
        }
    }
}

void UBShmEndpoint::PollOut(UBShmEndpoint* ep, uint32_t ep_event) {
    SocketUniquePtr s;
    if (Socket::Address(ep->_socket_id, &s) < 0) {
        return;
    }
  UBShmTransport *ub_transport = UBShmTransport::Get(s.get());
    CHECK(ep == ub_transport->_ub_ep);
    if (ep->IsWritable()) {
        s->WakeAsEpollOut();
    }
}

int UBShmEndpoint::GlobalInitialize() {
    g_ubring_resource_mutex = new butil::Mutex;
    _poller_groups = std::vector<PollerGroup>(FLAGS_task_group_ntags);
    return 0;
}

void UBShmEndpoint::GlobalRelease() {
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        PollingModeRelease(i);
    }
}

std::vector<UBShmEndpoint::PollerGroup> UBShmEndpoint::_poller_groups;

int UBShmEndpoint::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return 0;
    }
    struct FnArgs {
        Poller* poller;
        std::atomic<bool>* running;
    };
    auto fn = [](void* p) -> void* {
        std::unique_ptr<FnArgs> args(static_cast<FnArgs*>(p));
        auto poller = args->poller;
        auto running = args->running;
    std::unordered_set<PollerSidOp, PollerSidOpHash, PollerSidOpEqual> cq_sids;
        PollerSidOp op;

        if (poller->init_fn) {
            poller->init_fn();
        }
        while (running->load(std::memory_order_relaxed)) {
            while (poller->op_queue.Dequeue(op)) {
                if (op.type == PollerSidOp::ADD) {
          cq_sids.emplace(op);
                } else if (op.type == PollerSidOp::REMOVE) {
          cq_sids.erase(op);

                } else if (op.type == PollerSidOp::MOD) {
          cq_sids.erase(op);
          cq_sids.emplace(op);
                }
            }
      for (auto cq : cq_sids) {
                SocketUniquePtr s;
        if (Socket::Address(cq.sid, &s) < 0) {
                    continue;
                }
                UBShmEndpoint* ep = static_cast<UBShmEndpoint*>(s->user());
                if (!ep) {
                    continue;
                }

        if (cq.event & EPOLLIN) {
          PollIn(ep, cq.event);
                }

        if (cq.event & EPOLLOUT) {
          PollOut(ep, cq.event);
                }
            }
            if (poller->callback) {
                poller->callback();
            }
            if (FLAGS_ub_poller_yield) {
                bthread_yield();
            }
        }

        if (poller->release_fn) {
            poller->release_fn();
        }

        return nullptr;
    };
    for (int i = 0; i < FLAGS_ub_poller_num; ++i) {
        auto args = new FnArgs{&pollers[i], &running};
    auto attr =
        FLAGS_ub_disable_bthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "UBPolling");
        pollers[i].callback = callback;
        pollers[i].init_fn = init_fn; 
        pollers[i].release_fn = release_fn;
        auto rc = bthread_start_background(&pollers[i].tid, &attr, fn, args);
        if (rc != 0) {
            LOG(ERROR) << "Fail to start ubring polling bthread";
            return -1;
        }
    }
    return 0;
}

void UBShmEndpoint::PollingModeRelease(bthread_tag_t tag) {
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    running.store(false, std::memory_order_relaxed);
    for (int i = 0; i < FLAGS_ub_poller_num; ++i) {
        bthread_join(pollers[i].tid, nullptr);
    }
}

void UBShmEndpoint::PollerRegisterEvent(PollerSidOp::OpType op, uint32_t events) {
    auto index = butil::fmix32(_poller_sid) % FLAGS_ub_poller_num;
    auto& group = _poller_groups[bthread_self_tag()];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    if (INVALID_SOCKET_ID != _poller_sid) {
        poller.op_queue.Enqueue(PollerSidOp{_poller_sid, events, op});
    }
}

}  // namespace ubring
}  // namespace brpc

#endif  // if BRPC_WITH_UBRING
