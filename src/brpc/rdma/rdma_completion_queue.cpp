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

#ifdef BRPC_RDMA
#include <infiniband/verbs.h>
#endif
#include <pthread.h>
#include <unistd.h>
#include <gflags/gflags.h>
#include "butil/atomicops.h"                         // atomic
#include "butil/fast_rand.h"                         // butil::fast_rand
#include "butil/fd_utility.h"                        // make_non_blocking
#include "butil/logging.h"                           // LOG
#include "butil/object_pool.h"                       // butil::get_object
#include "butil/string_splitter.h"                   // butil::StringSplitter
#include "bthread/unstable.h"                        // bthread_flush
#include "brpc/socket.h"                             // Socket::Address
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_completion_queue.h"

namespace brpc {

DECLARE_bool(usercode_in_pthread);

namespace rdma {

#ifdef BRPC_RDMA
extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*);
extern int (*IbvDestroyCompChannel)(ibv_comp_channel*);
extern int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**);
extern void (*IbvAckCqEvents)(ibv_cq*, unsigned int);

DEFINE_int32(rdma_cq_size, 65536, "CQ size used when CQs are shared.");
DEFINE_bool(rdma_use_shared_cq, false, "Use shared CQ mode for RDMA.");
DEFINE_bool(rdma_use_polling, false, "Use polling mode for RDMA.");
DEFINE_bool(rdma_use_inplace, false, "Use inplace mode for shared CQ mode "
                                     "as far as possible.");
DEFINE_bool(rdma_handle_urgent, true,
            "Use foreground bthread for message handling in shared CQ mode");
DEFINE_bool(rdma_bind_cpu, false, "Bind polling thread to CPU core.");
DEFINE_string(rdma_cq_assign_policy, "rr", "The policy to assign a CQ");
DEFINE_int32(rdma_cq_num, 0, "The number of CQs used in shared CQ mode");
DEFINE_string(rdma_cq_cpu_set, "", "The set of CPU cores bound with CQs");
DEFINE_string(rdma_cq_queue_mask, "", "The mask of RNIC queues bound with CQs");
DEFINE_int32(rdma_cqe_poll_once, 32, "The maximum of cqe number polled onece.");

static bool g_use_polling = false;
static bool g_bind_cpu = false;
static int g_cq_enabled_core_array[1024];
static int g_cq_enabled_cores = 0;
static int g_cq_enabled_queue_array[1024];
static int g_cq_enabled_queues = 0;
#endif

static const int MAX_CQ_EVENTS = 128;
static const size_t CORE_NUM = (size_t)sysconf(_SC_NPROCESSORS_ONLN);

bool g_rdma_traffic_enabled = true;
int g_cq_num = 0;
static RdmaCompletionQueue* g_cqs = NULL;
static RdmaCQAssignPolicy* g_cq_policy = NULL;

static butil::atomic<uint32_t> g_active_conn_num(0);

RdmaCompletionQueue::RdmaCompletionQueue()
    : _cq(NULL)
    , _cq_channel(NULL)
    , _cq_size(0)
    , _cq_events(0)
    , _queue_index(0)
    , _tid(0)
    , _sid(0)
    , _ep_sid(0)
    , _keytable_pool(NULL)
    , _stop(false)
{
}

RdmaCompletionQueue::~RdmaCompletionQueue() {
    StopAndJoin();
    CleanUp();
}

bool RdmaCompletionQueue::IsShared() {
    return g_cq_num > 0;
}

void* RdmaCompletionQueue::GetCQ() const {
    return _cq;
}

RdmaCompletionQueue* RdmaCompletionQueue::NewOne(Socket* s, int cq_size) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(g_cq_policy != NULL);
    CHECK(g_cq_num == 0);

    RdmaCompletionQueue* rcq = new (std::nothrow) RdmaCompletionQueue;
    if (!rcq) {
        PLOG(ERROR) << "Fail to construct RdmaCompletionQueue";
        return NULL;
    }

    rcq->_cq_size = FLAGS_rdma_cq_size < cq_size ?
                    FLAGS_rdma_cq_size : cq_size;
    rcq->_keytable_pool = s->_keytable_pool;
    if (rcq->Init(g_cq_enabled_queue_array[g_cq_policy->Assign()]) < 0) {
        PLOG(ERROR) << "Fail to intialize RdmaCompletionQueue";
        delete rcq;
        return NULL;
    }

    g_active_conn_num.fetch_add(1, butil::memory_order_relaxed);
    rcq->_ep_sid = s->id();
    return rcq;
