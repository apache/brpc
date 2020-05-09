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

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>

#include "butil/time.h" 
#include "butil/iobuf.h"                         // butil::IOBuf

#include "brpc/controller.h"               // Controller
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/span.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/mysql_meta.pb.h"
#include "brpc/details/usercode_backup_pool.h"
#include "butil/sys_byteorder.h"
#include "brpc/policy/my_command.h"
#include "brpc/policy/mysql_protocol.h"
#include "brpc/policy/mysql_com.h"

extern "C" {
void bthread_assign_data(void* data);
}

namespace brpc {
namespace policy {

// This class is as parsing_context in socket.
class MysqlConnContext : public Destroyable  {
public:
    explicit MysqlConnContext(uint8_t seq_id) :
        sequence_id(seq_id) {}

    ~MysqlConnContext() { }
    // @Destroyable
    void Destroy() override {
        delete this;
    }

    int8_t NextSequenceId() {
        return ++sequence_id;
    }

    void ResetSequenceId(uint8_t seq_id) {
        sequence_id = seq_id;
    }

    void SetCurrentDB(const std::string& db_name) {
        current_db = db_name;
    }

    butil::StringPiece CurrentDB() {
        return butil::StringPiece(current_db);
    }

    // the sequence id of the request packet from the client
    int8_t sequence_id;
    std::string current_db;
};



class MysqlProtocolPacketHeader {
public:
    MysqlProtocolPacketHeader() :
        payload_length_{0U, 0U, 0U},
        sequence_id_(0U) { }

    void SetPayloadLength(uint32_t payload_length) {
        butil::IntStore3Bytes(payload_length_, payload_length);
    }

    void SetSequenceId(uint8_t sequence_id) {
        sequence_id_ = sequence_id;
    }

    void AppendToIOBuf(butil::IOBuf& iobuf) const {
        iobuf.append(payload_length_, 4);
    }
private:
    uint8_t payload_length_[3];
    uint8_t sequence_id_;
};

class MysqlProtocolPacketBody {
public:
    butil::IOBuf& buf() {
        return buf_;
    }

    void Append(const void* data, size_t count) {
        buf_.append(data, count);
    }

    size_t length() const {
        return buf_.length();
    }

private:
    butil::IOBuf buf_;
};

const uint8_t packet_handshake_body[] =
    "\x0a"              // protocol version
    "5.0.51b\x00"       // human readable server version
    "\x00\x00\x00\x00"  // connection id
    "\x26\x4f\x37\x58"  // |
    "\x43\x7a\x6c\x53"  // `>auth-plugin-data-part-1
    "\x00"              // filter
    "\x0c\xa2"          // capability flags (lower 2 bytes)
    "\x1c"              // server language encoding :cp 1257 change to gbk
    "\x02\x00"          // server status 0x0002,SERVER_STATUS_AUTOCOMMIT
    "\x00\x00"          // capability flags (upper 2 bytes)
    "\x00"              // length of auth-plugin-data
    "\x00\x00\x00\x00"  // |
    "\x00\x00\x00\x00"  // |
    "\x00\x00"          // `> 10bytes reserved
    "\x21\x25\x65\x57"  // |
    "\x62\x35\x42\x66"  // |
    "\x6f\x34\x62\x49"  // |
    "\x00";             // `> auth-plugin-data-part-2 len=13

const uint8_t packet_ok_body[] =
    "\x00"              // header, 0x00 or 0xFE the OK packet header
    "\x00"              // affected_rows
    "\x00"              // last_insert_id
    "\x02\x00"          // status_flags, AUTOCOMMIT, Server in auto_commit mode
    "\x00\x00";         // warnings

static void AppendPacketHeaderToIOBuf(const MysqlProtocolPacketHeader& header, butil::IOBuf& iobuf) {
    header.AppendToIOBuf(iobuf);
}

static void AppendPacketBodyToIOBuf(MysqlProtocolPacketBody& body, butil::IOBuf& iobuf) {
    iobuf.append(body.buf().movable());
}

void ServerSendInitialPacketDemo(Socket* socket) {
    butil::IOBuf handshake_packet;

    MysqlProtocolPacketBody packet_body;
    packet_body.Append(packet_handshake_body,
                       sizeof(packet_handshake_body) - 1);

    MysqlProtocolPacketHeader packet_header;
    packet_header.SetPayloadLength(packet_body.length());
    packet_header.SetSequenceId(0U /* the start sequence id */);

    AppendPacketHeaderToIOBuf(packet_header, handshake_packet);
    AppendPacketBodyToIOBuf(packet_body, handshake_packet);

    if (socket->Write(&handshake_packet) != 0) {
        LOG(ERROR) << "Fail to send initial handshake packet to socket: "
            << socket->description();
    }
}

class MysqlRawUnpacker {
public:
    explicit MysqlRawUnpacker(const void* stream) :
        stream_((const char*)stream) { }

