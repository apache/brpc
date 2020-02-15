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


#include <google/protobuf/descriptor.h>          // MethodDescriptor
#include <google/protobuf/message.h>             // Message
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include "butil/time.h"
#include "brpc/controller.h"                     // Controller
#include "brpc/socket.h"                         // Socket
#include "brpc/server.h"                         // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/compress.h"                       // ParseFromCompressedData
#include "brpc/details/controller_private_accessor.h"
#include "brpc/rpc_dump.h"
#include "brpc/policy/hulu_pbrpc_meta.pb.h"      // HuluRpcRequestMeta
#include "brpc/policy/hulu_pbrpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/hulu_pbrpc_controller.h"   // HuluController
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {
namespace policy {

// Notes:
// 1. 12-byte header [HULU][body_size][meta_size]
// 2. Fields in header are NOT in network byte order (which may cause
//    problems on machines with different byte order)
// 3. Use service->name() (rather than service->full_name()) + method_index
//    to locate method defined in .proto file
// 4. 'user_message_size' is the size of protobuf request,
//    and should be set if request/response has attachment
// 5. Not supported:
//    chunk_info                   - hulu doesn't support either
//    TalkType                     - nobody has use this so far in hulu

enum HuluCompressType {
    HULU_COMPRESS_TYPE_NONE = 0,
    HULU_COMPRESS_TYPE_SNAPPY = 1,
    HULU_COMPRESS_TYPE_GZIP = 2,
    HULU_COMPRESS_TYPE_ZLIB = 3,
};

CompressType Hulu2CompressType(HuluCompressType type) {
    switch (type) {
    case HULU_COMPRESS_TYPE_NONE:
        return COMPRESS_TYPE_NONE;
    case HULU_COMPRESS_TYPE_SNAPPY:
        return COMPRESS_TYPE_SNAPPY;
    case HULU_COMPRESS_TYPE_GZIP:
        return COMPRESS_TYPE_GZIP;
    case HULU_COMPRESS_TYPE_ZLIB:
        return COMPRESS_TYPE_ZLIB;
    default:
        LOG(ERROR) << "Unknown HuluCompressType=" << type;
        return COMPRESS_TYPE_NONE;
    }
}

HuluCompressType CompressType2Hulu(CompressType type) {
    switch (type) {
    case COMPRESS_TYPE_NONE:
        return HULU_COMPRESS_TYPE_NONE;
    case COMPRESS_TYPE_SNAPPY:
        return HULU_COMPRESS_TYPE_SNAPPY;
    case COMPRESS_TYPE_GZIP:
        return HULU_COMPRESS_TYPE_GZIP;
    case COMPRESS_TYPE_ZLIB:
        return HULU_COMPRESS_TYPE_ZLIB;
    case COMPRESS_TYPE_LZ4:
        LOG(ERROR) << "Hulu doesn't support LZ4";
        return HULU_COMPRESS_TYPE_NONE;
    default:
        LOG(ERROR) << "Unknown CompressType=" << type;
        return HULU_COMPRESS_TYPE_NONE;
    }
}

// Can't use RawPacker/RawUnpacker because HULU does not use network byte order!
class HuluRawPacker {
public:
    explicit HuluRawPacker(void* stream) : _stream((char*)stream) {}

    HuluRawPacker& pack32(uint32_t hostvalue) {
        *(uint32_t*)_stream = hostvalue;
        _stream += 4;
        return *this;
    }

    HuluRawPacker& pack64(uint64_t hostvalue) {
        uint32_t *p = (uint32_t*)_stream;
        *p = (hostvalue & 0xFFFFFFFF);
        *(p + 1) = (hostvalue >> 32);
        _stream += 8;
        return *this;
    }

private:
    char* _stream;
};

class HuluRawUnpacker {
public:
    explicit HuluRawUnpacker(const void* stream) 
        : _stream((const char*)stream) {}

    HuluRawUnpacker& unpack32(uint32_t & hostvalue) {
        hostvalue = *(const uint32_t*)_stream;
        _stream += 4;
        return *this;
    }

    HuluRawUnpacker& unpack64(uint64_t & hostvalue) {
        const uint32_t *p = (const uint32_t*)_stream;
        hostvalue = ((uint64_t)*(p + 1) << 32) | *p;
        _stream += 8;
        return *this;
    }

private:
    const char* _stream;
};

inline void PackHuluHeader(char* hulu_header, int meta_size, int body_size) {
    uint32_t* dummy = reinterpret_cast<uint32_t*>(hulu_header); // suppress strict-alias warning
    *dummy = *reinterpret_cast<const uint32_t*>("HULU");
    HuluRawPacker rp(hulu_header + 4);
    rp.pack32(meta_size + body_size).pack32(meta_size);
}

template <typename Meta>
static void SerializeHuluHeaderAndMeta(
    butil::IOBuf* out, const Meta& meta, int payload_size) {
    const int meta_size = meta.ByteSize();
    if (meta_size <= 244) { // most common cases
        char header_and_meta[12 + meta_size];
        PackHuluHeader(header_and_meta, meta_size, payload_size);
        ::google::protobuf::io::ArrayOutputStream arr_out(header_and_meta + 12, meta_size);
        ::google::protobuf::io::CodedOutputStream coded_out(&arr_out);
        meta.SerializeWithCachedSizes(&coded_out); // not calling ByteSize again
        CHECK(!coded_out.HadError());
        out->append(header_and_meta, sizeof(header_and_meta));
    } else {
        char header[12];
        PackHuluHeader(header, meta_size, payload_size);
        out->append(header, sizeof(header));
        butil::IOBufAsZeroCopyOutputStream buf_stream(out);
        ::google::protobuf::io::CodedOutputStream coded_out(&buf_stream);
        meta.SerializeWithCachedSizes(&coded_out);
        CHECK(!coded_out.HadError());
    }
}

ParseResult ParseHuluMessage(butil::IOBuf* source, Socket* socket,
                             bool /*read_eof*/, const void* /*arg*/) {
    char header_buf[12];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"HULU") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "HULU", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size;
    HuluRawUnpacker ru(header_buf + 4);
    ru.unpack32(body_size);
    if (body_size > FLAGS_max_body_size) {
        // We need this log to report the body_size to give users some clues
        // which is not printed in InputMessenger.
        LOG(ERROR) << "body_size=" << body_size << " from "
                   << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t meta_size;
    ru.unpack32(meta_size);
    if (__builtin_expect(meta_size > body_size, 0)) {
        LOG(ERROR) << "meta_size=" << meta_size
                   << " is bigger than body_size=" << body_size;
        // Pop the message
        source->pop_front(sizeof(header_buf) + body_size);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    source->pop_front(sizeof(header_buf));
    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, meta_size);
    source->cutn(&msg->payload, body_size - meta_size);
    return MakeMessage(msg);
}

// Assemble response packet using `correlation_id', `controller',
// `res', and then write this packet to `sock'
static void SendHuluResponse(int64_t correlation_id,
                             HuluController* cntl, 
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
    std::unique_ptr<HuluController, LogErrorTextAndDelete> recycle_cntl(cntl);
    ConcurrencyRemover concurrency_remover(method_status, cntl, received_us);
    std::unique_ptr<const google::protobuf::Message> recycle_req(req);
    std::unique_ptr<const google::protobuf::Message> recycle_res(res);

    if (cntl->IsCloseConnection()) {
        sock->SetFailed();
        return;
    }
    
    bool append_body = false;
    butil::IOBuf res_body_buf;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    CompressType type = cntl->response_compress_type();
    if (res != NULL && !cntl->Failed()) {
        if (!res->IsInitialized()) {
            cntl->SetFailed(
                ERESPONSE, "Missing required fields in response: %s",
                res->InitializationErrorString().c_str());
        } else if (!SerializeAsCompressedData(*res, &res_body_buf, type)) {
            cntl->SetFailed(ERESPONSE, "Fail to serialize response, "
                            "CompressType=%s", CompressTypeToCStr(type));
        } else {
            append_body = true;
        }
    }

    // Don't use res->ByteSize() since it may be compressed
    size_t res_size = 0;
    size_t attached_size = 0;
    if (append_body) {
        res_size = res_body_buf.length();
        attached_size = cntl->response_attachment().length();
    }

    HuluRpcResponseMeta meta;
    const int error_code = cntl->ErrorCode();
    meta.set_error_code(error_code);
    if (!cntl->ErrorText().empty()) {
        // Only set error_text when it's not empty since protobuf Message
        // always new the string no matter if it's empty or not.
        meta.set_error_text(cntl->ErrorText());
    }
    meta.set_correlation_id(correlation_id);
    meta.set_compress_type(
            CompressType2Hulu(cntl->response_compress_type()));
    if (attached_size > 0) {
        meta.set_user_message_size(res_size);
    }
    if (cntl->response_source_addr() != 0) {
        meta.set_user_defined_source_addr(cntl->response_source_addr());
    }
    if (!cntl->response_user_data().empty()) {
        meta.set_user_data(cntl->response_user_data());
    }

    butil::IOBuf res_buf;
    SerializeHuluHeaderAndMeta(&res_buf, meta, res_size + attached_size);
    if (append_body) {
        res_buf.append(res_body_buf.movable());
        if (attached_size) {
            res_buf.append(cntl->response_attachment().movable());
        }
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

void ProcessHuluRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    HuluRpcRequestMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse HuluRpcRequestMeta, close the connection";
        socket->SetFailed();
        return;
    }

    const CompressType req_cmp_type = Hulu2CompressType((HuluCompressType)meta.compress_type());
    SampledRequest* sample = AskToBeSampled();
    if (sample) {
        sample->meta.set_service_name(meta.service_name());
        sample->meta.set_method_index(meta.method_index());
        sample->meta.set_compress_type(req_cmp_type);
        sample->meta.set_protocol_type(PROTOCOL_HULU_PBRPC);
        sample->meta.set_user_data(meta.user_data());
        if (meta.has_user_message_size()
            && static_cast<size_t>(meta.user_message_size()) < msg->payload.size()) {
            size_t attachment_size = msg->payload.size() - meta.user_message_size();
            sample->meta.set_attachment_size(attachment_size);
        }
        sample->request = msg->payload;
        sample->submit(start_parse_us);
    }

    std::unique_ptr<HuluController> cntl(new (std::nothrow) HuluController());
    if (NULL == cntl.get()) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }
    std::unique_ptr<google::protobuf::Message> req;
    std::unique_ptr<google::protobuf::Message> res;

    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl.get());
    int64_t correlation_id = meta.correlation_id();
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    if (meta.has_log_id()) {
        cntl->set_log_id(meta.log_id());
    }
    cntl->set_request_compress_type(req_cmp_type);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_HULU_PBRPC)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

    if (meta.has_user_data()) {
        cntl->set_request_user_data(meta.user_data());
    }

    if (meta.has_user_defined_source_addr()) {
        cntl->set_request_source_addr(meta.user_defined_source_addr());
    }
    

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(meta.has_trace_id())) {
        span = Span::CreateServerSpan(
            meta.trace_id(), meta.span_id(), meta.parent_span_id(),
            msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(meta.log_id());
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_HULU_PBRPC);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(msg->payload.size() + msg->meta.size() + 12);
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
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                            server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                            " -usercode_in_pthread is on");
            break;
        }
        
        const Server::MethodProperty *sp =
            server_accessor.FindMethodPropertyByNameAndIndex(
                meta.service_name(), meta.method_index());
        if (NULL == sp) {
            cntl->SetFailed(ENOMETHOD, "Fail to find method=%d of service=%s",
                            meta.method_index(), meta.service_name().c_str());
            break;
        } else if (sp->service->GetDescriptor()
                   == BadMethodService::descriptor()) {
            BadMethodRequest breq;
            BadMethodResponse bres;
            breq.set_service_name(meta.service_name());
            sp->service->CallMethod(sp->method, cntl.get(), &breq, &bres, NULL);
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
        const int reqsize = msg->payload.length();
        butil::IOBuf req_buf;
        butil::IOBuf* req_buf_ptr = &msg->payload;
        if (meta.has_user_message_size()) {
            msg->payload.cutn(&req_buf, meta.user_message_size());
            req_buf_ptr = &req_buf;
            cntl->request_attachment().swap(msg->payload);
        }

        req.reset(svc->GetRequestPrototype(method).New());
        if (!ParseFromCompressedData(*req_buf_ptr, req.get(), req_cmp_type)) {
            cntl->SetFailed(EREQUEST, "Fail to parse request message, "
                            "CompressType=%s, request_size=%d", 
                            CompressTypeToCStr(req_cmp_type), reqsize);
            break;
        }

        res.reset(svc->GetResponsePrototype(method).New());
        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            int64_t, HuluController*, const google::protobuf::Message*,
            const google::protobuf::Message*, const Server*,
                  MethodStatus *, int64_t>(
                &SendHuluResponse, correlation_id, cntl.get(),
                req.get(), res.get(), server,
                method_status, msg->received_us());

        // optional, just release resourse ASAP
        msg.reset();
        req_buf.clear();

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

    // `cntl', `req' and `res' will be deleted inside `SendHuluResponse'
    // `socket' will be held until response has been sent
    SendHuluResponse(correlation_id, cntl.release(),
                     req.release(), res.release(), server,
                     method_status, msg->received_us());
}