#endif
}

RdmaCompletionQueue* RdmaCompletionQueue::GetOne() {
    CHECK(g_cq_policy != NULL);
    CHECK(g_cq_num > 0);
    g_active_conn_num.fetch_add(1, butil::memory_order_relaxed);
    return &g_cqs[g_cq_policy->Assign()];
}

int RdmaCompletionQueue::Init(int queue) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    _queue_index = queue;

    ibv_context* ctx = (ibv_context*)GetRdmaContext();
    ibv_comp_channel* cq_channel = NULL;
    if (!g_use_polling) {
        cq_channel = IbvCreateCompChannel(ctx);
        if (!cq_channel) {
            return -1;
        }
        _cq_channel = cq_channel;
    }

    if (_cq_size == 0) {
        _cq_size = FLAGS_rdma_cq_size;
    }

    ibv_cq* cq = IbvCreateCq(ctx, _cq_size, this, cq_channel, _queue_index);
    if (!cq) {
        PLOG(ERROR) << "Fail to create cq on queue " << _queue_index;
        return -1;
    }
    _cq = cq;

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _keytable_pool;
    if (!g_use_polling) {
        options.fd = cq_channel->fd;
        options.on_edge_triggered_events = PollCQ;
    }
    if (Socket::Create(options, &_sid) < 0) {
        return -1;
    }

    if (!g_use_polling) {
        butil::make_close_on_exec(cq_channel->fd);
        if (butil::make_non_blocking(cq_channel->fd) < 0) {
            return -1;
        }

        if (ibv_req_notify_cq(cq, 1) < 0) {
            return -1;
        }
    } else {
        CHECK(_tid == 0);
        // When we use polling mode, we select several thread workers to do
        // the polling all the time. These thread workers will not handle other
        // tasks any more.
        if (bthread_start_background(&_tid, &BTHREAD_ATTR_NORMAL,
                                     PollThis, this) != 0) {
            return -1;
        }
    }
    return 0;
#endif
}

void RdmaCompletionQueue::StopAndJoin() {
    _stop = true;
    if (_tid > 0) {
        bthread_join(_tid, 0);
        _tid = 0;
    }
}

void RdmaCompletionQueue::CleanUp() {
#ifdef BRPC_RDMA
    ibv_comp_channel* cq_channel = (ibv_comp_channel*)_cq_channel;
    ibv_cq* cq = (ibv_cq*)_cq;

    if (IsRdmaAvailable()) {
        if (cq) {
            if (cq_channel) {
                IbvAckCqEvents(cq, _cq_events);
            }
            if (IbvDestroyCq(cq) < 0) {
                PLOG(WARNING) << "Fail to destroy rdma cq";
            }
            _cq = NULL;
            if (cq_channel) {
                if (IbvDestroyCompChannel(cq_channel) < 0) {
                    PLOG(WARNING) << "Fail to destroy rdma cq channel";
                }
                _cq_channel = NULL;
            }
        }
    }
    _cq_events = 0;
    if (_sid > 0) {
        SocketUniquePtr s;
        if (Socket::Address(_sid, &s) == 0) {
            s->_fd = -1;
            s->_user = NULL;
            s->SetFailed();
        }
        _sid = 0;
    }
    _ep_sid = 0;
#endif
}

void RdmaCompletionQueue::Release() {
    g_cq_policy->Return(_queue_index);
    if (g_active_conn_num.fetch_sub(1, butil::memory_order_relaxed) == 1 &&
        !IsRdmaAvailable()) {
        // Do not waste CPU on unavailable RDMA
        GlobalCQRelease();
    }
}