    MysqlRawUnpacker& unpack_uint3(uint32_t& hostvalue) {
        hostvalue = butil::UnsignedIntLoad3Bytes(stream_);
        stream_ += 3;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint4(uint32_t& hostvalue) {
        hostvalue = butil::UnsignedIntLoad4Bytes(stream_);
        stream_ += 4;
        return *this;
    }
    MysqlRawUnpacker& unpack_uint1(uint8_t& hostvalue) {
        hostvalue = *((const uint8_t*)stream_);
        stream_ += 1;
        return *this;
    }
private:
    const char* stream_;
};

ParseResult ParseMysqlMessage(
        butil::IOBuf* source,
        Socket* socket, 
        bool /*read_eof*/, 
        const void* /*arg*/) {
    char header_buf[4];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size;
    uint8_t sequence_id;
    MysqlRawUnpacker ru(header_buf);
    ru.unpack_uint3(body_size).unpack_uint1(sequence_id);

    if (body_size > FLAGS_max_body_size) {
        LOG(ERROR) << "body_size=" << body_size << " from "
            << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    } else if (body_size == 0xffffff) {
        // TODO: handle the more than 16M messages
        // if payload_length if 0xffffff, it has following packets.
        // but we do not support it now.
        LOG(ERROR) << "have not support more than 16M data";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    }

    MysqlConnContext* ctx = static_cast<MysqlConnContext*>(socket->parsing_context());
    if (ctx == nullptr) {
        ctx = new (std::nothrow) MysqlConnContext(sequence_id);
        if (ctx == nullptr) {
            LOG(ERROR) << "Fail to new MysqlConnContext for socket: "
                << socket->description();
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        socket->reset_parsing_context(ctx);
    }
    ctx->ResetSequenceId(sequence_id);

    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(header_buf));
    source->cutn(&msg->payload, body_size);
    return MakeMessage(msg);
}

static void SendOKPacket(Socket* socket, uint8_t sequence_id);

static butil::StringPiece GetPBMethodNameByCommandId(uint8_t command_id) {
    static std::map<uint8_t, std::string> cmd2name = {
        {COM_QUIT, "brpc.policy.MysqlService.Quit"},
        {COM_INIT_DB, "brpc.policy.MysqlService.InitDB"},
        {COM_REFRESH, "brpc.policy.MysqlService.Refresh"},
        {COM_STATISTICS, "brpc.policy.MysqlService.Statistics"},
        {COM_PROCESS_INFO, "brpc.policy.MysqlService.ProcessInfo"},
        {COM_PROCESS_KILL, "brpc.policy.MysqlService.ProcessKill"},
        {COM_DEBUG, "brpc.policy.MysqlService.Debug"},
        {COM_PING, "brpc.policy.MysqlService.Ping"},
        {COM_QUERY, "brpc.policy.MysqlService.Query"}
    };
    static const std::string default_method_name =
        "brpc.policy.MysqlService.UnknownMethod";

    const auto& citr = cmd2name.find(command_id);
    if (citr != cmd2name.end()) {
        return butil::StringPiece(citr->second);
    } else {
        return butil::StringPiece(default_method_name);
    }
}

static bool ParseQuit(
    uint8_t /*command_id*/,
    butil::IOBuf& /*payload*/,
    google::protobuf::Message* /*req_base*/) {
    return true;
}

static bool ParseInitDB(
    uint8_t /*command_id*/,
    butil::IOBuf& payload,
    google::protobuf::Message* req_base) {

    InitDBRequest* req = static_cast<InitDBRequest*>(req_base);
    req->set_db_name(payload.to_string());
    return true;
}

static bool ParseRefresh(
    uint8_t /*command_id*/,
    butil::IOBuf& payload,
    google::protobuf::Message* req_base) {

    uint8_t flags;
    payload.cutn(&flags, 1);
    RefreshRequest* req = static_cast<RefreshRequest*>(req_base);
    req->set_grant(flags & REFRESH_GRANT);
    req->set_log(flags & REFRESH_LOG);
    req->set_tables(flags & REFRESH_TABLES);
    req->set_hosts(flags & REFRESH_HOSTS);
    req->set_status(flags & REFRESH_STATUS);
    req->set_threads(flags & REFRESH_THREADS);
    req->set_slave(flags & REFRESH_SLAVE);
    req->set_master(flags & REFRESH_MASTER);

    return true;
}

static bool ParseStatistics(
    uint8_t /*command_id*/,
    butil::IOBuf& /*payload*/,
    google::protobuf::Message* /*req_base*/) {
    return true;
}

static bool ParseProcessInfo(
    uint8_t /*command_id*/,
    butil::IOBuf& /*payload*/,
    google::protobuf::Message* /*req_base*/) {
    return true;
}

static bool ParseProcessKill(
    uint8_t /*command_id*/,
    butil::IOBuf& payload,
    google::protobuf::Message* req_base) {

    uint32_t connection_id;
    char buffer[4];
    payload.copy_to(buffer, 4);
    MysqlRawUnpacker ru(buffer);
    ru.unpack_uint4(connection_id);

    auto* req = static_cast<ProcessKillRequest*>(req_base);
    req->set_connection_id(connection_id);

    return true;
}

static bool ParseDebug(
    uint8_t /*command_id*/,
    butil::IOBuf& /*payload*/,
    google::protobuf::Message* /*req_base*/) {
    return true;
}

static bool ParsePing(
    uint8_t /*command_id*/,
    butil::IOBuf& /*payload*/,
    google::protobuf::Message* /*req_base*/) {
    return true;
}

static bool ParseQuery(
    uint8_t /*command_id*/,
    butil::IOBuf& payload,
    google::protobuf::Message* req_base) {

    QueryRequest* req = static_cast<QueryRequest*>(req_base);
    req->set_sql(payload.to_string());
    return true;
}

static bool ParseUnknownMethod(
    uint8_t command_id,
    butil::IOBuf& ,
    google::protobuf::Message* msg) {

    UnknownMethodRequest* req = static_cast<UnknownMethodRequest*>(msg);
    req->set_command_id(command_id);
    return true;
}

static bool ParseFromRemainedPayload(
    uint8_t command_id,
    butil::IOBuf& payload,
    google::protobuf::Message* msg) {

    typedef bool (*ParseFunctionPtr)(
        uint8_t, butil::IOBuf&, google::protobuf::Message*);
    static std::map<uint8_t, ParseFunctionPtr> cmd2parser = {
        {COM_QUIT, ParseQuit},
        {COM_INIT_DB, ParseInitDB},
        {COM_REFRESH, ParseRefresh},
        {COM_STATISTICS, ParseStatistics},
        {COM_PROCESS_INFO, ParseProcessInfo},
        {COM_PROCESS_KILL, ParseProcessKill},
        {COM_DEBUG, ParseDebug},
        {COM_PING, ParsePing},
        {COM_QUERY, ParseQuery}
    };

    ParseFunctionPtr parser = ParseUnknownMethod;
    const auto& citr = cmd2parser.find(command_id);
    if (citr != cmd2parser.end()) {
        parser = citr->second;
    }
    CHECK(parser);
    return parser(command_id, payload, msg);
}

static void SendErrorPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req,
    const google::protobuf::Message* res) {
    // TODO:
    LOG(INFO) << "SendErrorPacket";
}

static void SendUnknownMethodPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req,
    const google::protobuf::Message* res) {
    LOG(INFO) << "SendUnknownMethodPacket";
    SendErrorPacket(command_id, cntl, socket, ctx, req, res);
}

static void SendQuitPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req,
    const google::protobuf::Message* res) {
    // Just close the connection on quit
    LOG(INFO) << "SendQuitPacket";
    socket->SetFailed();
}

