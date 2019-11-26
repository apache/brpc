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

// Authors: Ge,Jun (gejun@baidu.com)
//          Jiashun Zhu(zhujiashun2010@gmail.com)

#include <queue>
#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>
#include "butil/logging.h"                       // LOG()
#include "butil/time.h"
#include "butil/iobuf.h"                         // butil::IOBuf
#include "brpc/controller.h"               // Controller
#include "brpc/details/controller_private_accessor.h"
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/redis.h"
#include "brpc/redis_command.h"
#include "brpc/policy/redis_protocol.h"
#include "bthread/execution_queue.h"

namespace brpc {

DECLARE_bool(enable_rpcz);
DECLARE_bool(usercode_in_pthread);

namespace policy {

DEFINE_bool(redis_verbose, false,
            "[DEBUG] Print EVERY redis request/response");

struct InputResponse : public InputMessageBase {
    bthread_id_t id_wait;
    RedisResponse response;

    // @InputMessageBase
    void DestroyImpl() {
        delete this;
    }
};

class RedisConnContext;
class ConsumeTaskDone : public google::protobuf::Closure {
public:
    ConsumeTaskDone()
        : _ready(false)
        , output_message(&_arena) {}

    void Run() override;
    bool is_ready() { return _ready.load(butil::memory_order_acquire); }

private:
    butil::atomic<bool> _ready;
    butil::Arena _arena;

public:
    RedisMessage output_message;
    RedisConnContext* meta;
    butil::IOBuf sendbuf;
};


class RedisConnContext : public brpc::SharedObject {
public:
    RedisConnContext() : handler_continue(NULL) {}
    ~RedisConnContext() {
        ClearQueue(dones);
    }

    void Push(ConsumeTaskDone* done) {
        std::unique_lock<butil::Mutex> m(_mutex);
        dones.push(done);
    }
    void Flush() {
        std::queue<ConsumeTaskDone*> ready_to_write;
        SocketUniquePtr s;
        if (Socket::Address(socket_id, &s) != 0) {
            LOG(WARNING) << "Fail to address redis socket";
            return;
        }
        {
            std::unique_lock<butil::Mutex> m(_mutex);
            if (_writing) return;
            _writing = true;
        }
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        std::queue<ConsumeTaskDone*> ready_to_delete;
        while (true) {
            std::unique_lock<butil::Mutex> m(_mutex);
            while (!dones.empty() && dones.front()->is_ready()) {
                ready_to_write.push(dones.front());
                dones.pop();
            }
            if (ready_to_write.empty()) {
                _writing = false;
                break;
            }
            m.unlock();

            while (!ready_to_write.empty()) {
                ConsumeTaskDone* head = ready_to_write.front();
                ready_to_write.pop();
                LOG_IF(WARNING, s->Write(&head->sendbuf, &wopt) != 0)
                    << "Fail to send redis reply";
                ready_to_delete.push(head);
            }
        }
        ClearQueue(ready_to_delete);
    }

    SocketId socket_id;
    RedisService::CommandMap command_map;
    RedisCommandHandler* handler_continue;
    std::queue<ConsumeTaskDone*> dones;

private:
    void ClearQueue(std::queue<ConsumeTaskDone*>& queue) {
        while (!queue.empty()) {
            ConsumeTaskDone* head = queue.front();
            queue.pop();
            delete head;
        }
    }

