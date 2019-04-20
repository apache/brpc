// Copyright (c) 2019 Baidu, Inc.
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

// Authors: Yang,Liming (yangliming01@baidu.com)

#include <google/protobuf/descriptor.h>  // MethodDescriptor
#include <google/protobuf/message.h>     // Message
#include <gflags/gflags.h>
#include <sstream>
#include "butil/logging.h"  // LOG()
#include "butil/time.h"
#include "butil/iobuf.h"  // butil::IOBuf
#include "butil/sys_byteorder.h"
#include "brpc/controller.h"  // Controller
#include "brpc/details/controller_private_accessor.h"
#include "brpc/socket.h"  // Socket
#include "brpc/server.h"  // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/mysql.h"
#include "brpc/mysql_reply.h"
#include "brpc/policy/mysql_protocol.h"
#include "brpc/policy/mysql_authenticator.h"

namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

DEFINE_bool(mysql_verbose, false, "[DEBUG] Print EVERY mysql request/response");

namespace {
// I really don't add a variable in controller, so I need to set auth step to AuthContext group
const char* mysql_auth_step[] = {"AUTH_STEP_RESPONSED", "AUTH_STEP_OK"};

struct InputResponse : public InputMessageBase {
    bthread_id_t id_wait;
    MysqlResponse response;

    // @InputMessageBase
    void DestroyImpl() {
        delete this;
    }
};
}  // namespace

void MysqlParseAuth(const butil::StringPiece& raw,
                    std::string* user,
                    std::string* password,
                    std::string* schema);
void MysqlParseParams(const butil::StringPiece& raw, std::string* params);
// pack mysql authentication_data
int MysqlPackAuthenticator(const MysqlReply::Auth& auth,
                           const butil::StringPiece& user,
                           const butil::StringPiece& password,
                           const butil::StringPiece& schema,
                           std::string* auth_cmd);
int MysqlPackParams(const butil::StringPiece& params, std::string* param_cmd);
bool MysqlHandleParams(const butil::StringPiece& params, std::string* param_cmd);

