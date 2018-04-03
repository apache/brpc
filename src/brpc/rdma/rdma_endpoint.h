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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)
//         Ding,Jie (dingjie01@baidu.com)

#ifdef BRPC_RDMA

#ifndef BRPC_RDMA_ENDPOINT_H
#define BRPC_RDMA_ENDPOINT_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <vector>
#include <butil/atomicops.h>                    // butil::atomic
#include <butil/containers/flat_map.h>
#include <butil/macros.h>
#include <bthread/bthread.h>
#include "brpc/rdma/rdma.pb.h"
#include "brpc/rdma/rdma_iobuf.h"

namespace brpc {

class Socket;
struct InputMessageBase;

namespace rdma {

class RdmaConnServiceImpl;

struct ParseData {
    void* msg_buf;
    size_t msg_len;
};

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint {
friend class brpc::Socket;
friend class RdmaAcceptor;
friend class RdmaConnServiceImpl;
public:
    RdmaEndpoint(Socket* s);
    ~RdmaEndpoint();

    // get an event from ibverbs fd
    static void StartVerbsEvent(uint64_t id);

    // get an event from rdmacm fd
    static void StartCmEvent(uint64_t id);

    // init resources
    void Init();

    // release resources
    void CleanUp();

private:
    enum Status {
        UNINITIALIZED,
        ADDR_RESOLVING,
        ROUTE_RESOLVING,
        CONNECTING,
        ACCEPTING,
        ESTABLISHED
    };

    enum RdmaReadStatus {
        READING,
        ERROR,
        SUCCESS
    };

    // post RDMA_RECV to recv queue
    bool PostRecv();

    // set the rdma channel to failed
    void SetFailed();

    // get the failed status of rdma channel
    bool Failed() {return _rdma_error.load(butil::memory_order_relaxed); }

    // establish rdma connection
    bool StartConnect();

    // write in background
    static void* KeepWrite(void* arg);

    // check if all data is written
    bool IsWriteComplete(void* old_head_void, bool singular_node,
                         void** new_tail_void);

    // write with rdma
    int Write(void* arg, void* rdma_arg);

    // send the data with ibv_post_send
    int DoWrite(void* arg);

    // allocate resources
    int AllocateResources();

    // release resources
    void DeallocateResources();

    // process the RPC request/response (in arg)
    static void* DoProcess(void* arg);

    // start the thread of DoProcess
    inline void StartProcess(brpc::InputMessageBase* msg);

    // poll cq to get cqe
    static void* PollCQ(void* arg);

    // Send pure ACK without data
    void SendAck();

    // handle rdmacm events
    int HandleCmEvent();

    // when the server receives an rdmacm event during connection
    int DoAccept(int err);

    // when the client receives an rdmacm event during connection
    int DoConnect(int err);

    // send RdmaConnect rpc call request
    bool SendRdmaRequest(Socket *s, butil::IOBuf *packet,
                         RdmaConnRequest *request,
                         bthread_id_t cid);

    // release the requests and put them into tcp channel
    void ReleaseAllFailedWriteRequests(void* arg);

    // start a background write thread
    inline void StartKeepWrite(void* arg);

    // get and ack the cq events
    void GetAndAckCQEvents();

    // pointer to the associated Socket
    Socket* _socket;

    // act as sbuf
    std::vector<RdmaIOBuf> _iobuf_pool;

    // the index in _iobuf_pool which can be used
    size_t _iobuf_index;

    // how many IOBuf in _iobuf_pool is available
    butil::atomic<int> _iobuf_idle_count;

    // pointer to the rdmacm struct
    rdma_cm_id* _cm_id;

    // status of the rdma_endpoint
    Status _status;

    // write head of the WriteRequest Queue
    butil::atomic<void*> _write_head;

    // butex for writing
    butil::atomic<int>* _write_butex;

    // seq no. for sending
    uint32_t _seq;

    // received ack no. from the peer side
    butil::atomic<uint32_t> _received_ack;

    // the ack no. should be included in imm data to the peer side
    butil::atomic<uint32_t> _reply_ack;

    // wr num in remote rq
    butil::atomic<int> _remote_rq_wr;

    // for reduce overhead of atomic fetch_add
    int _remote_rq_wr_tmp;

    // the total length received not acked
    uint32_t _unack_len;

    // new wr num in local rq
    butil::atomic<int> _new_local_rq_wr;

    // for reduce overhead of atomic fetch_add
    int _new_local_rq_wr_tmp;

    // how many cq events should be acknowledged
    uint32_t _cq_events;

    // used for avoid run PollCQ in two threads
    butil::atomic<int> _npolls;

    // used for protect rdmacm
    bthread::Mutex _cm_lock;

    // the sid of the Socket at the peer side
    uint64_t _remote_sid;

    // the address of the rbuf
    uint8_t* _local_rbuf;

    // the size of the rbuf
    uint32_t _local_rbuf_size;

    // the memory region of the rbuf
    ibv_mr* _local_rbuf_mr;

    // the address of the remote side's rbuf
    uint8_t* _remote_rbuf;

    // the size of the remote side's rbuf
    uint32_t _remote_rbuf_size;

    // the rkey of the remote side's rbuf
    uint32_t _remote_rbuf_rkey;

    // used for copying the data received temporarily
    butil::IOBuf _read_buf;

    // whether the rdma channel has error or not
    // Note:
    // Currently, we let the Socket goes back to tcp whenever there is an error
    // on rdma channel, even if some errors may be recoverable.
    butil::atomic<bool> _rdma_error;

    // used for authentication when building connection
    void* _auth_for_connect;

    DISALLOW_COPY_AND_ASSIGN(RdmaEndpoint);
};

}  // namespace rdma
}  // namespace brpc

#endif // BRPC_RDMA_ENDPOINT_H

#endif

