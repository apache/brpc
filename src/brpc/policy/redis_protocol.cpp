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
#include "brpc/policy/redis_authenticator.h"
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
        : redis_service(rs)
        , batched_size(0) {
        user_ctx.reset(rs->NewConnectionContext());
    }

    ~RedisConnContext();
    // @Destroyable
    void Destroy() override;

    const RedisService* redis_service;

    std::unique_ptr<ConnectionContext> user_ctx;

    // >0 if command handler is run in batched mode.
    int batched_size;

    RedisCommandParser parser;
    butil::Arena arena;
};

int ConsumeCommand(RedisConnContext* ctx,
                   const std::vector<butil::StringPiece>& args,
                   bool flush_batched,
                   butil::IOBufAppender* appender) {
    RedisReply output(&ctx->arena);
    RedisCommandHandlerResult result = REDIS_CMD_HANDLED;

    result = ctx->redis_service->DispatchCommand(ctx->user_ctx.get(), args, &output, flush_batched);

    if (result == REDIS_CMD_HANDLED) {
        if (ctx->batched_size) {
            if ((int)output.size() != (ctx->batched_size + 1)) {
                LOG(ERROR) << "reply array size can't be matched with batched size, "
                           << " expected=" << ctx->batched_size + 1 << " actual=" << output.size();
                return -1;
            }
            for (int i = 0; i < (int)output.size(); ++i) {
                output[i].SerializeTo(appender);
            }
            ctx->batched_size = 0;
        } else {
            output.SerializeTo(appender);
        }
    } else if (result == REDIS_CMD_CONTINUE) {
        if (ctx->batched_size != 0)
        {
            LOG(ERROR) << "CONTINUE should not be returned in a "
                          "batched process.";
            return -1;
        }
        output.SerializeTo(appender);
    } else if (result == REDIS_CMD_BATCHED) {
        ctx->batched_size++;
        // just do nothing and wait handler to return OK.
    } else {
        LOG(ERROR) << "unknown status=" << result;
        return -1;
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
            if (ConsumeCommand(ctx, current_args, false, &appender) != 0) {
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            current_args.swap(next_args);
        }
        if (ConsumeCommand(ctx, current_args,
                    true /*must be the last message*/, &appender) != 0) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        butil::IOBuf sendbuf;
        appender.move_to(sendbuf);
        CHECK(!sendbuf.empty());
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        LOG_IF(WARNING, socket->Write(&sendbuf, &wopt) != 0)
            << "Fail to send redis reply";
        if(ctx->parser.ParsedArgsSize() == 0) {
            ctx->arena.clear();
        }
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

            const int consume_count = (pi.auth_flags ? pi.auth_flags : pi.count);

            ParseError err = msg->response.ConsumePartialIOBuf(*source, consume_count);
            if (err != PARSE_OK) {
                socket->GivebackPipelinedInfo(pi);
                return MakeParseError(err);
            }

            if (pi.auth_flags) {
                for (int i = 0; i < (int)pi.auth_flags; ++i) {
                    if (i >= msg->response.reply_size() ||
                        !(msg->response.reply(i).type() ==
                              brpc::REDIS_REPLY_STATUS &&
                          msg->response.reply(i).data().compare("OK") == 0)) {
                        LOG(ERROR) << "Redis Auth failed: " << msg->response;
                        return MakeParseError(PARSE_ERROR_NO_RESOURCE,
                            "Fail to authenticate with Redis");
                    }
                }

                DestroyingPtr<InputResponse> auth_msg(
                     static_cast<InputResponse*>(socket->release_parsing_context()));
                pi.auth_flags = 0;
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
    msg.reset();  // optional, just release resource ASAP
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
    // If redis byte size is zero, brpc call will fail with E22. Continuous E22 may cause E112 in the end.
    // So set failed and return useful error message
    if (rr->ByteSize() == 0) {
        return cntl->SetFailed(EREQUEST, "request byte size is empty");
    }
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
        const RedisAuthenticator* redis_auth =
            dynamic_cast<const RedisAuthenticator*>(auth);
        if (redis_auth == NULL) {
            return cntl->SetFailed(EREQUEST, "Fail to generate credential");
        }
        ControllerPrivateAccessor(cntl).set_auth_flags(
            redis_auth->GetAuthFlags());
    } else {
        ControllerPrivateAccessor(cntl).clear_auth_flags();
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