int RdmaCompletionQueue::GetAndAckEvents() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cq_channel != NULL);
    CHECK(_cq != NULL);
    ibv_comp_channel* cq_channel = (ibv_comp_channel*)_cq_channel;
    ibv_cq* cq = (ibv_cq*)_cq;

    int events = 0;
    void* context = NULL;
    while (1) {
        if (IbvGetCqEvent(cq_channel, &cq, &context) < 0) {
            if (errno != EAGAIN) {
                PLOG(ERROR) << "Fail to get CQ event";
                return -1;
            }
            break;
        }
        ++events;
    }
    if (events == 0) {
        return 0;
    }
    _cq_events += events;
    if (_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(cq, _cq_events);
        _cq_events = 0;
    }
    return 0;
#endif
}

static inline void CQError() {
    // If we use shared CQ mode, the failure of CQ-related operation is
    // a fatal error. Currently, we force the application to fall back
    // to TCP.
    LOG(FATAL) << "We get a CQ error when we use shared CQ mode. ";
    GlobalDisableRdma();
}

void* RdmaCompletionQueue::PollThis(void* arg) {
    RdmaCompletionQueue* rcq = (RdmaCompletionQueue*)arg;
    SocketUniquePtr s;
    if (Socket::Address(rcq->_sid, &s) < 0) {
        return NULL;
    }
    rcq->PollCQ(s.get());
    return NULL;
}

