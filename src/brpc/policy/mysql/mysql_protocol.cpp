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
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "brpc/policy/mysql/mysql_protocol.h"
#include "brpc/policy/mysql/mysql_auth_scramble.h"

namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

DEFINE_bool(mysql_verbose, false, "Print all mysql requests and responses");

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
// The connection-phase handshake spans several packets, so it needs per-connection
// (not per-RPC) scratch state. Rather than add a field to the shared Controller, we
// reuse the per-connection AuthContext: group() tracks the auth step, and (for
// caching_sha2_password below) roles() stashes the salt across the RSA round trip.
const char* auth_step[] = {"AUTH_OK", "PARAMS_OK"};

// Extra AuthContext group/state markers for the caching_sha2_password
// multi-round-trip exchange.  After the client sends the 32-byte fast
// scramble in its HandshakeResponse41 (group still default/empty), the
// server may answer with an AuthMoreData status byte.  These markers track
// where we are in that follow-up handshake so re-entries pick the right
// branch:
//   CACHE_SHA2_SENT   : sent the fast scramble; awaiting the server's
//                       AuthMoreData (0x03 fast-auth / 0x04 full-auth) or OK.
//   CACHE_SHA2_PUBKEY : on plain TCP full-auth, we requested the RSA public
//                       key (sent 0x02); awaiting the AuthMoreData carrying
//                       the PEM, after which we send the RSA-encrypted pw.
const char* kCacheSha2Sent = "CACHE_SHA2_SENT";
const char* kCacheSha2Pubkey = "CACHE_SHA2_PUBKEY";

