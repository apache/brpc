// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#include <google/protobuf/descriptor.h>          // MethodDescriptor
#include <google/protobuf/message.h>             // Message
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include "butil/time.h"
#include "brpc/controller.h"                // Controller
#include "brpc/socket.h"                    // Socket
#include "brpc/server.h"                    // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/compress.h"                  // ParseFromCompressedData
#include "brpc/rpc_dump.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/sofa_pbrpc_meta.pb.h" // SofaRpcMeta
#include "brpc/policy/sofa_pbrpc_protocol.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {
namespace policy {

// Notes:
// 1. 24-byte header [SOFA][meta_size][body_size(64)][message_size(64)],
// 2. Fields in header are NOT in network byte order (which may cause
//    problems on machines with different byte order)
// 3. meta of request and response are same, distinguished by SofaRpcMeta::type
// 4. sofa-pbrpc does not support log_id, attachment, tracing

CompressType Sofa2CompressType(SofaCompressType type) {
    switch (type) {
    case SOFA_COMPRESS_TYPE_NONE:
        return COMPRESS_TYPE_NONE;
    case SOFA_COMPRESS_TYPE_SNAPPY:
        return COMPRESS_TYPE_SNAPPY;
    case SOFA_COMPRESS_TYPE_GZIP:
        return COMPRESS_TYPE_GZIP;
    case SOFA_COMPRESS_TYPE_ZLIB:
        return COMPRESS_TYPE_ZLIB;
    default:
        LOG(ERROR) << "Unknown SofaCompressType=" << type;
        return COMPRESS_TYPE_NONE;
    }
}

SofaCompressType CompressType2Sofa(CompressType type) {
    switch (type) {
    case COMPRESS_TYPE_NONE:
        return SOFA_COMPRESS_TYPE_NONE;
    case COMPRESS_TYPE_SNAPPY:
        return SOFA_COMPRESS_TYPE_SNAPPY;
    case COMPRESS_TYPE_GZIP:
        return SOFA_COMPRESS_TYPE_GZIP;
    case COMPRESS_TYPE_ZLIB:
        return SOFA_COMPRESS_TYPE_ZLIB;
    case COMPRESS_TYPE_LZ4:
        LOG(ERROR) << "sofa-pbrpc does not support LZ4";
        return SOFA_COMPRESS_TYPE_NONE;
    default:
        LOG(ERROR) << "Unknown SofaCompressType=" << type;
        return SOFA_COMPRESS_TYPE_NONE;
    }
}

// Can't use RawPacker/RawUnpacker because SOFA does not use network byte order!
class SofaRawPacker {
public:
    explicit SofaRawPacker(void* stream) : _stream((char*)stream) {}

    SofaRawPacker& pack32(uint32_t hostvalue) {
        *(uint32_t*)_stream = hostvalue;
        _stream += 4;
        return *this;
    }

    SofaRawPacker& pack64(uint64_t hostvalue) {
        uint32_t *p = (uint32_t*)_stream;
        *p = (hostvalue & 0xFFFFFFFF);
        *(p + 1) = (hostvalue >> 32);
        _stream += 8;
        return *this;
    }

private:
    char* _stream;
};

class SofaRawUnpacker {
public:
    explicit SofaRawUnpacker(const void* stream) 
        : _stream((const char*)stream) {}

    SofaRawUnpacker& unpack32(uint32_t & hostvalue) {
        hostvalue = *(const uint32_t*)_stream;
        _stream += 4;
        return *this;
    }