void RdmaCompletionQueue::PollCQ(Socket* m) {
#ifdef BRPC_RDMA
    RdmaCompletionQueue* rcq = (RdmaCompletionQueue*)m->user();
    ibv_cq* cq = (ibv_cq*)rcq->_cq;
    SocketUniquePtr s;
    if (g_cq_num == 0 && Socket::Address(rcq->_ep_sid, &s) < 0) {
        return;
    }

    if (!g_use_polling) {
        if (rcq->GetAndAckEvents() < 0) {
            CQError();
            return;
        }
    } else {
        // Binding polling threads to CPU cores can get a better performance
        // when these cores are not used by other processes. Please use isolcpu
        // to provision dedicated cores for polling.
        if (g_bind_cpu) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
            CPU_SET(g_cq_enabled_core_array[rcq->_queue_index % g_cq_enabled_cores], &mask);
            pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
        }
    }

    int progress = Socket::PROGRESS_INIT;
    bool notified = false;
    InputMessenger::InputMessageClosure last_msg;
    ibv_wc wc[FLAGS_rdma_cqe_poll_once];
    while (!rcq->_stop) {
        int cnt = ibv_poll_cq(cq, FLAGS_rdma_cqe_poll_once, wc);
        if (cnt < 0) {
            PLOG(FATAL) << "Fail to ibv_poll_cq";
            if (g_cq_num == 0) {
                return;
            }
            CQError();
            break;
        }
        if (cnt == 0) {
            if (!g_use_polling) {
                if (!notified) {
                    // Since RDMA only provides one shot event, we have to call the
                    // notify function every time. Because there is a possibility
                    // that the event arrives after the poll but before the notify,
                    // we should re-poll the CQ once after the notify to check if
                    // there is an available CQE.
                    if (ibv_req_notify_cq(cq, 1) < 0) {
                        PLOG(FATAL) << "Fail to ibv_req_cq_notify";
                        if (g_cq_num == 0) {
                            return;
                        }
                        CQError();
                        break;
                    }
                    notified = true;
                    continue;
                }
                if (!m->MoreReadEvents(&progress)) {
                    break;
                }
                if (rcq->GetAndAckEvents() < 0) {
                    if (g_cq_num == 0) {
                        return;
                    }
                    CQError();
                    break;
                }
                notified = false;
            } else {
                last_msg.reset(NULL);
                s.reset(NULL);
            }
            continue;
        }
        notified = false;
        if (!g_rdma_traffic_enabled) {
            continue;
        }

        int num = 0;
        for (int i = 0; i < cnt; ++i) {
            if (g_cq_num > 0) {
                RdmaWrId* wrid = (RdmaWrId*)wc[i].wr_id;
                if (s == NULL || s->id() != wrid->sid) {
                    s.reset(NULL);
                    if (Socket::Address(wrid->sid, &s) < 0) {
                        butil::return_object<RdmaWrId>(wrid);
                        continue;
                    }
                    if (wrid->version != s->_rdma_ep->_version) {
                        // Belongs to old socket before reviving, avoid ABA problem
                        butil::return_object<RdmaWrId>(wrid);
                        continue;
                    }
                }
                butil::return_object<RdmaWrId>(wrid);
            }

            if (s->Failed()) {
                continue;
            }

            if (wc[i].status != IBV_WC_SUCCESS) {
                errno = ERDMA;
                PLOG(WARNING) << "Fail to handle RDMA completion, error status("
                              << wc[i].status << ")";
                s->SetFailed(errno, "Fail to handle RDMA completion, error status: %d",
                             wc[i].status);
                continue;
            }

            RdmaCompletion comp;
            RdmaCompletion* rc = NULL;
            if (g_cq_num == 0 || FLAGS_rdma_use_inplace) {
                rc = &comp;
            } else {
                rc = butil::get_object<RdmaCompletion>();
            }
            switch (wc[i].opcode) {
            case IBV_WC_SEND: {
                rc->type = RDMA_EVENT_SEND;
                break;
            }
            case IBV_WC_RECV: {
                rc->type = RDMA_EVENT_RECV;
                break;
            }
            case IBV_WC_RECV_RDMA_WITH_IMM: {
                rc->type = RDMA_EVENT_RECV_WITH_IMM;
                break;
            }
            case IBV_WC_RDMA_WRITE: {
                rc->type = RDMA_EVENT_WRITE;
                break;
            }
            default:
                rc->type = RDMA_EVENT_ERROR;
            }
            rc->len = wc[i].byte_len;
            rc->imm = ntohl(wc[i].imm_data);

            if (g_cq_num == 0 || FLAGS_rdma_use_inplace) {
                int new_thread_num = HandleCompletion(rc, s.get(), last_msg);
                if (new_thread_num > 0) {
                    num += new_thread_num;
                }
            } else {
                CHECK(s->_rdma_ep->_completion_queue.push(rc));
                if (s->_rdma_ep->_ncompletions.fetch_add(
                            1, butil::memory_order_release) == 0) {
                    SocketUniquePtr tmp;
                    s->ReAddress(&tmp);
                    bthread_attr_t attr = FLAGS_usercode_in_pthread ?
                            BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
                    bthread_t tid;
                    int ret = 0;
                    if (FLAGS_rdma_handle_urgent) {
                        ret = bthread_start_urgent(
                                &tid, &attr, HandleCompletions, s.get());
                    } else {
                        ret = bthread_start_background(
                                &tid, &attr, HandleCompletions, s.get());
                    }
                    if (ret != 0) {
                        PLOG(WARNING) << "Fail to start bthread to "
                                      << "handle RDMA completion";
                        HandleCompletion(rc, s.get(), last_msg);
                        butil::return_object<RdmaCompletion>(rc);
                    } else {
                        tmp.release();
                    }
                }
            }
        }

        if (g_cq_num == 0 || FLAGS_rdma_use_inplace) {
            if (num > 0) {
                // The flush is used for bthreads created by InputMessenger.
                bthread_flush();
            }
        }
    }
#endif
}

int RdmaCompletionQueue::HandleCompletion(
        RdmaCompletion* rc, Socket* s,
        InputMessenger::InputMessageClosure& last_msg) {
    ssize_t nr = s->_rdma_ep->HandleCompletion(*rc);
    if (nr < 0) {
        PLOG(WARNING) << "Fail to handle RDMA completion";
        s->SetFailed(errno, "Fail to handle RDMA completion");
        return -1;
    }
    if (nr == 0) {
        return 0;
    }

    const int64_t received_us = butil::cpuwide_time_us();
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    InputMessenger* messenger = static_cast<InputMessenger*>(s->user());
    return messenger->ProcessNewMessage(
            s, nr, false, received_us, base_realtime, last_msg);
}

