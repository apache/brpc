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

//          Jiashun Zhu(zhujiashun2010@gmail.com)

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>
#include <queue>
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

// This class is as parsing_context in socket.
class RedisConnContext : public Destroyable  {
public:
    explicit RedisConnContext(const RedisService* rs)
        : redis_service(rs) {}

    ~RedisConnContext();
    // @Destroyable
    void Destroy() override;

    const RedisService* redis_service;
    // If user starts a transaction, transaction_handler indicates the
    // handler pointer that runs the transaction command.
    std::unique_ptr<RedisCommandHandler> transaction_handler;

    RedisCommandParser parser;
    butil::Arena arena;
};

int ConsumeCommand(RedisConnContext* ctx,
                   const std::vector<butil::StringPiece>& args,
                   bool last_of_batch,
                   RedisReply* output,
                   RedisCommandHandlerResult* result) {
    if (ctx->transaction_handler) {
        *result = ctx->transaction_handler->Run(args, output, last_of_batch);
        if (result->state == REDIS_CMD_HANDLED) {
            ctx->transaction_handler.reset(NULL);
        } else if (result->state == REDIS_CMD_WAIT) {
            LOG(ERROR) << "REDIS_CMD_WAIT should not be returned by a transaction handler.";
            return -1;
        }
    } else {
        RedisCommandHandler* ch = ctx->redis_service->FindCommandHandler(args[0]);
        if (!ch) {
            char buf[64];
            snprintf(buf, sizeof(buf), "ERR unknown command `%s`", args[0].as_string().c_str());
            output->SetError(buf);
        } else {
            *result = ch->Run(args, output, last_of_batch);
            switch (result->state) {
                case REDIS_CMD_CONTINUE:
                    ctx->transaction_handler.reset(ch->NewTransactionHandler());
                    break;
                case REDIS_CMD_HANDLED:
                    // fall through
                case REDIS_CMD_WAIT:
                    break;
                default:
                    return -1;
            }
        }
    }
    return 0;
}

// ========== impl of RedisConnContext ==========

RedisConnContext::~RedisConnContext() { }

void RedisConnContext::Destroy() {
    delete this;
}

// ========== impl of RedisConnContext ==========

ParseResult ParseRedisMessage(butil::IOBuf* source, Socket* socket,
                              bool read_eof, const void* arg) {
    if (read_eof || source->empty()) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const Server* server = static_cast<const Server*>(arg);
    if (server) {
        const RedisService* const rs = server->options().redis_service;
        if (!rs) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        RedisConnContext* ctx = static_cast<RedisConnContext*>(socket->parsing_context());
        if (ctx == NULL) {
            ctx = new RedisConnContext(rs);
            socket->reset_parsing_context(ctx);
        }
        std::vector<butil::StringPiece> current_args;
        butil::IOBufAppender appender;
        ParseError err = PARSE_OK;
        std::queue<RedisCommandHandlerResult> result_queue;
        std::queue<RedisReply> reply_queue;

        err = ctx->parser.Consume(*source, &current_args, &ctx->arena);
        if (err != PARSE_OK) {
            return MakeParseError(err);
        }
        while (true) {
            std::vector<butil::StringPiece> next_args;
            err = ctx->parser.Consume(*source, &next_args, &ctx->arena);
            if (err != PARSE_OK) {
                break;
            }
            result_queue.emplace();
            reply_queue.emplace(&ctx->arena);
            if (ConsumeCommand(ctx, current_args, false,
                        &reply_queue.back(), &result_queue.back()) != 0) {
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            current_args.swap(next_args);
        }
        result_queue.emplace();
        reply_queue.emplace(&ctx->arena);
        if (ConsumeCommand(ctx, current_args,
                    true /*must be the last message*/,
                    &reply_queue.back(), &result_queue.back()) != 0) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        while (!result_queue.empty()) {
            RedisCommandHandlerResult& result = result_queue.front();
            RedisReply& output = reply_queue.front();
            switch (result.state) {
                case REDIS_CMD_HANDLED:
                    // fall through
                case REDIS_CMD_CONTINUE:
                    break;
                case REDIS_CMD_WAIT:
                    result.waitobj->Wait();
                    break;
                default:
                    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            output.SerializeTo(&appender);
            if (result.waitobj) {
                delete result.waitobj;
            }
            result_queue.pop();
            reply_queue.pop();
        }
        CHECK(reply_queue.empty());
        butil::IOBuf sendbuf;
        appender.move_to(sendbuf);
        CHECK(!sendbuf.empty());
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        LOG_IF(WARNING, socket->Write(&sendbuf, &wopt) != 0)
            << "Fail to send redis reply";
        ctx->arena.clear();
        return MakeParseError(err);
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
                    !(msg->response.reply(0).type() == brpc::REDIS_REPLY_STATUS &&
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
    } else {
        ControllerPrivateAccessor(cntl).clear_with_auth();
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
