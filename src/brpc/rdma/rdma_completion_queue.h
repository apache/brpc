// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMA_COMPLETION_QUEUE_H
#define BRPC_RDMA_COMPLETION_QUEUE_H

#include <vector>                                   // std::vector
#include "butil/macros.h"                           // DISALLOW_COPY_AND_ASSIGN
#include "bthread/bthread.h"                        // butil::Mutex
#include "brpc/input_messenger.h"
#include "brpc/socket.h"

namespace brpc {
namespace rdma {

// Event type of verbs
enum RdmaEvent {
    RDMA_EVENT_UNKNOWN,
    RDMA_EVENT_SEND,            // opcode==IBV_WC_SEND
    RDMA_EVENT_WRITE,           // opcode==IBV_WC_WRITE
    RDMA_EVENT_RECV,            // opcode==IBV_WC_RECV
    RDMA_EVENT_RECV_WITH_IMM,   // opcode==IBV_WC_RECV_RDMA_WITH_IMM
    RDMA_EVENT_ERROR            // status!=IBV_WC_SUCCESS
};

// A wrapper of ibv_wc
struct BAIDU_CACHELINE_ALIGNMENT RdmaCompletion {
    RdmaEvent type;
    uint32_t len;           // byte_len in ibv_wc
    uint32_t imm;           // imm_data in ibv_wc
};

// A wrapper for CQ processing.
class RdmaCompletionQueue : public SocketUser {
public:
    RdmaCompletionQueue();
    ~RdmaCompletionQueue();

    // Whether the CQs are shared
    static bool IsShared();

    // Create one non-shared CQ, the given s refer to the caller's Socket
    static RdmaCompletionQueue* NewOne(Socket* s, int cq_size);

    // Get a shared CQ
    static RdmaCompletionQueue* GetOne();

    // Initialize the CQ on the given cpu core id.
    // Return 0 if success, -1 if failed.
    int Init(int cpu);

    // Destroy the non-shared CQ or dereference the shared CQ
    void Release();

    // Stop and join PollCQ thread
    void StopAndJoin();

    // Get CQ
    void* GetCQ() const;

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaCompletionQueue);

    // Call PollCQ in it
    static void* PollThis(void* arg);

    // PollCQ thread
    static void PollCQ(Socket* m);

    // Handle all completions in one bthread when shared CQ is enabled
    static void* HandleCompletions(void* arg);

    // Handle the given completion
    static int HandleCompletion(RdmaCompletion* rc, Socket* s,
            InputMessenger::InputMessageClosure& last_msg);

    // Clean the resources, not including the PollCQ thread
    void CleanUp();

    // Get and ACK the CQ events.
    // Return 0 if success, -1 if failed.
    int GetAndAckEvents();

    // CQ
    void* _cq;

    // CQ Channel
    void* _cq_channel;

    // The capacity size of CQ
    int _cq_size;

    // The number of cq events unacked
    int _cq_events;

    // The cpu index of this completion queue
    int _cpu_index;

    // PollCQ thread, only used for shared CQ in polling mode
    bthread_t _tid;

    // SocketId for receiving the event
    SocketId _sid;

    // SocketId for RdmaEndpoint
    SocketId _ep_sid;

    // Keytable_pool
    bthread_keytable_pool_t* _keytable_pool;

    // If the poll CQ thread should be stopped
    bool _stop;
};

// The policy to assign a CQ to a connection (in shared CQ case)
// or a core (in non-shared CQ case).
class RdmaCQAssignPolicy {
public:
    RdmaCQAssignPolicy(uint32_t range);
    virtual ~RdmaCQAssignPolicy();

    // To assign an index
    virtual uint32_t Assign() = 0;

    // This index is released once
    virtual void Return(uint32_t index) = 0;

protected:
    uint32_t _range;
    butil::Mutex _lock;

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaCQAssignPolicy);
};

class RandomRdmaCQAssignPolicy : public RdmaCQAssignPolicy {
public:
    RandomRdmaCQAssignPolicy(uint32_t range);

    // @override
    virtual uint32_t Assign();

    // @override
    virtual void Return(uint32_t index);

private:
    DISALLOW_COPY_AND_ASSIGN(RandomRdmaCQAssignPolicy);
};

class RoundRobinRdmaCQAssignPolicy : public RdmaCQAssignPolicy {
public:
    RoundRobinRdmaCQAssignPolicy(uint32_t range);

    // @override
    virtual uint32_t Assign();

    // @override
    virtual void Return(uint32_t index);

private:
    DISALLOW_COPY_AND_ASSIGN(RoundRobinRdmaCQAssignPolicy);

    uint32_t _current;
};

class LeastUtilizedRdmaCQAssignPolicy : public RdmaCQAssignPolicy {
public:
    LeastUtilizedRdmaCQAssignPolicy(uint32_t range);

    // @override
    virtual uint32_t Assign();

    // @override
    virtual void Return(uint32_t index);

private:
    DISALLOW_COPY_AND_ASSIGN(LeastUtilizedRdmaCQAssignPolicy);

    std::vector<uint32_t> _list;
};

// Initialize global CQ environments when RDMA initialization
int GlobalCQInit();

// Release global CQ environments when exit
void GlobalCQRelease();

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_COMPLETION_QUEUE_H