bool VerifyHuluRequest(const InputMessageBase* msg_base) {
    const MostCommonMessage* msg =
        static_cast<const MostCommonMessage*>(msg_base);
    Socket* socket = msg->socket();
    const Server* server = static_cast<const Server*>(msg->arg());

    HuluRpcRequestMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse HuluRpcRequestMeta";
        return false;
    }
    const Authenticator* auth = server->options().auth;
    if (NULL == auth) {
        // Fast pass (no authentication)
        return true;
    }    
    if (auth->VerifyCredential(
                meta.credential_data(), socket->remote_side(), 
                socket->mutable_auth_context()) != 0) {
        return false;
    }
    return true;
}

void ProcessHuluResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    HuluRpcResponseMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse from response meta";
        return;
    }

    const bthread_id_t cid = { static_cast<uint64_t>(meta.correlation_id()) };
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
        span->set_response_size(msg->meta.size() + msg->payload.size() + 12);
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    if (meta.error_code() != 0) {
        // If error_code is unset, default is 0 = success.
        cntl->SetFailed(meta.error_code(), 
                              "%s", meta.error_text().c_str());
    } else {
        // Parse response message iff error code from meta is 0
        butil::IOBuf res_buf;
        butil::IOBuf* res_buf_ptr = &msg->payload;
        if (meta.has_user_message_size()) {
            msg->payload.cutn(&res_buf, meta.user_message_size());
            res_buf_ptr = &res_buf;
            cntl->response_attachment().swap(msg->payload);
        }

        CompressType res_cmp_type = Hulu2CompressType((HuluCompressType)meta.compress_type());
        cntl->set_response_compress_type(res_cmp_type);
        if (cntl->response()) {
            if (!ParseFromCompressedData(
                    *res_buf_ptr, cntl->response(), res_cmp_type)) {
                cntl->SetFailed(
                    ERESPONSE, "Fail to parse response message, "
                    "CompressType=%s, response_size=%" PRIu64, 
                    CompressTypeToCStr(res_cmp_type),
                    (uint64_t)msg->payload.length());
            }
        } // else silently ignore the response.
        HuluController* hulu_controller = dynamic_cast<HuluController*>(cntl);
        if (hulu_controller) {
            if (meta.has_user_defined_source_addr()) {
                hulu_controller->set_response_source_addr(
                            meta.user_defined_source_addr());
            }
            if (meta.has_user_data()) {
                hulu_controller->set_response_user_data(meta.user_data());
            }
        }
    }
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void PackHuluRequest(butil::IOBuf* req_buf,
                     SocketMessage**,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* cntl,
                     const butil::IOBuf& req_body,
                     const Authenticator* auth) {
    HuluRpcRequestMeta meta;
    if (auth != NULL && auth->GenerateCredential(
                meta.mutable_credential_data()) != 0) {
        return cntl->SetFailed(EREQUEST, "Fail to generate credential");
    }

    if (method) {
        meta.set_service_name(method->service()->name());
        meta.set_method_index(method->index());
        meta.set_compress_type(CompressType2Hulu(cntl->request_compress_type()));
    } else if (cntl->sampled_request()) {
        // Replaying. Keep service-name as the one seen by server.
        meta.set_service_name(cntl->sampled_request()->meta.service_name());
        meta.set_method_index(cntl->sampled_request()->meta.method_index());
        meta.set_compress_type(
            CompressType2Hulu(cntl->sampled_request()->meta.compress_type()));
        meta.set_user_data(cntl->sampled_request()->meta.user_data());
    } else {
        return cntl->SetFailed(ENOMETHOD, "method is NULL");
    }

    HuluController* hulu_controller = dynamic_cast<HuluController*>(cntl);
    if (hulu_controller != NULL) {
        if (hulu_controller->request_source_addr() != 0) {
            meta.set_user_defined_source_addr(
                    hulu_controller->request_source_addr());
        }
        if (!hulu_controller->request_user_data().empty()) {
            meta.set_user_data(hulu_controller->request_user_data());
        }
    }
    
    meta.set_correlation_id(correlation_id);
    if (cntl->has_log_id()) {
        meta.set_log_id(cntl->log_id());
    }

    // Don't use res->ByteSize() since it may be compressed
    const size_t req_size = req_body.size();
    const size_t attached_size = cntl->request_attachment().length();
    if (attached_size) {
        meta.set_user_message_size(req_size);
    } // else don't set user_mesage_size when there's no attachment, otherwise
    // existing hulu-pbrpc server may complain about empty attachment.

    Span* span = ControllerPrivateAccessor(cntl).span();
    if (span) {
        meta.set_trace_id(span->trace_id());
        meta.set_span_id(span->span_id());
        meta.set_parent_span_id(span->parent_span_id());
    }
    
    SerializeHuluHeaderAndMeta(req_buf, meta, req_size + attached_size);
    req_buf->append(req_body);
    if (attached_size) {
        req_buf->append(cntl->request_attachment());
    }
}

}  // namespace policy
} // namespace brpc