ParseError HandleAuthentication(const InputResponse* msg, const Socket* socket, PipelinedInfo* pi) {
    const bthread_id_t cid = pi->id_wait;
    Controller* cntl = NULL;
    if (bthread_id_lock(cid, (void**)&cntl) != 0) {
        LOG(ERROR) << "[MYSQL PARSE] fail to lock controller";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    ParseError parseCode = PARSE_OK;
    const AuthContext* ctx = socket->auth_context();
    if (ctx == NULL) {
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        LOG(ERROR) << "[MYSQL PARSE] auth context is null";
        goto END_OF_AUTH;
    }
    if (msg->response.reply(0).is_auth()) {
        std::string user, password, schema, auth_cmd;
        const MysqlReply& reply = msg->response.reply(0);
        MysqlParseAuth(ctx->user(), &user, &password, &schema);
        if (MysqlPackAuthenticator(reply.auth(), user, password, schema, &auth_cmd) == 0) {
            butil::IOBuf buf;
            buf.append(auth_cmd);
            buf.cut_into_file_descriptor(socket->fd());
            const_cast<AuthContext*>(ctx)->set_group(mysql_auth_step[0]);
        } else {
            parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
            LOG(ERROR) << "[MYSQL PARSE] wrong pack authentication data";
        }
    } else if (msg->response.reply_size() > 0) {
        for (size_t i = 0; i < msg->response.reply_size(); ++i) {
            if (!msg->response.reply(i).is_ok()) {
                LOG(ERROR) << "[MYSQL PARSE] auth failed " << msg->response;
                parseCode = PARSE_ERROR_NO_RESOURCE;
                goto END_OF_AUTH;
            }
        }
        std::string params, params_cmd;
        MysqlParseParams(ctx->user(), &params);
        if (ctx->group() == mysql_auth_step[0] && !params.empty()) {
            if (MysqlPackParams(params, &params_cmd) == 0) {
                butil::IOBuf buf;
                buf.append(params_cmd);
                buf.cut_into_file_descriptor(socket->fd());
                const_cast<AuthContext*>(ctx)->set_group(mysql_auth_step[1]);
            } else {
                parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
                LOG(ERROR) << "[MYSQL PARSE] wrong pack params data";
            }
        } else {
            butil::IOBuf raw_req;
            raw_req.append(ctx->starter());
            raw_req.cut_into_file_descriptor(socket->fd());
            pi->with_auth = false;
        }
    } else {
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        LOG(ERROR) << "[MYSQL PARSE] wrong authentication step";
    }

END_OF_AUTH:
    if (bthread_id_unlock(cid) != 0) {
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        LOG(ERROR) << "[MYSQL PARSE] fail to unlock controller";
    }
    return parseCode;
}
// "Message" = "Response" as we only implement the client for mysql.
ParseResult ParseMysqlMessage(butil::IOBuf* source,
                              Socket* socket,
                              bool /*read_eof*/,
                              const void* /*arg*/) {
    if (source->empty()) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    PipelinedInfo pi;
    if (!socket->PopPipelinedInfo(&pi)) {
        LOG(WARNING) << "No corresponding PipelinedInfo in socket";
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    InputResponse* msg = static_cast<InputResponse*>(socket->parsing_context());
    if (msg == NULL) {
        msg = new InputResponse;
        socket->reset_parsing_context(msg);
    }

    ParseError err = msg->response.ConsumePartialIOBuf(*source, pi.with_auth);
    if (err != PARSE_OK) {
        socket->GivebackPipelinedInfo(pi);
        return MakeParseError(err);
    }
    if (FLAGS_mysql_verbose) {
        LOG(INFO) << "[MYSQL PARSE] " << msg->response;
    }
    if (pi.with_auth) {
        ParseError err = HandleAuthentication(msg, socket, &pi);
        if (err != PARSE_OK) {
            return MakeParseError(err, "Fail to authenticate with Mysql");
        }
        DestroyingPtr<InputResponse> auth_msg =
            static_cast<InputResponse*>(socket->release_parsing_context());
        socket->GivebackPipelinedInfo(pi);
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    msg->id_wait = pi.id_wait;
    socket->release_parsing_context();
    return MakeMessage(msg);
}

void ProcessMysqlResponse(InputMessageBase* msg_base) {
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
        if (cntl->response()->GetDescriptor() != MysqlResponse::descriptor()) {
            cntl->SetFailed(ERESPONSE, "Must be MysqlResponse");
        } else {
            // We work around ParseFrom of pb which is just a placeholder.
            ((MysqlResponse*)cntl->response())->Swap(&msg->response);
            if (FLAGS_mysql_verbose) {
                LOG(INFO) << "\n[MYSQL RESPONSE] " << *((MysqlResponse*)cntl->response());
            }
        }
    }  // silently ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void SerializeMysqlRequest(butil::IOBuf* buf,
                           Controller* cntl,
                           const google::protobuf::Message* request) {
    if (request == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    if (request->GetDescriptor() != MysqlRequest::descriptor()) {
        return cntl->SetFailed(EREQUEST, "The request is not a MysqlRequest");
    }
    const MysqlRequest* rr = (const MysqlRequest*)request;
    // We work around SerializeTo of pb which is just a placeholder.
    if (!rr->SerializeTo(buf)) {
        return cntl->SetFailed(EREQUEST, "Fail to serialize MysqlRequest");
    }
    ControllerPrivateAccessor(cntl).set_pipelined_count(1);
    auto tx = rr->get_tx();
    if (tx != NULL) {
        ControllerPrivateAccessor(cntl).use_bind_sock(tx->GetSocketId());
    }
    if (FLAGS_mysql_verbose) {
        LOG(INFO) << "\n[MYSQL REQUEST] " << *rr;
    }
}

void PackMysqlRequest(butil::IOBuf* buf,
                      SocketMessage**,
                      uint64_t /*correlation_id*/,
                      const google::protobuf::MethodDescriptor*,
                      Controller* cntl,
                      const butil::IOBuf& request,
                      const Authenticator* auth) {
    if (auth) {
        const MysqlAuthenticator* my_auth(dynamic_cast<const MysqlAuthenticator*>(auth));
        if (my_auth == NULL) {
            LOG(ERROR) << "[MYSQL PACK] there is not MysqlAuthenticator";
            return;
        }
        Socket* sock = ControllerPrivateAccessor(cntl).get_sending_socket();
        if (sock == NULL) {
            LOG(ERROR) << "[MYSQL PACK] get sending socket with NULL";
            return;
        }
        AuthContext* ctx = sock->mutable_auth_context();
        std::string params;
        if (!MysqlHandleParams(my_auth->params(), &params)) {
            LOG(ERROR) << "[MYSQL PACK] handle params error";
            return;
        }
        std::stringstream ss;
        ss << my_auth->user() << "\t" << my_auth->passwd() << "\t" << my_auth->schema() << "\t"
           << params;
        ctx->set_user(ss.str());
        ctx->set_starter(request.to_string());
        ControllerPrivateAccessor(cntl).add_with_auth();
    } else {
        buf->append(request);
    }
}

const std::string& GetMysqlMethodName(const google::protobuf::MethodDescriptor*,
                                      const Controller*) {
    const static std::string MYSQL_SERVER_STR = "mysql-server";
    return MYSQL_SERVER_STR;
}

}  // namespace policy
}  // namespace brpc