void* RdmaCompletionQueue::HandleCompletions(void* arg) {
    // Do not join this bthread manually.
    SocketUniquePtr s((Socket*)arg);
    InputMessenger::InputMessageClosure last_msg;

    int progress = Socket::PROGRESS_INIT;
    RdmaCompletion* rc = NULL;
    int num = 0;
    do {
        if (rc) {
            if (!s->Failed()) {
                int ret = HandleCompletion(rc, s.get(), last_msg);
                if (ret > 0) {
                    num += ret;
                }
            }
            butil::return_object<RdmaCompletion>(rc);
        }
        if (!s->_rdma_ep->_completion_queue.pop(rc)) {
            if (s->_rdma_ep->_ncompletions.compare_exchange_strong(progress, 0,
                    butil::memory_order_release, butil::memory_order_acquire)) {
                break;
            } else {
                rc = NULL;
            }
        }
    } while (true);

    // Flush the bthreads created by InputMessenger.
    if (num > 0) {
        bthread_flush();
    }

    return NULL;
}

RdmaCQAssignPolicy::RdmaCQAssignPolicy(uint32_t range)
    : _range(range)
    , _lock()
{
}

RdmaCQAssignPolicy::~RdmaCQAssignPolicy() { }

RandomRdmaCQAssignPolicy::RandomRdmaCQAssignPolicy(uint32_t range)
    : RdmaCQAssignPolicy(range)
{
}

uint32_t RandomRdmaCQAssignPolicy::Assign() {
    return butil::fast_rand() % _range;
}

void RandomRdmaCQAssignPolicy::Return(uint32_t) {
}

RoundRobinRdmaCQAssignPolicy::RoundRobinRdmaCQAssignPolicy(uint32_t range)
    : RdmaCQAssignPolicy(range)
    , _current(0)
{
}

uint32_t RoundRobinRdmaCQAssignPolicy::Assign() {
    BAIDU_SCOPED_LOCK(_lock);
    uint32_t rst = _current++;
    if (_current == _range) {
        _current = 0;
    }
    return rst;
}

void RoundRobinRdmaCQAssignPolicy::Return(uint32_t) { }

LeastUtilizedRdmaCQAssignPolicy::LeastUtilizedRdmaCQAssignPolicy(uint32_t range)
    : RdmaCQAssignPolicy(range)
    , _list(range)
{
    for (size_t i = 0; i < _list.size(); ++i) {
        _list[i] = 0;
    }
}

uint32_t LeastUtilizedRdmaCQAssignPolicy::Assign() {
    uint32_t min = 0xffffffff;
    size_t index = 0;
    BAIDU_SCOPED_LOCK(_lock);
    for (size_t i = 0; i < _list.size(); ++i) {
        if (_list[i] == 0) {
            index = i;
            break;
        }
        if (min > _list[i]) {
            min = _list[i];
            index = i;
        }
    }

    ++_list[index];
    return index;
}

void LeastUtilizedRdmaCQAssignPolicy::Return(uint32_t index) {
    BAIDU_SCOPED_LOCK(_lock);
    CHECK(_list[index] > 0);
    --_list[index];
}

#ifdef BRPC_RDMA
static void ParseCpuMask(std::string& cpu_str) {
    if (cpu_str == "") {
        for (size_t i = 0; i < CORE_NUM; ++i) {
            g_cq_enabled_core_array[i] = i;
        }
        g_cq_enabled_cores = CORE_NUM;
        return;
    }
    int cores = 0;
    for (butil::StringSplitter s(cpu_str.c_str(), ','); s; ++s) {
        int num = 0;
        unsigned int last_core = -1;
        for (butil::StringSplitter ss(s.field(), s.field() + s.length(), '-'); ss; ++ss) {
            if (++num > 2) {
                return;
            }
            unsigned int core = 0;
            if (ss.to_uint(&core) < 0 || core >= CORE_NUM) {
                return;
            }
            if (num == 1) {
                last_core = core;
                g_cq_enabled_core_array[cores++] = core;
                continue;
            }
            if (last_core > core) {
                return;
            }
            for (unsigned int i = last_core + 1; i <= core; ++i) {
                g_cq_enabled_core_array[cores++] = i;
            }
        }
    }
    g_cq_enabled_cores = cores;
}

