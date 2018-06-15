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
#include <butil/fd_utility.h>                        // make_non_blocking
#include <butil/logging.h>                           // LOG
#include <butil/object_pool.h>                       // butil::get_object
#include <butil/rand_util.h>                         // butil::RandGenerator
#include <bthread/execution_queue.h>
#include <gflags/gflags.h>
#include "brpc/input_messenger.h"
#include "brpc/socket.h"                             // Socket::Address
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"                   // GetRdmaContext
#include "brpc/rdma/rdma_completion_queue.h"

namespace brpc {
namespace rdma {

DEFINE_int32(rdma_cq_size, 65536, "CQ size used when CQs are shared.");
DEFINE_bool(rdma_use_polling, false, "Use polling mode for RDMA.");
DEFINE_bool(rdma_use_inplace, false, "Use inplace mode for RDMA.");
DEFINE_bool(rdma_bind_cpu, false, "Bind polling thread to CPU core.");
DEFINE_string(rdma_cq_assign_policy, "rr", "The policy to assign a CQ");
DEFINE_int32(rdma_cq_num, 0, "CQ num used when CQs are shared by connections. "
                             "0 means no sharing.");
// Sometimes we do not want to use the first core since some other interrupts
// are bound to this core. Therefore we use an offset to specify the first
// core to start shared CQ.
DEFINE_int32(rdma_cq_offset, 4, "The first core index to start CQ");

static const int MAX_COMPLETIONS_ONCE = 32;
static const int MAX_CQ_EVENTS = 128;
static const size_t CORE_NUM = (size_t)sysconf(_SC_NPROCESSORS_ONLN);

int g_cq_num = 0;
static bool g_use_polling = false;
static bool g_bind_cpu = false;
static RdmaCompletionQueue* g_cq = NULL;
static RdmaCQAssignPolicy* g_cq_policy = NULL;

RdmaCompletionQueue::RdmaCompletionQueue()
    : _cq(NULL)
    , _cq_channel(NULL)
    , _cq_size(0)
    , _cq_events(0)
    , _cpu_index(0)
    , _tid(0)
    , _sid(0)
    , _ep_sid(0)
    , _stop(false)
{
}

RdmaCompletionQueue::~RdmaCompletionQueue() {
    Release();
    StopAndJoin();
    CleanUp();
}

bool RdmaCompletionQueue::IsShared() const {
    return g_cq_num > 0;
}

void* RdmaCompletionQueue::GetCQ() const {
    return _cq;
}

RdmaCompletionQueue* RdmaCompletionQueue::GetOne(Socket* s, int cq_size) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    int index = g_cq_policy->Assign();

    if (g_cq_num == 0) {
        RdmaCompletionQueue* rcq = new (std::nothrow) RdmaCompletionQueue;
        if (!rcq) {
            PLOG(ERROR) << "Fail to construct RdmaCompletionQueue";
            return NULL;
        }

        rcq->_cq_size = FLAGS_rdma_cq_size < cq_size ?
                        FLAGS_rdma_cq_size : cq_size;
        rcq->_keytable_pool = s->_keytable_pool;
        if (rcq->Init(index) < 0) {
            PLOG(ERROR) << "Fail to intialize RdmaCompletionQueue";
            delete rcq;
            return NULL;
        }

        rcq->_ep_sid = s->id();
        return rcq;
    } else {
        return &g_cq[index];
    }
#endif
}

int RdmaCompletionQueue::Init(int cpu) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    _cpu_index = cpu;

    ibv_context* ctx = (ibv_context*)GetRdmaContext();
    ibv_comp_channel* cq_channel = NULL;
    if (!g_use_polling) {
        cq_channel = ibv_create_comp_channel(ctx);
        if (!cq_channel) {
            return -1;
        }
        _cq_channel = cq_channel;
    }

    if (_cq_size == 0) {
        _cq_size = FLAGS_rdma_cq_size;
    }

    ibv_cq* cq = ibv_create_cq(ctx, _cq_size, this, cq_channel, _cpu_index);
    if (!cq) {
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
        if (bthread_start_background(&_tid, &BTHREAD_ATTR_PTHREAD,
                                     PollThis, this) < 0) {
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

    if (cq_channel && cq) {
        ibv_ack_cq_events(cq, _cq_events);
        ibv_destroy_comp_channel(cq_channel);
        _cq_channel = NULL;
    }
    _cq_events = 0;
    if (cq) {
        ibv_destroy_cq(cq);
        _cq = NULL;
    }
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
    g_cq_policy->Return(_cpu_index);
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
        if (ibv_get_cq_event(cq_channel, &cq, &context) < 0) {
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
        ibv_ack_cq_events(cq, _cq_events);
        _cq_events = 0;
    }
    return 0;
#endif
}

static inline void CQError() {
    // TODO:
    // If we use shared CQ mode, the failure of CQ-related operation is
    // a fatal error. Currently, we force the application to exit.
    LOG(FATAL) << "We get a CQ error when we use shared CQ mode. "
               << "Application exit.";
    exit(1);
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
            CPU_SET(rcq->_cpu_index, &mask);
            pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
        }
    }