    bool _writing = false;
    butil::Mutex _mutex;
};

struct TaskContext {
    RedisMessage message;
    butil::Arena arena;
};

const char** ParseArgs(const RedisMessage& message) {
    const char** args = (const char**)
        malloc(sizeof(const char*) * (message.size() + 1 /* NULL */));
    for (size_t i = 0; i < message.size(); ++i) {
        if (!message[i].is_string()) {
            free(args);
            return NULL;
        }
        args[i] = message[i].c_str();
    }
    args[message.size()] = NULL;
    return args;
}

void ConsumeTaskDone::Run() { 
    butil::intrusive_ptr<RedisConnContext> delete_queue_meta(meta, false);
    output_message.SerializeToIOBuf(&sendbuf);
    _ready.store(true, butil::memory_order_release);
    meta->Flush();
}

int ConsumeTask(RedisConnContext* meta, const RedisMessage& m) {
    ConsumeTaskDone* done = new ConsumeTaskDone;
    ClosureGuard done_guard(done);
    meta->Push(done);
    meta->AddRefManually();
    done->meta = meta;
    RedisMessage& output = done->output_message;

    const char** args = ParseArgs(m);
    if (!args) {
        output.SetError("ERR command not string");
        return -1;
    }
    if (meta->handler_continue) {
        RedisCommandHandler::Result result = meta->handler_continue->Run(
                args, &output, done_guard.release());
        if (result == RedisCommandHandler::OK) {
            meta->handler_continue = NULL;
        }
    } else {
        std::string comm;
        comm.reserve(8);
        for (const char* c = m[0].c_str(); *c; ++c) {
            comm.push_back(std::tolower(*c));
        }
        auto it = meta->command_map.find(comm);
        if (it == meta->command_map.end()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "ERR unknown command `%s`", comm.c_str());
            output.SetError(buf);
        } else {
            RedisCommandHandler::Result result =
                it->second->Run(args, &output, done_guard.release());
            if (result == RedisCommandHandler::CONTINUE) {
                meta->handler_continue = it->second.get();
            }
        }
    }
    free(args);
    return 0;
}

int Consume(void* meta, bthread::TaskIterator<TaskContext*>& iter) {
    RedisConnContext* qmeta = static_cast<RedisConnContext*>(meta);
    if (iter.is_queue_stopped()) {
        qmeta->RemoveRefManually();
        return 0;
    }
    for (; iter; ++iter) {
        std::unique_ptr<TaskContext> ctx(*iter);
        ConsumeTask(qmeta, ctx->message);
    }
    return 0;
}

class ServerContext : public Destroyable {
public:
    ~ServerContext() {
        bthread::execution_queue_stop(queue);
    }

    // @Destroyable
    void Destroy() { delete this; }

    int init(RedisConnContext* meta) {
        bthread::ExecutionQueueOptions q_opt;
        q_opt.bthread_attr =
            FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
        if (bthread::execution_queue_start(&queue, &q_opt, Consume, meta) != 0) {
            LOG(ERROR) << "Fail to start execution queue";
            return -1;
        }
        return 0;
    }

