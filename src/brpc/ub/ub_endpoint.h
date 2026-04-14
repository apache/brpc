//
// Created by z00926396 on 2026/4/11.
//

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
#include "brpc/ub/ub_helper.h"
#include "brpc/ub/ub_ring.h"


namespace brpc {
class Socket;
namespace ub {

DECLARE_int32(ub_poller_num);
DECLARE_bool(ub_edisp_unsched);
DECLARE_bool(ub_disable_bthread);

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
    void (*_done)(int, void*){NULL};
    void* _data{NULL};
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

    // Cut data from the given IOBuf list and use RDMA to send
    // Return bytes cut if success, -1 if failed and errno set
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can send more data
    bool IsWritable() const;

    void PollerRegisterEpollOut(bool pollin) {
        uint32_t events = EPOLLOUT | EPOLLET;
        if (pollin) {
            PollerRegisterEvent(CqSidOp::MOD, events | EPOLLIN);
            return;
        }
        PollerRegisterEvent(CqSidOp::ADD, events);
    }

    void PollerUnRegisterEpollOut(bool pollin) {
        uint32_t events = EPOLLIN | EPOLLET;
        if (pollin) {
            PollerRegisterEvent(CqSidOp::MOD, events);
            return;
        }
        PollerRegisterEvent(CqSidOp::REMOVE);
    }

    // Callback when there is new epollin event on TCP fd
    static void OnNewDataFromTcp(Socket* m);

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

private:
    enum State {
        UNINIT = 0x0,
        C_ALLOC_SHM = 0x1,
        C_HELLO_SEND = 0x2,
        C_HELLO_WAIT = 0x3,
        C_MAP_REMOTE_SHM = 0x4,
        C_ACK_SEND = 0x5,
        S_HELLO_WAIT = 0x11,
        S_ALLOC_SHM = 0x12,
        S_HELLO_SEND = 0x13,
        S_ACK_WAIT = 0x14,
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

    // Poll CQ and get the work completion
    static void PollIn(UBShmEndpoint* ep, uint32_t epEvent);

    static void PollOut(UBShmEndpoint* ep, uint32_t epEvent);

    // Try to read data on TCP fd in _socket
    inline void TryReadOnTcp();

    // Not owner
    Socket* _socket;

    State _state;

    // ub resource
    UBRing* _ub_ring{nullptr};

    SocketId _cq_sid;

    // butex for inform read events on TCP fd during handshake
    butil::atomic<int> *_read_butex;

    DISALLOW_COPY_AND_ASSIGN(UBShmEndpoint);

    struct CqSidOp {
        enum OpType {
            ADD,
            REMOVE,
            MOD
        };
        SocketId sid;
        uint32_t event;
        OpType type;
    };

    struct CqSidOpHash {
        std::size_t operator()(const CqSidOp& op) const {
            return op.sid;
        }
    };

    struct CqSidOpEqual {
        bool operator()(const CqSidOp& lhs, const CqSidOp& rhs) const {
            return lhs.sid == rhs.sid;
        }
    };

    // Poller instance
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
        butil::MPSCQueue<CqSidOp, butil::ObjectPoolAllocator<CqSidOp>> op_queue;
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

    void PollerRegisterEvent(CqSidOp::OpType op, uint32_t events = EPOLLET);
};

}  // namespace ub
}  // namespace brpc

#else  // if BRPC_WITH_UBRING

class UBShmEndpoint { };

#endif

#endif //BRPC_UB_ENDPOINT_H