    int progress = Socket::PROGRESS_INIT;
    bool notified = false;
    InputMessenger::InputMessageClosure last_msg;
    ibv_wc wc[MAX_COMPLETIONS_ONCE];
    while (!rcq->_stop) {
        int cnt = ibv_poll_cq(cq, MAX_COMPLETIONS_ONCE, wc);
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
                s.reset(NULL);
            }
            continue;
        }
        notified = false;

        const int64_t received_us = butil::cpuwide_time_us();
        const int64_t base_realtime = butil::gettimeofday_us() - received_us;
        for (int i = 0; i < cnt; ++i) {
            if (s == NULL || s->id() != wc[i].wr_id) {
                s.reset(NULL);
                if (Socket::Address(wc[i].wr_id, &s) < 0) {
                    continue;
                }
            }

            if (s->Failed()) {
                continue;
            }

            if (wc[i].status != IBV_WC_SUCCESS) {
                errno = ERDMA;
                PLOG(WARNING) << "Fail to handle RDMA completion";
                s->SetFailed(errno, "Fail to handle RDMA completion");
                continue;
            }

            RdmaCompletion* rc = butil::get_object<RdmaCompletion>();
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

            if (g_cq_num == 0) {
                ssize_t nr = s->_rdma_ep->HandleCompletion(*rc);
                butil::return_object<RdmaCompletion>(rc);
                if (nr < 0) {
                    PLOG(WARNING) << "Fail to handle RDMA completion";
                    s->SetFailed(errno, "Fail to handle RDMA completion");
                    continue;
                }
                if (nr == 0) {
                    continue;
                }

                InputMessenger* messenger =
                        static_cast<InputMessenger*>(s->user());
                if (messenger->ProcessNewMessage(s.get(), nr, false,
                            received_us, base_realtime, last_msg) < 0) {
                    continue;
                }
            } else {
                SocketUniquePtr tmp;
                s->ReAddress(&tmp);
                rc->socket = tmp.get();
                bthread::TaskOptions opt;
                // Inplace leads to calling as a function, which will not start a new
                // bthread. So the usercode MUST NOT have blocking operation.
                opt.in_place_if_possible = FLAGS_rdma_use_inplace;
                if (bthread::execution_queue_execute(
                            s->_rdma_ep->_completion_queue, rc, &opt) < 0) {
                    PLOG(WARNING) << "Fail to insert RDMA completion to execution queue";
                } else {
                    tmp.release();
                }
            }
        }
    }
#endif
}

RdmaCQAssignPolicy::RdmaCQAssignPolicy(uint32_t range)
    : _range(range)
    , _lock()
{
}

RdmaCQAssignPolicy::~RdmaCQAssignPolicy() {
}

RandomRdmaCQAssignPolicy::RandomRdmaCQAssignPolicy(uint32_t range)
    : RdmaCQAssignPolicy(range)
{
}

uint32_t RandomRdmaCQAssignPolicy::Assign() {
    return butil::RandGenerator(_range);
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

void RoundRobinRdmaCQAssignPolicy::Return(uint32_t) {
}

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

int GlobalCQInit() {
    if (FLAGS_rdma_cq_num >= (int32_t)CORE_NUM) {
        g_cq_num = CORE_NUM - 1;
    } else {
        g_cq_num = FLAGS_rdma_cq_num;
    }
    if (g_cq_num == 0) {
        g_use_polling = false;
    } else {
        g_use_polling = FLAGS_rdma_use_polling;
        g_bind_cpu = FLAGS_rdma_bind_cpu;
    }

    uint32_t cq_assign_range = CORE_NUM;
    if (g_cq_num > 0) {
        g_cq = new (std::nothrow) RdmaCompletionQueue[g_cq_num];
        if (!g_cq) {
            PLOG(WARNING) << "Fail to malloc RdmaCompletionQueue";
            return -1;
        }
        for (int i = 0; i < g_cq_num; ++i) {
            if (g_cq[i].Init((i + FLAGS_rdma_cq_offset) % CORE_NUM) < 0) {
                PLOG(WARNING) << "Fail to initialize RdmaCompletionQueue";
                return -1;
            }
        }
        cq_assign_range = g_cq_num;
    }

    if (FLAGS_rdma_cq_assign_policy.compare("random") == 0) {
        g_cq_policy = new (std::nothrow)
                      RandomRdmaCQAssignPolicy(cq_assign_range);
    } else if (FLAGS_rdma_cq_assign_policy.compare("rr") == 0) {
        g_cq_policy = new (std::nothrow)
                      RoundRobinRdmaCQAssignPolicy(cq_assign_range);
    } else if (FLAGS_rdma_cq_assign_policy.compare("least") == 0) {
        g_cq_policy = new (std::nothrow)
                      LeastUtilizedRdmaCQAssignPolicy(cq_assign_range);
    } else {
        LOG(WARNING) << "Incorrect RdmaCQAssignPolicy. Possible value:"
                        " rr, random, least";
        return -1;
    }

    if (!g_cq_policy) {
        PLOG(WARNING) << "Fail to construct RdmaCQAssignPolicy";
        return -1;
    }

    return 0;
}

void GlobalCQRelease() {
    for (int i = 0; i < g_cq_num; ++i) {
        g_cq[i].StopAndJoin();
    }
    // Do not release g_cq and g_cq_policy to avoid segmentation fault
}

}  // namespace rdma
}  // namespace brpc