static void SendInitDBPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendInitDB";

    const auto* req = static_cast<const InitDBRequest*>(req_base);
    const auto* res = static_cast<const InitDBResponse*>(res_base);
    if (res->error_code() == 0) {
        // set db_name to ctx
        ctx->SetCurrentDB(req->db_name());
        SendOKPacket(socket, ctx->NextSequenceId());
    } else {
        SendErrorPacket(command_id, cntl, socket, ctx, req_base, res_base);
    }
}

static void SendRefreshPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendRefresh";

    // COM_REFRESH is deprecated.
    // Send error packet back if cntl Failed is setted.
    // Otherwise ok packet.
    if (cntl->Failed()) {
        SendErrorPacket(command_id, cntl, socket, ctx, req_base, res_base);
    } else {
        SendOKPacket(socket, ctx->NextSequenceId());
    }
}

static void SendStatisticsPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendStatisticsPacket";

    const auto* res = static_cast<const StatisticsResponse*>(res_base);
    // make the header
    MysqlProtocolPacketHeader packet_header;
    packet_header.SetPayloadLength(res->stats().length());
    packet_header.SetSequenceId(ctx->NextSequenceId());
    // composite header and body
    butil::IOBuf stats_packet;
    AppendPacketHeaderToIOBuf(packet_header, stats_packet);
    stats_packet.append(res->stats());
    // send
    socket->Write(&stats_packet);
}