static void ParseQueueMask(std::string& mask_str) {
    int max_queues = ((ibv_context*)GetRdmaContext())->num_comp_vectors;
    if (mask_str == "") {
        for (int i = 0; i < max_queues; ++i) {
            g_cq_enabled_queue_array[i] = i;
        }
        g_cq_enabled_queues = max_queues;
        return;
    }
    int queues = 0;
    for (butil::StringSplitter s(mask_str.c_str(), ','); s; ++s) {
        int num = 0;
        unsigned int last_queue = -1;
        for (butil::StringSplitter ss(s.field(), s.field() + s.length(), '-'); ss; ++ss) {
            if (++num > 2) {
                return;
            }
            unsigned int queue = 0;
            if (ss.to_uint(&queue) < 0 || (int)queue >= max_queues) {
                return;
            }
            if (num == 1) {
                last_queue = queue;
                g_cq_enabled_queue_array[queues++] = queue;
                continue;
            }
            if (last_queue > queue) {
                return;
            }
            for (unsigned int i = last_queue + 1; i <= queue; ++i) {
                g_cq_enabled_queue_array[queues++] = i;
            }
        }
    }
    g_cq_enabled_queues = queues;
}

#endif

int GlobalCQInit() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    ParseQueueMask(FLAGS_rdma_cq_queue_mask);
    if (g_cq_enabled_queues == 0) {
        LOG(ERROR) << "Incorrect rdma_cq_queue_mask, use it like taskset -c";
        return -1;
    }
    ParseCpuMask(FLAGS_rdma_cq_cpu_set);
    if (g_cq_enabled_cores == 0) {
        LOG(ERROR) << "Incorrect rdma_cq_cpu_set, use it like taskset -c";
        return -1;
    }

    if (FLAGS_rdma_cq_num < 0) {
        g_cq_num = 0;
    } else {
        g_cq_num = std::min(FLAGS_rdma_cq_num, g_cq_enabled_cores);
    }
    g_use_polling = FLAGS_rdma_use_polling;
    g_bind_cpu = FLAGS_rdma_bind_cpu;
    if (g_use_polling && g_cq_num == 0) {
        g_cq_num = g_cq_enabled_cores;
    }

    if (g_cq_num > 0) {
        g_cqs = new (std::nothrow) RdmaCompletionQueue[g_cq_num];
        if (!g_cqs) {
            PLOG(WARNING) << "Fail to malloc RdmaCompletionQueue";
            return -1;
        }
        for (int i = 0; i < g_cq_num; ++i) {
            if (g_cqs[i].Init(g_cq_enabled_queue_array[i]) < 0) {
                PLOG(WARNING) << "Fail to initialize RdmaCompletionQueue";
                return -1;
            }
        }
    }

    if (FLAGS_rdma_cq_assign_policy.compare("random") == 0) {
        g_cq_policy = new (std::nothrow)
            RandomRdmaCQAssignPolicy((g_cq_num == 0) ? g_cq_enabled_queues : g_cq_num);
    } else if (FLAGS_rdma_cq_assign_policy.compare("rr") == 0) {
        g_cq_policy = new (std::nothrow)
            RoundRobinRdmaCQAssignPolicy((g_cq_num == 0) ? g_cq_enabled_queues : g_cq_num);
    } else if (FLAGS_rdma_cq_assign_policy.compare("least_used") == 0) {
        g_cq_policy = new (std::nothrow)
            LeastUtilizedRdmaCQAssignPolicy((g_cq_num == 0) ? g_cq_enabled_queues : g_cq_num);
    } else {
        LOG(WARNING) << "Incorrect RdmaCQAssignPolicy. Possible value:"
                        " rr, random, least_used";
        return -1;
    }

    if (!g_cq_policy) {
        PLOG(WARNING) << "Fail to construct RdmaCQAssignPolicy";
        return -1;
    }

    return 0;
#endif
}

static pthread_once_t release_cq_once = PTHREAD_ONCE_INIT;

void GlobalCQReleaseImpl() {
    for (int i = 0; i < g_cq_num; ++i) {
        g_cqs[i].StopAndJoin();
    }

    // Do not delete g_cqs and g_cq_policy here
}

void GlobalCQRelease() {
    if (pthread_once(&release_cq_once,
                     GlobalCQReleaseImpl) != 0) {
        LOG(FATAL) << "Fail to pthread_once GlobalCQRelease";
    }
}

}  // namespace rdma
}  // namespace brpc