    bthread::ExecutionQueueId<TaskContext*> queue;
};

ParseResult ParseRedisMessage(butil::IOBuf* source, Socket* socket,
                              bool read_eof, const void* arg) {
    if (read_eof || source->empty()) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const Server* server = static_cast<const Server*>(arg);
    if (server) {
        RedisService* rs = server->options().redis_service;
        if (!rs) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        ServerContext* ctx = static_cast<ServerContext*>(socket->parsing_context());
        if (ctx == NULL) {
            RedisConnContext* meta = new RedisConnContext;
            meta->AddRefManually();
            meta->socket_id = socket->id();
            rs->CloneCommandMap(&meta->command_map);
            ctx = new ServerContext;
            if (ctx->init(meta) != 0) {
                delete ctx;
                meta->RemoveRefManually();
                LOG(ERROR) << "Fail to init redis ServerContext";
                return MakeParseError(PARSE_ERROR_NO_RESOURCE);
            }
            socket->reset_parsing_context(ctx);
        }
        std::unique_ptr<TaskContext> task_ctx(new TaskContext);
        ParseError err = task_ctx->message.ConsumePartialIOBuf(*source, &task_ctx->arena);
        if (err != PARSE_OK) {
            return MakeParseError(err);
        }
        if (bthread::execution_queue_execute(ctx->queue, task_ctx.get()) != 0) {
            LOG(ERROR) << "Fail to push execution queue";
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        task_ctx.release();
        return MakeMessage(NULL);
    } else {
        // NOTE(gejun): PopPipelinedInfo() is actually more contended than what
        // I thought before. The Socket._pipeline_q is a SPSC queue pushed before
        // sending and popped when response comes back, being protected by a
        // mutex. Previously the mutex is shared with Socket._id_wait_list. When
        // 200 bthreads access one redis-server, ~1.5s in total is spent on
        // contention in 10-second duration. If the mutex is separated, the time
        // drops to ~0.25s. I further replaced PeekPipelinedInfo() with
        // GivebackPipelinedInfo() to lock only once(when receiving response)
        // in most cases, and the time decreases to ~0.14s.
        PipelinedInfo pi;
        if (!socket->PopPipelinedInfo(&pi)) {
            LOG(WARNING) << "No corresponding PipelinedInfo in socket";
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }

        do {
            InputResponse* msg = static_cast<InputResponse*>(socket->parsing_context());
            if (msg == NULL) {
                msg = new InputResponse;
                socket->reset_parsing_context(msg);
            }

            const int consume_count = (pi.with_auth ? 1 : pi.count);

            ParseError err = msg->response.ConsumePartialIOBuf(*source, consume_count);
            if (err != PARSE_OK) {
                socket->GivebackPipelinedInfo(pi);
                return MakeParseError(err);
            }

            if (pi.with_auth) {
                if (msg->response.reply_size() != 1 ||
                    !(msg->response.reply(0).type() == brpc::REDIS_MESSAGE_STATUS &&
                      msg->response.reply(0).data().compare("OK") == 0)) {
                    LOG(ERROR) << "Redis Auth failed: " << msg->response;
                    return MakeParseError(PARSE_ERROR_NO_RESOURCE,
                                          "Fail to authenticate with Redis");
                }

                DestroyingPtr<InputResponse> auth_msg(
                     static_cast<InputResponse*>(socket->release_parsing_context()));
                pi.with_auth = false;
                continue;
            }

            CHECK_EQ((uint32_t)msg->response.reply_size(), pi.count);
            msg->id_wait = pi.id_wait;
            socket->release_parsing_context();
            return MakeMessage(msg);
        } while(true);
    }

    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
}

void ProcessRedisResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<InputResponse> msg(static_cast<InputResponse*>(msg_base));

    const bthread_id_t cid = msg->id_wait;
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        return;
    }

    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(msg->base_real_us());
        span->set_received_us(msg->received_us());
        span->set_response_size(msg->response.ByteSize());
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    if (cntl->response() != NULL) {
        if (cntl->response()->GetDescriptor() != RedisResponse::descriptor()) {
            cntl->SetFailed(ERESPONSE, "Must be RedisResponse");
        } else {
            // We work around ParseFrom of pb which is just a placeholder.
            if (msg->response.reply_size() != (int)accessor.pipelined_count()) {
                cntl->SetFailed(ERESPONSE, "pipelined_count=%d of response does "
                                      "not equal request's=%d",
                                      msg->response.reply_size(), accessor.pipelined_count());
            }
            ((RedisResponse*)cntl->response())->Swap(&msg->response);
            if (FLAGS_redis_verbose) {
                LOG(INFO) << "\n[REDIS RESPONSE] "
                          << *((RedisResponse*)cntl->response());
            }
        }
    } // silently ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void ProcessRedisRequest(InputMessageBase* msg_base) { }

void SerializeRedisRequest(butil::IOBuf* buf,
                           Controller* cntl,
                           const google::protobuf::Message* request) {
    if (request == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    if (request->GetDescriptor() != RedisRequest::descriptor()) {
        return cntl->SetFailed(EREQUEST, "The request is not a RedisRequest");
    }
    const RedisRequest* rr = (const RedisRequest*)request;
    // We work around SerializeTo of pb which is just a placeholder.
    if (!rr->SerializeTo(buf)) {
        return cntl->SetFailed(EREQUEST, "Fail to serialize RedisRequest");
    }
    ControllerPrivateAccessor(cntl).set_pipelined_count(rr->command_size());
    if (FLAGS_redis_verbose) {
        LOG(INFO) << "\n[REDIS REQUEST] " << *rr;
    }
}

void PackRedisRequest(butil::IOBuf* buf,
                      SocketMessage**,
                      uint64_t /*correlation_id*/,
                      const google::protobuf::MethodDescriptor*,
                      Controller* cntl,
                      const butil::IOBuf& request,
                      const Authenticator* auth) {
    if (auth) {
        std::string auth_str;
        if (auth->GenerateCredential(&auth_str) != 0) {
            return cntl->SetFailed(EREQUEST, "Fail to generate credential");
        }
        buf->append(auth_str);
        ControllerPrivateAccessor(cntl).add_with_auth();
    }

    buf->append(request);
}

const std::string& GetRedisMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*) {
    const static std::string REDIS_SERVER_STR = "redis-server";
    return REDIS_SERVER_STR;
}

}  // namespace policy
} // namespace brpc