    SofaRawUnpacker& unpack64(uint64_t & hostvalue) {
        const uint32_t *p = (const uint32_t*)_stream;
        hostvalue = ((uint64_t)*(p + 1) << 32) | *p;
        _stream += 8;
        return *this;
    }

private:
    const char* _stream;
};

inline void PackSofaHeader(char* sofa_header, int meta_size, int body_size) {
    uint32_t* dummy = reinterpret_cast<uint32_t*>(sofa_header); // suppress strict-alias warning
    *dummy = *reinterpret_cast<const uint32_t*>("SOFA");

    SofaRawPacker rp(sofa_header + 4);
    rp.pack32(meta_size).pack64(body_size).pack64(meta_size + body_size);
}

static void SerializeSofaHeaderAndMeta(
    butil::IOBuf* out, const SofaRpcMeta& meta, int payload_size) {
    const int meta_size = meta.ByteSize();
    if (meta_size <= 232) { // most common cases
        char header_and_meta[24 + meta_size];
        PackSofaHeader(header_and_meta, meta_size, payload_size);
        ::google::protobuf::io::ArrayOutputStream arr_out(header_and_meta + 24, meta_size);
        ::google::protobuf::io::CodedOutputStream coded_out(&arr_out);
        meta.SerializeWithCachedSizes(&coded_out); // not calling ByteSize again
        CHECK(!coded_out.HadError());
        out->append(header_and_meta, sizeof(header_and_meta));
    } else {
        char header[24];
        PackSofaHeader(header, meta_size, payload_size);
        out->append(header, sizeof(header));
        butil::IOBufAsZeroCopyOutputStream buf_stream(out);
        ::google::protobuf::io::CodedOutputStream coded_out(&buf_stream);
        meta.SerializeWithCachedSizes(&coded_out);
        CHECK(!coded_out.HadError());
    }
}

ParseResult ParseSofaMessage(butil::IOBuf* source, Socket* socket,
                             bool /*read_eof*/, const void* /*arg*/) {
    char header_buf[24];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"SOFA") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "SOFA", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t meta_size;
    uint64_t body_size;
    uint64_t msg_size;
    SofaRawUnpacker ru(header_buf + 4);
    ru.unpack32(meta_size).unpack64(body_size).unpack64(msg_size);
    if (msg_size != meta_size + body_size) {
        LOG(ERROR) << "msg_size=" << msg_size << " != meta_size=" << meta_size
                   << " + body_size=" << body_size;
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    if (body_size > FLAGS_max_body_size) {
        // We need this log to report the body_size to give users some clues
        // which is not printed in InputMessenger.
        LOG(ERROR) << "body_size=" << body_size << " from "
                   << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + msg_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    source->pop_front(sizeof(header_buf));
    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, meta_size);
    source->cutn(&msg->payload, body_size);
    return MakeMessage(msg);
}

// Assemble response packet using `correlation_id', `controller',
// `res', and then write this packet to `sock'
static void SendSofaResponse(int64_t correlation_id,
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
    Socket* sock = accessor.get_sending_socket();
    std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);
    ConcurrencyRemover concurrency_remover(method_status, cntl, received_us);
    std::unique_ptr<const google::protobuf::Message> recycle_req(req);
    std::unique_ptr<const google::protobuf::Message> recycle_res(res);

    if (cntl->IsCloseConnection()) {
        sock->SetFailed();
        return;
    }

    LOG_IF(WARNING, !cntl->response_attachment().empty())
        << "sofa-pbrpc does not support attachment, "
        "your response_attachment will not be sent";

    bool append_body = false;
    butil::IOBuf res_body;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    CompressType type = cntl->response_compress_type();
    if (res != NULL && !cntl->Failed()) {
        if (!res->IsInitialized()) {
            cntl->SetFailed(
                ERESPONSE, "Missing required fields in response: %s", 
                res->InitializationErrorString().c_str());
        } else if (!SerializeAsCompressedData(*res, &res_body, type)) {
            cntl->SetFailed(ERESPONSE, "Fail to serialize response, "
                            "CompressType=%s", CompressTypeToCStr(type));
        } else {
            append_body = true;
        }
    }

    // Don't use res->ByteSize() since it may be compressed
    size_t res_size = 0;
    if (append_body) {
        res_size = res_body.length();
    }
    
    SofaRpcMeta meta;
    meta.set_type(SofaRpcMeta::RESPONSE);
    // sofa-pbrpc client needs `failed' to be set currently(1.0.1.28195).
    const int error_code = cntl->ErrorCode();
    meta.set_failed(error_code != 0);
    meta.set_error_code(error_code);
    if (!cntl->ErrorText().empty()) {
        // Only set error_text when it's not empty since protobuf Message
        // always new the string no matter if it's empty or not.
        meta.set_reason(cntl->ErrorText());
    }
    meta.set_sequence_id(correlation_id);
    meta.set_compress_type(
        CompressType2Sofa(cntl->response_compress_type()));

    butil::IOBuf res_buf;
    SerializeSofaHeaderAndMeta(&res_buf, meta, res_size);
    if (append_body) {
        res_buf.append(res_body.movable());
    }
    if (span) {
        span->set_response_size(res_buf.size());
    }
    // Have the risk of unlimited pending responses, in which case, tell
    // users to set max_concurrency.
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (sock->Write(&res_buf, &wopt) != 0) {
        const int errcode = errno;
        PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
        cntl->SetFailed(errcode, "Fail to write into %s",
                        sock->description().c_str());
        return;
    }
    if (span) {
        // TODO: this is not sent
        span->set_sent_us(butil::cpuwide_time_us());
    }
}

// Defined in baidu_rpc_protocol.cpp
void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done);

void ProcessSofaRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    SofaRpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse SofaRpcMeta from " << *socket;
        socket->SetFailed(EREQUEST, "Fail to parse SofaRpcMeta from %s",
                          socket->description().c_str());
        return;
    }
    const CompressType req_cmp_type = Sofa2CompressType(meta.compress_type());

    SampledRequest* sample = AskToBeSampled();
    if (sample) {
        sample->set_method_name(meta.method());
        sample->set_compress_type(req_cmp_type);
        sample->set_protocol_type(PROTOCOL_SOFA_PBRPC);
        sample->request = msg->payload;
        sample->submit(start_parse_us);
    }

    std::unique_ptr<Controller> cntl(new (std::nothrow) Controller);
    if (NULL == cntl.get()) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }
    std::unique_ptr<google::protobuf::Message> req;
    std::unique_ptr<google::protobuf::Message> res;

    ControllerPrivateAccessor accessor(cntl.get());
    ServerPrivateAccessor server_accessor(server);
    const int64_t correlation_id = meta.sequence_id();
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();

    cntl->set_request_compress_type(req_cmp_type);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_SOFA_PBRPC)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

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
        span->set_protocol(PROTOCOL_SOFA_PBRPC);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(msg->meta.size() + msg->payload.size() + 24);
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
        
        const Server::MethodProperty *sp =
            server_accessor.FindMethodPropertyByFullName(meta.method());
        if (NULL == sp) {
            cntl->SetFailed(ENOMETHOD, "Fail to find method=%s", 
                            meta.method().c_str());
            break;
        }
        // Switch to service-specific error.
        non_service_error.release();
        method_status = sp->status;
        if (method_status) {
            int rejected_cc = 0;
            if (!method_status->OnRequested(&rejected_cc)) {
                cntl->SetFailed(ELIMIT, "Rejected by %s's ConcurrencyLimiter, concurrency=%d",
                                sp->method->full_name().c_str(), rejected_cc);
                break;
            }
        }
        google::protobuf::Service* svc = sp->service;
        const google::protobuf::MethodDescriptor* method = sp->method;
        accessor.set_method(method);
        if (span) {
            span->ResetServerSpanName(method->full_name());
        }
        req.reset(svc->GetRequestPrototype(method).New());
        if (!ParseFromCompressedData(msg->payload, req.get(), req_cmp_type)) {
            cntl->SetFailed(EREQUEST, "Fail to parse request message, "
                            "CompressType=%d, size=%d", 
                            req_cmp_type, (int)msg->payload.size());
            break;
        }

        res.reset(svc->GetResponsePrototype(method).New());
        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            int64_t, Controller*, const google::protobuf::Message*,
            const google::protobuf::Message*, const Server*,
                  MethodStatus *, int64_t>(
                    &SendSofaResponse, correlation_id, cntl.get(),
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
    SendSofaResponse(correlation_id, cntl.release(),
                     req.release(), res.release(), server,
                     method_status, msg->received_us());
}

bool VerifySofaRequest(const InputMessageBase* msg_base) {
    const Server* server = static_cast<const Server*>(msg_base->arg());
    if (server->options().auth) {
        LOG(WARNING) << "sofa-pbrpc does not support authentication";
        return false;
    }
    return true;
}

void ProcessSofaResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SofaRpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse response meta";
        return;
    }

    const bthread_id_t cid = { static_cast<uint64_t>(meta.sequence_id()) };
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
        span->set_response_size(msg->meta.size() + msg->payload.size() + 24);
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    if (meta.error_code() != 0) {
        // If error_code is unset, default is 0 = success.
        cntl->SetFailed(meta.error_code(), 
                              "%s", meta.reason().c_str());
    } else if (cntl->response()) {
        // Parse response message iff error code from meta is 0
        CompressType res_cmp_type = Sofa2CompressType(meta.compress_type());
        if (!ParseFromCompressedData(
                msg->payload, cntl->response(), res_cmp_type)) {
            cntl->SetFailed(
                ERESPONSE, "Fail to parse response message, "
                "CompressType=%d, response_size=%" PRIu64, 
                res_cmp_type, (uint64_t)msg->payload.length());
        } else {
            cntl->set_response_compress_type(res_cmp_type);
        }
    } // else silently ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void PackSofaRequest(butil::IOBuf* req_buf,
                     SocketMessage**,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* cntl,
                     const butil::IOBuf& req_body,
                     const Authenticator* /*not supported*/) {
    if (!cntl->request_attachment().empty()) {
        LOG(WARNING) << "sofa-pbrpc does not support attachment, "
            "your request_attachment will not be sent";
    }
    SofaRpcMeta meta;
    meta.set_type(SofaRpcMeta::REQUEST);
    meta.set_sequence_id(correlation_id);
    if (method) {
        meta.set_method(method->full_name());
        meta.set_compress_type(CompressType2Sofa(cntl->request_compress_type()));
    } else if (cntl->rpc_dump_meta()) {
        // Replaying.
        meta.set_method(cntl->rpc_dump_meta()->method_name());
        meta.set_compress_type(
            CompressType2Sofa(cntl->rpc_dump_meta()->compress_type()));
    } else {
        return cntl->SetFailed(ENOMETHOD, "method is NULL");
    }

    SerializeSofaHeaderAndMeta(req_buf, meta, req_body.size());
    req_buf->append(req_body);
}

} // namespace policy
} // namespace brpc