static void SendProcessInfoPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendProcessInfoPacket";

    // COM_PROCESS_INFO is deprecated.
    // Send error packet back if cntl Failed is setted.
    // Otherwise text resultset.
    if (cntl->Failed()) {
        SendErrorPacket(command_id, cntl, socket, ctx, req_base, res_base);
    } else {
        //TODO: send text resultset
    }
}

static void SendProcessKillPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendProcessKillPacket";

    // COM_PROCESS_KILL is deprecated.
    // Send error packet back if cntl Failed is setted.
    // Otherwise ok packet.
    if (cntl->Failed()) {
        SendErrorPacket(command_id, cntl, socket, ctx, req_base, res_base);
    } else {
        SendOKPacket(socket, ctx->NextSequenceId());
    }
}

static void SendDebugPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendDebugPacket";

    // Send error packet back if cntl Failed is setted.
    // Otherwise ok packet.
    if (cntl->Failed()) {
        SendErrorPacket(command_id, cntl, socket, ctx, req_base, res_base);
    } else {
        SendOKPacket(socket, ctx->NextSequenceId());
    }
}

static void SendPingPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req_base,
    const google::protobuf::Message* res_base) {
    LOG(INFO) << "SendPingPacket";

    SendOKPacket(socket, ctx->NextSequenceId());
}

static void SendQueryPacket(
    uint8_t command_id, Controller* cntl,
    Socket* socket, MysqlConnContext* ctx,
    const google::protobuf::Message* req,
    const google::protobuf::Message* res) {
    LOG(INFO) << "SendQueryPacket";
    SendOKPacket(socket, ctx->NextSequenceId());
}