// Frames |payload| as a single MySQL packet: 3-byte little-endian payload
// length + 1-byte sequence id, then the payload, and writes it to |fd|.
// |seq| is the sequence id the packet must carry (the previous server
// packet's seq + 1, per the MySQL packet-sequence rule).
static void WriteMysqlAuthPacket(int fd, const std::string& payload, uint8_t seq) {
    butil::IOBuf buf;
    const uint32_t len = butil::ByteSwapToLE32((uint32_t)payload.size());
    buf.append(&len, 3);
    buf.push_back((char)seq);
    buf.append(payload);
    buf.cut_into_file_descriptor(fd);
}

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
        auto stub = static_cast<MysqlStatementStub*>(accessor.session_data());
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
            accessor.set_mysql_statement_type(MYSQL_NEED_PREPARE);
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
            const ssize_t nw = buf.cut_into_file_descriptor(socket->fd());
            if (nw < 0 || !buf.empty()) {
                LOG(WARNING) << "[MYSQL PARSE] failed to write auth command to fd="
                             << socket->fd() << ", nw=" << nw
                             << ", remaining=" << buf.size();
            }
            const bool is_caching_sha2 = (reply.auth().auth_plugin() == "caching_sha2_password");
            if (is_caching_sha2) {
                // caching_sha2_password is a multi-round-trip exchange: stash
                // the 20-byte salt (greeting salt + salt2) for a later RSA
                // full-auth round, and mark that the fast scramble was sent.
                // _roles is otherwise unused on the mysql path.
                std::string salt;
                salt.append(reply.auth().salt().data(), reply.auth().salt().size());
                salt.append(reply.auth().salt2().data(), reply.auth().salt2().size());
                const_cast<AuthContext*>(ctx)->set_roles(salt);
                const_cast<AuthContext*>(ctx)->set_group(kCacheSha2Sent);
            } else {
                const_cast<AuthContext*>(ctx)->set_group(auth_step[0]);
            }
        } else {
            parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
            LOG(ERROR) << "[MYSQL PARSE] wrong pack authentication data";
        }
    } else if (msg->response.reply(0).is_auth_more_data()) {
        // caching_sha2_password follow-up packet (server -> client).  The
        // first data byte after the 0x01 tag is a status marker, except when
        // we are awaiting the RSA public key (CACHE_SHA2_PUBKEY), in which
        // case the whole payload is the PEM public key.
        std::string user, password, schema, collation;
        MysqlParseAuthenticator(ctx->user(), &user, &password, &schema, &collation);
        const MysqlReply::AuthMoreData& amd = msg->response.reply(0).auth_more_data();
        const butil::StringPiece data = amd.data();
        const uint8_t next_seq = (uint8_t)(amd.seq() + 1);
        if (ctx->group() == kCacheSha2Pubkey) {
            // The payload is the server's PEM RSA public key.  Encrypt the
            // password with it (plain-TCP full-auth) and send the ciphertext.
            const std::string rsa = mysql::CachingSha2PasswordRsaEncrypt(
                data, ctx->roles(), password);
            if (rsa.empty()) {
                parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
                LOG(ERROR) << "[MYSQL PARSE] failed to RSA-encrypt caching_sha2 password";
                goto END_OF_AUTH;
            }
            WriteMysqlAuthPacket(socket->fd(), rsa, next_seq);
            // Stay in CACHE_SHA2_SENT-equivalent: the server replies OK next.
            const_cast<AuthContext*>(ctx)->set_group(kCacheSha2Sent);
        } else if (!data.empty() && (uint8_t)data[0] == 0x03) {
            // fast_auth_success: server will send OK next; send nothing.
            const_cast<AuthContext*>(ctx)->set_group(kCacheSha2Sent);
        } else if (!data.empty() && (uint8_t)data[0] == 0x04) {
            // perform_full_authentication.
            if (socket->is_ssl()) {
                // Secure channel: send the cleartext password (one round trip).
                const std::string clear = mysql::CachingSha2PasswordCleartext(password);
                WriteMysqlAuthPacket(socket->fd(), clear, next_seq);
                const_cast<AuthContext*>(ctx)->set_group(kCacheSha2Sent);
            } else {
                // Plain TCP: request the server's RSA public key (0x02), then
                // wait for the AuthMoreData carrying the PEM.
                WriteMysqlAuthPacket(socket->fd(), std::string(1, (char)0x02), next_seq);
                const_cast<AuthContext*>(ctx)->set_group(kCacheSha2Pubkey);
            }
        } else {
            parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
            LOG(ERROR) << "[MYSQL PARSE] unexpected caching_sha2 AuthMoreData marker";
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
        // Auth just completed (either native's single round trip, group
        // AUTH_OK, or caching_sha2's multi round-trip, group CACHE_SHA2_SENT)
        // and connection params have not been sent yet: send them now.
        const bool auth_just_done =
            (ctx->group() == auth_step[0] || ctx->group() == kCacheSha2Sent);
        if (auth_just_done && !params.empty()) {
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
            pi->auth_flags = 0;
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
    MysqlStatementStub* stub = NULL;
    MysqlStatement* stmt = NULL;
    stub = static_cast<MysqlStatementStub*>(ControllerPrivateAccessor(cntl).session_data());
    if (stub == NULL) {
        LOG(ERROR) << "[MYSQL PACK] get prepare statement with NULL";
        parseCode = PARSE_ERROR_ABSOLUTELY_WRONG;
        goto END_OF_PREPARE;
    }
    stmt = stub->stmt();
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
    {
        const ssize_t nw = buf.cut_into_file_descriptor(socket->fd());
        if (nw < 0 || !buf.empty()) {
            LOG(WARNING) << "[MYSQL PARSE] failed to write execute command to fd="
                         << socket->fd() << ", nw=" << nw
                         << ", remaining=" << buf.size();
        }
    }
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
    ParseError err = msg->response.ConsumePartialIOBuf(*source, pi.auth_flags != 0, stmt_type);
    if (FLAGS_mysql_verbose) {
        LOG(INFO) << "[MYSQL PARSE] " << msg->response;
    }
    if (err != PARSE_OK) {
        if (err == PARSE_ERROR_NOT_ENOUGH_DATA) {
            socket->GivebackPipelinedInfo(pi);
        }
        return MakeParseError(err);
    }
    if (pi.auth_flags) {
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
        // A failed PREPARE (e.g. ER_PARSE_ERROR 1064) comes back as a normal
        // ERR packet. Deliver it to the caller like any other error response
        // and keep the connection open -- matching the command path and other
        // protocols (redis, baidu_std). Only a successful prepare proceeds to
        // pack and send the COM_STMT_EXECUTE.
        if (!msg->response.reply(0).is_prepare_ok()) {
            msg->id_wait = pi.id_wait;
            socket->release_parsing_context();
            return MakeMessage(msg);
        }
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
    // Controller::span() returns a std::shared_ptr<Span> in current master
    // (was a raw Span* when #2093 was written).
    if (auto span = accessor.span()) {
        span->set_base_real_us(msg->base_real_us());
        span->set_received_us(msg->received_us());
        span->set_response_size(msg->response.ByteSize());
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    if (cntl->response() != NULL) {
        if (cntl->response()->GetDescriptor() != MysqlResponse::descriptor()) {
            LOG(ERROR) << "[MYSQL PROCESS] response message is not a MysqlResponse";
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
        LOG(ERROR) << "[MYSQL SERIALIZE] request is NULL";
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    if (request->GetDescriptor() != MysqlRequest::descriptor()) {
        LOG(ERROR) << "[MYSQL SERIALIZE] request message is not a MysqlRequest";
        return cntl->SetFailed(EREQUEST, "The request is not a MysqlRequest");
    }
    const MysqlRequest* rr = (const MysqlRequest*)request;
    // We work around SerializeTo of pb which is just a placeholder.
    if (!rr->SerializeTo(buf)) {
        LOG(ERROR) << "[MYSQL SERIALIZE] failed to serialize MysqlRequest to IOBuf";
        return cntl->SetFailed(EREQUEST, "Fail to serialize MysqlRequest");
    }
    // mysql doesn't use pipelined_count to verify the end of a response; instead we
    // reuse it as a MysqlStmtType tag so the parse function knows which reply shape
    // to expect (OK and PrepareOk are otherwise indistinguishable). Default to
    // MYSQL_NORMAL_STATEMENT (1); it is upgraded to MYSQL_PREPARED_STATEMENT (2)
    // below when the request carries a prepared statement.
    ControllerPrivateAccessor accessor(cntl);
    accessor.set_mysql_statement_type(MYSQL_NORMAL_STATEMENT);

    auto tx = rr->tx();
    if (tx != NULL) {
        accessor.use_bind_sock(tx->GetSocketId());
    }
    auto st = rr->stmt();
    if (st != NULL) {
        accessor.set_session_data(rr->stmt());
        accessor.set_mysql_statement_type(MYSQL_PREPARED_STATEMENT);
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
        // Mark this as an auth write so the connection-phase handshake is run
        // and the (empty) data buffer is allowed through Socket::Write.  Mirrors
        // redis's set_auth_flags(); 1 == "this pipelined slot is the auth reply".
        accessor.set_auth_flags(1);
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
