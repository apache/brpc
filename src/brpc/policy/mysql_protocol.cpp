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
#include "brpc/policy/mysql_authenticator.h"
#include "brpc/policy/mysql_protocol.h"

namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

DEFINE_bool(mysql_verbose, false, "[DEBUG] Print EVERY mysql request/response");

void MysqlParseAuthenticator(const butil::StringPiece& raw,
                             std::string* user,
                             std::string* password,
                             std::string* schema,
                             std::string* collation);
void MysqlParseParams(const butil::StringPiece& raw, std::string* params);
// pack mysql authentication_data
int MysqlPackAuthenticator(const MysqlReply::Auth& auth,
                           const butil::StringPiece& user,
                           const butil::StringPiece& password,
                           const butil::StringPiece& schema,
                           const butil::StringPiece& collation,
                           std::string* auth_cmd);
int MysqlPackParams(const butil::StringPiece& params, std::string* param_cmd);

namespace {
// I really don't want to add a variable in controller, so I use AuthContext group to mark auth
// step.
const char* auth_step[] = {"AUTH_OK", "PARAMS_OK"};

struct InputResponse : public InputMessageBase {
    bthread_id_t id_wait;
    MysqlResponse response;

    // @InputMessageBase
    void DestroyImpl() {
        delete this;
    }
};

bool PackRequest(butil::IOBuf* buf,
                 ControllerPrivateAccessor& accessor,
                 const butil::IOBuf& request) {
    if (accessor.pipelined_count() == MYSQL_PREPARED_STATEMENT) {
        Socket* sock = accessor.get_sending_socket();
        if (sock == NULL) {
            LOG(ERROR) << "[MYSQL PACK] get sending socket with NULL";
            return false;
        }
        auto stub = accessor.get_stmt();
        if (stub == NULL) {
            LOG(ERROR) << "[MYSQL PACK] get prepare statement with NULL";
            return false;
        }
        uint32_t stmt_id;
        // if can't found stmt_id in this socket, create prepared statement on it, store user
        // request.
        if ((stmt_id = stub->stmt()->StatementId(sock->id())) == 0) {
            butil::IOBuf b;
            butil::Status st = MysqlMakeCommand(&b, MYSQL_COM_STMT_PREPARE, stub->stmt()->str());
            if (!st.ok()) {
                LOG(ERROR) << "[MYSQL PACK] make prepare statement error " << st;
                return false;
            }
            accessor.set_pipelined_count(MYSQL_NEED_PREPARE);
            buf->append(b);
            return true;
        }
        // else pack execute header with stmt_id
        butil::Status st = stub->PackExecuteCommand(buf, stmt_id);
        if (!st.ok()) {
            LOG(ERROR) << "write execute data error " << st;
            return false;
        }
        return true;
    }
    buf->append(request);
    return true;
}

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
        std::string user, password, schema, collation, auth_cmd;
        const MysqlReply& reply = msg->response.reply(0);
        MysqlParseAuthenticator(ctx->user(), &user, &password, &schema, &collation);
        if (MysqlPackAuthenticator(reply.auth(), user, password, schema, collation, &auth_cmd) ==
            0) {
            butil::IOBuf buf;
            buf.append(auth_cmd);
            buf.cut_into_file_descriptor(socket->fd());
            const_cast<AuthContext*>(ctx)->set_group(auth_step[0]);
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
        if (ctx->group() == auth_step[0] && !params.empty()) {
            if (MysqlPackParams(params, &params_cmd) == 0) {
                butil::IOBuf buf;
                buf.append(params_cmd);
                buf.cut_into_file_descriptor(socket->fd());
                const_cast<AuthContext*>(ctx)->set_group(auth_step[1]);
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

ParseError HandlePrepareStatement(const InputResponse* msg,
                                  const Socket* socket,
                                  PipelinedInfo* pi) {
    if (!msg->response.reply(0).is_prepare_ok()) {
        LOG(ERROR) << "[MYSQL PARSE] response is not prepare ok, " << msg->response;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    const MysqlReply::PrepareOk& ok = msg->response.reply(0).prepare_ok();
    const bthread_id_t cid = pi->id_wait;
    Controller* cntl = NULL;
    if (bthread_id_lock(cid, (void**)&cntl) != 0) {
        LOG(ERROR) << "[MYSQL PARSE] fail to lock controller";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    ParseError parseCode = PARSE_OK;
    butil::IOBuf buf;
    butil::Status st;
    auto stub = ControllerPrivateAccessor(cntl).get_stmt();
    auto stmt = stub->stmt();
    if (stmt == NULL || stmt->param_count() != ok.param_count()) {
        LOG(ERROR) << "[MYSQL PACK] stmt can't be NULL";
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        goto END_OF_PREPARE;
    }
    if (stmt->param_count() != ok.param_count()) {
        LOG(ERROR) << "[MYSQL PACK] stmt param number " << stmt->param_count()
                   << " not equal to prepareOk.param_number " << ok.param_count();
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        goto END_OF_PREPARE;
    }
    stmt->SetStatementId(socket->id(), ok.stmt_id());
    st = stub->PackExecuteCommand(&buf, ok.stmt_id());
    if (!st.ok()) {
        LOG(ERROR) << "[MYSQL PACK] make execute header error " << st;
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        goto END_OF_PREPARE;
    }
    buf.cut_into_file_descriptor(socket->fd());
    pi->count = MYSQL_PREPARED_STATEMENT;
END_OF_PREPARE:
    if (bthread_id_unlock(cid) != 0) {
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        LOG(ERROR) << "[MYSQL PARSE] fail to unlock controller";
    }
    return parseCode;
}

}  // namespace

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

    MysqlStmtType stmt_type = static_cast<MysqlStmtType>(pi.count);
    ParseError err = msg->response.ConsumePartialIOBuf(*source, pi.with_auth, stmt_type);
    if (FLAGS_mysql_verbose) {
        LOG(INFO) << "[MYSQL PARSE] " << msg->response;
    }
    if (err != PARSE_OK) {
        if (err == PARSE_ERROR_NOT_ENOUGH_DATA) {
            socket->GivebackPipelinedInfo(pi);
        }
        return MakeParseError(err);
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
    if (stmt_type == MYSQL_NEED_PREPARE) {
        // store stmt_id, make execute header.
        ParseError err = HandlePrepareStatement(msg, socket, &pi);
        if (err != PARSE_OK) {
            return MakeParseError(err, "Fail to make parepared statement with Mysql");
        }
        DestroyingPtr<InputResponse> prepare_msg =
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
    // mysql protocol don't use pipelined count to verify the end of a response, so pipelined count
    // is meanless, but we can use it help us to distinguish mysql reply type. In mysql protocol, we
    // can't distinguish OK and PreparedOk, so we set pipelined count to 2 to let parse function to
    // parse PreparedOk reply
    ControllerPrivateAccessor accessor(cntl);
    accessor.set_pipelined_count(MYSQL_NORMAL_STATEMENT);

    auto tx = rr->get_tx();
    if (tx != NULL) {
        accessor.use_bind_sock(tx->GetSocketId());
    }
    auto st = rr->get_stmt();
    if (st != NULL) {
        accessor.set_stmt(st);
        accessor.set_pipelined_count(MYSQL_PREPARED_STATEMENT);
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
    ControllerPrivateAccessor accessor(cntl);
    if (auth) {
        const MysqlAuthenticator* my_auth(dynamic_cast<const MysqlAuthenticator*>(auth));
        if (my_auth == NULL) {
            LOG(ERROR) << "[MYSQL PACK] there is not MysqlAuthenticator";
            return;
        }
        Socket* sock = accessor.get_sending_socket();
        if (sock == NULL) {
            LOG(ERROR) << "[MYSQL PACK] get sending socket with NULL";
            return;
        }
        AuthContext* ctx = sock->mutable_auth_context();
        // std::string params;
        // if (!MysqlHandleParams(my_auth->params(), &params)) {
        //     LOG(ERROR) << "[MYSQL PACK] handle params error";
        //     return;
        // }
        // std::stringstream ss;
        // ss << my_auth->user() << "\t" << my_auth->passwd() << "\t" << my_auth->schema() << "\t"
        //    << my_auth->collation() << "\t" << params;
        std::string str;
        if (!my_auth->SerializeToString(&str)) {
            LOG(ERROR) << "[MYSQL PACK] auth param serialize to string failed";
            return;
        }
        ctx->set_user(str);
        butil::IOBuf b;
        if (!PackRequest(&b, accessor, request)) {
            LOG(ERROR) << "[MYSQL PACK] pack request error";
            return;
        }
        ctx->set_starter(b.to_string());
        accessor.add_with_auth();
    } else {
        if (!PackRequest(buf, accessor, request)) {
            LOG(ERROR) << "[MYSQL PACK] pack request error";
            return;
        }
    }
}

const std::string& GetMysqlMethodName(const google::protobuf::MethodDescriptor*,
                                      const Controller*) {
    const static std::string MYSQL_SERVER_STR = "mysql-server";
    return MYSQL_SERVER_STR;
}

}  // namespace policy
}  // namespace brpc