// Assemble response packet using `correlation_id', `controller',
// `res', and then write this packet to `sock'
static void SendMysqlResponse(
    uint8_t command_id,
    int64_t correlation_id,
    Controller* cntl, 
    const google::protobuf::Message* req,
    const google::protobuf::Message* res,
    const Server* server,
    MethodStatus* method_status,
    int64_t received_us) {

    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    Socket* socket = accessor.get_sending_socket();
    MysqlConnContext* ctx = static_cast<MysqlConnContext*>(socket->parsing_context());
    CHECK(ctx);
    std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);
    ConcurrencyRemover concurrency_remover(method_status, cntl, received_us);
    std::unique_ptr<const google::protobuf::Message> recycle_req(req);
    std::unique_ptr<const google::protobuf::Message> recycle_res(res);

    if (cntl->IsCloseConnection()) {
        socket->SetFailed();
        return;
    }

    LOG_IF(WARNING, !cntl->response_attachment().empty())
        << "mysql protocol does not support attachment, "
        "your response_attachment will not be sent";

    typedef void (*SendResponseByCommandIdFunctionPtr)(
        uint8_t, Controller*, Socket*, MysqlConnContext*,
        const google::protobuf::Message* req,
        const google::protobuf::Message* res);
    static std::map<uint8_t, SendResponseByCommandIdFunctionPtr> senders = {
        {COM_QUIT, SendQuitPacket},
        {COM_INIT_DB, SendInitDBPacket},
        {COM_REFRESH, SendRefreshPacket},
        {COM_STATISTICS, SendStatisticsPacket},
        {COM_PROCESS_INFO, SendProcessInfoPacket},
        {COM_PROCESS_KILL, SendProcessKillPacket},
        {COM_DEBUG, SendDebugPacket},
        {COM_DEBUG, SendPingPacket},
        {COM_QUERY, SendQueryPacket}
    };

    SendResponseByCommandIdFunctionPtr sender = SendUnknownMethodPacket;
    const auto& citr = senders.find(command_id);
    if (citr != senders.end()) {
        sender = citr->second;
    }
    CHECK(sender);
    sender(command_id, cntl, socket, ctx, req, res);
}

// Defined in baidu_rpc_protocol.cpp
void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done);

