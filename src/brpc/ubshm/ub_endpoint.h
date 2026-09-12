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

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/containers/mpsc_queue.h"
#include "brpc/socket.h"
#include "brpc/ubshm/ub_helper.h"
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/shm/shm_def.h"


namespace brpc {
class Socket;
namespace ubring {

DECLARE_int32(ub_poller_num);
DECLARE_bool(ub_edisp_unsched);
DECLARE_bool(ub_disable_bthread);

enum UbrDataFormat {
    UBR_DATA_FORMAT_NONE = 0,
    UBR_DATA_FORMAT_LEGACY_64 = 1,
};

struct HelloFormatExtension {
    // The V3 format extension is a fixed-size frame. A different wire size
    // requires negotiation through a new hello version.
    static const uint16_t WIRE_SIZE = 4;

    uint16_t extension_len;
    uint16_t format_id;

    void Serialize(void* data) const;
    void Deserialize(const void* data);
};

struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(void* data);
    std::string toString() const;

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint64_t len;
    char shm_name[SHM_MAX_NAME_BUFF_LEN];
};

class UBConnect : public AppConnect {
public:
    void StartConnect(const Socket* socket,
            void (*done)(int err, void* data), void* data) override;
    void StopConnect(Socket*) override;
    struct RunGuard {
        RunGuard(UBConnect* rc) { this_rc = rc; }
        ~RunGuard() { if (this_rc) this_rc->Run(); }
        UBConnect* this_rc;
    };

private:
    void Run();
    void (*_done)(int, void*){nullptr};
    void* _data{nullptr};
};

class BAIDU_CACHELINE_ALIGNMENT UBShmEndpoint : public SocketUser {
friend class UBConnect;
friend class Socket;
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

    // Callback when there is new epollin event on TCP fd
    static void OnNewDataFromTcp(Socket* m);

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

#ifdef UNIT_TEST
public:
#else
private:
#endif
    enum State {
        UNINIT = 0x0,
        C_ALLOC_SHM = 0x1,
        C_HELLO_SEND = 0x2,
        C_HELLO_WAIT = 0x3,
        C_FORMAT_SEND = 0x4,
        C_FORMAT_WAIT = 0x5,
        C_MAP_REMOTE_SHM = 0x6,
        C_ACK_SEND = 0x7,
        S_HELLO_WAIT = 0x11,
        S_ALLOC_SHM = 0x12,
        S_HELLO_SEND = 0x13,
        S_FORMAT_WAIT = 0x14,
        S_FORMAT_SEND = 0x15,
        S_ACK_WAIT = 0x16,
        ESTABLISHED = 0x100,
        FALLBACK_TCP = 0x200,
        FAILED = 0x300
    };

    // Process handshake at the client
    static void* ProcessHandshakeAtClient(void* arg);

    // Process handshake at the server
    static void* ProcessHandshakeAtServer(void* arg);

    // Allocate resources
    // Return 0 if success, -1 if failed and errno set
    int AllocateClientResources(SHM* local_trx_shm, const char* shm_name);

    int AllocateServerResources(SHM* remote_trx_shm, SHM* local_trx_shm);

    // Release resources
    void DeallocateResources();

    // Read at most len bytes from fd in _socket to data
    // wait for _read_butex if encounter EAGAIN
    // return -1 if encounter other errno (including EOF)
    int ReadFromFd(void* data, size_t len);


    // Write at most len bytes from data to fd in _socket
    // wait for _epollout_butex if encounter EAGAIN
    // return -1 if encounter other errno
    int WriteToFd(void* data, size_t len);

    // Poll inbound and outbound UBRing events.
    static void PollIn(UBShmEndpoint* ep, uint32_t ep_event);

    static void PollOut(UBShmEndpoint* ep, uint32_t ep_event);

    // Try to read data on TCP fd in _socket
    inline void TryReadOnTcp();

    // Not owner
    Socket* _socket;
    SocketId _socket_id;

    State _state;
    UbrDataFormat _negotiated_data_format{UBR_DATA_FORMAT_NONE};

    // ub resource
    ubring::UBRing* _ub_ring{nullptr};

    // Synthetic SocketId registered with the UBRing poller.
    SocketId _poller_sid;

    // butex for inform read events on TCP fd during handshake
    butil::atomic<int> *_read_butex;

    DISALLOW_COPY_AND_ASSIGN(UBShmEndpoint);

    struct PollerSidOp {
        enum OpType {
            ADD,
            REMOVE,
            MOD
        };
        SocketId sid;
        uint32_t events;
        OpType type;
    };

    struct PollerSidOpHash {
        std::size_t operator()(const PollerSidOp& op) const {
            return op.sid;
        }
    };

    struct PollerSidOpEqual {
        bool operator()(const PollerSidOp& lhs, const PollerSidOp& rhs) const {
            return lhs.sid == rhs.sid;
        }
    };

    // Poller instance
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
        butil::MPSCQueue<
            PollerSidOp, butil::ObjectPoolAllocator<PollerSidOp>> op_queue;
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

    void PollerRegisterEvent(PollerSidOp::OpType op,
                             uint32_t events = EPOLLET);
};

}  // namespace ubring
}  // namespace brpc

#else  // if BRPC_WITH_UBRING

class UBShmEndpoint { };

#endif

#endif //BRPC_UB_ENDPOINT_H
