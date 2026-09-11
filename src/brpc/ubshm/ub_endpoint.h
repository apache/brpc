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

#ifndef BRPC_UB_ENDPOINT_H
#define BRPC_UB_ENDPOINT_H

#if BRPC_WITH_UBRING

#include "brpc/handshake/ubshm_handshake.h"
#include "brpc/socket.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/ub_helper.h"
#include "brpc/ubshm/ub_ring.h"
#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include <functional>
#include <vector>

namespace brpc {
class Socket;
class UBShmTransport;
namespace handshake {
class UBShmServerHandshakeAdapter;
}
namespace ubring {

DECLARE_int32(ub_poller_num);
DECLARE_bool(ub_edisp_unsched);
DECLARE_bool(ub_disable_bthread);

class BAIDU_CACHELINE_ALIGNMENT UBShmEndpoint : public SocketUser {
friend class Socket;
  friend class ::brpc::UBShmTransport;
  friend class ::brpc::handshake::UBShmServerHandshakeAdapter;

public:
    explicit UBShmEndpoint(Socket* s);
    ~UBShmEndpoint() override;

    // Global initialization
    // Return 0 if success, -1 if failed and errno set
    static int GlobalInitialize();

    static void GlobalRelease();

    // Reset the endpoint (for next use)
    void Reset();

    // Cut data from the given IOBuf list and use UBRING to send
    // Return bytes cut if success, -1 if failed and errno set
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can send more data
    bool IsWritable() const;

    void PollerRegisterEpollOut(bool pollin) {
        uint32_t events = EPOLLOUT | EPOLLET;
        if (pollin) {
            PollerRegisterEvent(PollerSidOp::MOD, events | EPOLLIN);
            return;
        }
        PollerRegisterEvent(PollerSidOp::ADD, events);
    }

    void PollerUnRegisterEpollOut(bool pollin) {
        uint32_t events = EPOLLIN | EPOLLET;
        if (pollin) {
            PollerRegisterEvent(PollerSidOp::MOD, events);
            return;
        }
        PollerRegisterEvent(PollerSidOp::REMOVE);
    }

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

private:
    // Allocate resources
    // Return 0 if success, -1 if failed and errno set
    int AllocateClientResources(SHM* local_trx_shm, const char* shm_name);

    int AllocateServerResources(SHM* remote_trx_shm, SHM* local_trx_shm);

    // Release resources
    void DeallocateResources();

  // Poll CQ and get the work completion
    static void PollIn(UBShmEndpoint* ep, uint32_t ep_event);

    static void PollOut(UBShmEndpoint* ep, uint32_t ep_event);

    // Not owner
    Socket* _socket;
    SocketId _socket_id;

    // ub resource
    ubring::UBRing* _ub_ring{nullptr};

    SocketId _poller_sid;

    DISALLOW_COPY_AND_ASSIGN(UBShmEndpoint);

    struct PollerSidOp {
    enum OpType { ADD, REMOVE, MOD };
        SocketId sid;
    uint32_t event;
        OpType type;
    };

    struct PollerSidOpHash {
    std::size_t operator()(const PollerSidOp &op) const { return op.sid; }
    };

    struct PollerSidOpEqual {
        bool operator()(const PollerSidOp& lhs, const PollerSidOp& rhs) const {
            return lhs.sid == rhs.sid;
        }
    };

    // Poller instance
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
    butil::MPSCQueue<PollerSidOp, butil::ObjectPoolAllocator<PollerSidOp>> op_queue;
        // Callback used for io_uring/spdk etc
        std::function<void()> callback;
        // Init and Destroy function
        std::function<void()> init_fn;
        std::function<void()> release_fn;
    };
    // Poller group
    struct BAIDU_CACHELINE_ALIGNMENT PollerGroup {
        PollerGroup() : pollers(FLAGS_ub_poller_num), running(false) {}
        std::vector<Poller> pollers;
        std::atomic<bool> running;
    };
    static std::vector<PollerGroup> _poller_groups;

  void PollerRegisterEvent(PollerSidOp::OpType op, uint32_t events = EPOLLET);
};

}  // namespace ubring
}  // namespace brpc

#else  // if BRPC_WITH_UBRING

class UBShmEndpoint { };

#endif

#endif //BRPC_UB_ENDPOINT_H