void ProcessMysqlRequest(InputMessageBase* msg_base) {
    const int64_t start_process_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    MysqlConnContext* ctx = static_cast<MysqlConnContext*>(socket->parsing_context());
    CHECK(ctx);

    if (msg->IsInitalResponseMessage()) {
        // This is the auth response message verified by VerifyMysqlRequest
        SendOKPacket(socket, ctx->NextSequenceId());
        LOG(INFO) << "Auth reponse message on socket: " << socket->description();
        return;
    }

    uint8_t command_id;
    auto n = msg->payload.copy_to(&command_id, sizeof(command_id));
    CHECK(n == sizeof(command_id));

    const Server* server = static_cast<const Server*>(msg->arg());
    ScopedNonServiceError non_service_error(server);

    std::unique_ptr<Controller> cntl(new (std::nothrow) Controller);
    if (cntl.get() == nullptr) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }
    std::unique_ptr<google::protobuf::Message> req;
    std::unique_ptr<google::protobuf::Message> res;

    ControllerPrivateAccessor accessor(cntl.get());
    ServerPrivateAccessor server_accessor(server);
    accessor.set_server(server)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_MYSQL)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

    const int64_t correlation_id = 0;

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(false)) {
        span = Span::CreateServerSpan(
            0/*meta.trace_id()*/, 0/*meta.span_id()*/,
            0/*meta.parent_span_id()*/, msg->base_real_us());
        accessor.set_span(span);
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_MYSQL);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_process_us);
        span->set_request_size(msg->meta.size() + msg->payload.size());
    }

    MethodStatus* method_status = NULL;
    do {
        if (!server->IsRunning()) {
            cntl->SetFailed(ELOGOFF, "Server is stopping");
            break;
        }

        if (socket->is_overcrowded()) {
            cntl->SetFailed(EOVERCROWDED, "Connection to %s is overcrowded",
                    butil::endpoint2str(socket->remote_side()).c_str());
            break;
        }

        if (!server_accessor.AddConcurrency(cntl.get())) {
            cntl->SetFailed(
                    ELIMIT, "Reached server's max_concurrency=%d",
                    server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                    " -usercode_in_pthread is on");
            break;
        }

        const butil::StringPiece method_name =
            GetPBMethodNameByCommandId(command_id);
        const Server::MethodProperty* mp =
            server_accessor.FindMethodPropertyByFullName(method_name);
        if (NULL == mp) {
            cntl->SetFailed(ENOMETHOD, "Fail to find method=%s",
                    method_name.data());
            break;
        }
        // Switch to service-specific error.
        non_service_error.release();

        method_status = mp->status;
        if (method_status) {
            int rejected_cc = 0;
            if (!method_status->OnRequested(&rejected_cc)) {
                cntl->SetFailed(ELIMIT, "Rejected by %s's ConcurrencyLimiter, concurrency=%d",
                        mp->method->full_name().c_str(), rejected_cc);
                break;
            }
        }
        google::protobuf::Service* svc = mp->service;
        const google::protobuf::MethodDescriptor* method = mp->method;
        accessor.set_method(method);
        if (span) {
            span->ResetServerSpanName(method->full_name());
        }
        req.reset(svc->GetRequestPrototype(method).New());

        msg->payload.pop_front(1); // pop the command id from payload
        if (!ParseFromRemainedPayload(command_id, msg->payload, req.get())) {
            LOG(ERROR) << "Fail to parse request message, size=" << msg->payload.size();
            cntl->SetFailed(EREQUEST, "Fail to parse request message, size=%d", 
                    (int)msg->payload.size());
            break;
        }

        res.reset(svc->GetResponsePrototype(method).New());
        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            uint8_t, int64_t, Controller*, const google::protobuf::Message*,
            const google::protobuf::Message*, const Server*,
            MethodStatus *, int64_t>(
                    &SendMysqlResponse, command_id, correlation_id, cntl.get(),
                    req.get(), res.get(), server,
                    method_status, msg->received_us());

        msg.reset();  // optional, just release resourse ASAP

        // `cntl', `req' and `res' will be deleted inside `done'
        if (span) {
            span->set_start_callback_us(butil::cpuwide_time_us());
            span->AsParent();
        }
        if (!FLAGS_usercode_in_pthread) {
            return svc->CallMethod(method, cntl.release(), 
                    req.release(), res.release(), done);
        }
        if (BeginRunningUserCode()) {
            svc->CallMethod(method, cntl.release(), 
                    req.release(), res.release(), done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool(
                    svc, method, cntl.release(),
                    req.release(), res.release(), done);
        }
    } while (false);

    // `cntl', `req' and `res' will be deleted inside `SendSofaResponse'
    // `socket' will be held until response has been sent
    SendMysqlResponse(command_id, correlation_id, cntl.release(),
            req.release(), res.release(), server,
            method_status, msg->received_us());
}

static void SendOKPacket(Socket* socket, uint8_t sequence_id) {
    // make the body
    MysqlProtocolPacketBody packet_body;
    packet_body.Append(packet_ok_body, sizeof(packet_ok_body) - 1);
    // make the header
    MysqlProtocolPacketHeader packet_header;
    packet_header.SetPayloadLength(packet_body.length());
    packet_header.SetSequenceId(sequence_id);
    // composite header and body
    butil::IOBuf ok_packet;
    AppendPacketHeaderToIOBuf(packet_header, ok_packet);
    AppendPacketBodyToIOBuf(packet_body, ok_packet);
    // send
    socket->Write(&ok_packet);
}

bool VerifyMysqlRequest(const InputMessageBase* msg_base) {
    InputMessageBase* mutable_msg_base = const_cast<InputMessageBase*>(msg_base);
    mutable_msg_base->SetInitialResponseMessage();

    const MostCommonMessage* msg =
        static_cast<const MostCommonMessage*>(msg_base);
    const Server* server = static_cast<const Server*>(msg->arg());
    const Authenticator* auth = server->options().auth;
    Socket* socket = msg->socket();

    if (!auth) {
        // Fast pass (no authentication)
        return true;
    }

    if (auth->VerifyCredential(
            msg->payload.to_string(), // payload is mysql auth repoonse packet body
            socket->remote_side(),
            socket->mutable_auth_context()) != 0) {
        return false;
    }
    return true;
}

} // namespace policy
} // namespace brpc
