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

#include "brpc/policy/flatbuffers_protocol.h"

#if BRPC_WITH_FLATBUFFERS
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include "butil/logging.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/flatbuffers/message.h"
#include "brpc/flatbuffers/service.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" void bthread_assign_data(void* data);

namespace brpc {
namespace policy {
namespace {

const size_t kHeaderSize = 12;
const size_t kRequestMetaSize = 24;
const size_t kResponseMetaSize = 20;

// The header is big-endian; the fixed metadata prefix is little-endian, as in
// the experimental FRPC format on x86/ARM. meta_size permits appended fields.
// Bytewise access also handles fragmented or unaligned input without packed
// structs, aliasing violations or host-endian dependencies.
uint32_t Load32(const unsigned char* p, bool big_endian = false) {
    uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i) {
        result |= static_cast<uint32_t>(p[i]) << (8 * (big_endian ? 3 - i : i));
    }
    return result;
}

uint64_t Load64(const unsigned char* p) {
    return Load32(p) | (static_cast<uint64_t>(Load32(p + 4)) << 32);
}

void Store32(unsigned char* p, uint32_t value, bool big_endian = false) {
    for (size_t i = 0; i < 4; ++i) {
        p[i] = static_cast<unsigned char>(value >> (8 * (big_endian ? 3 - i : i)));
    }
}

void Store64(unsigned char* p, uint64_t value) {
    Store32(p, static_cast<uint32_t>(value));
    Store32(p + 4, static_cast<uint32_t>(value >> 32));
}

bool ValidSizes(size_t message_size, size_t attachment_size, size_t meta_size) {
    const size_t limit = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    return message_size <= limit && attachment_size <= limit &&
           meta_size <= limit && message_size <= limit - meta_size &&
           attachment_size <= limit - meta_size - message_size &&
           meta_size + message_size + attachment_size <=
               static_cast<size_t>(FLAGS_max_body_size);
}

void PackHeader(unsigned char* header, size_t meta_size, size_t payload_size) {
    memcpy(header, "FRPC", 4);
    Store32(header + 4, static_cast<uint32_t>(meta_size + payload_size), true);
    Store32(header + 8, static_cast<uint32_t>(meta_size), true);
}

bool SerializePayload(const flatbuffers::Message& message, butil::IOBuf* out) {
    butil::IOBuf body;
    if (!message.append_msg_to_iobuf(body)) {
        return false;
    }
    body.pop_front(message.get_meta_size());
    if (body.size() != message.size()) {
        return false;
    }
    out->append(body);
    return true;
}

bool ValidPayloadSizes(uint32_t message_size, uint32_t attachment_size,
                       size_t payload_size) {
    const uint32_t limit = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    return message_size <= limit && attachment_size <= limit &&
           attachment_size <= payload_size &&
           message_size == payload_size - attachment_size;
}

// One closure owns the request, response, controller and concurrency accounting
// until the application completes, including asynchronous service methods.
class FlatBuffersCall : public google::protobuf::Closure {
public:
    FlatBuffersCall(uint64_t id, int64_t received_us)
        : correlation_id(id), received_us(received_us), cntl(new Controller),
          service(nullptr), method(nullptr), status(nullptr) {}

    ~FlatBuffersCall() override {
        {
            ConcurrencyRemover remover(status, cntl.get(), received_us);
        }
        cntl->CallAfterRpcResp(&request, &response);
    }

    void Run() override {
        std::unique_ptr<FlatBuffersCall> self(this);
        ControllerPrivateAccessor accessor(cntl.get());
        Socket* socket = accessor.get_sending_socket();
        if (cntl->IsCloseConnection()) {
            socket->SetFailed();
            return;
        }

        butil::IOBuf payload;
        size_t attachment_size = 0;
        if (!cntl->Failed()) {
            if (cntl->response_compress_type() != COMPRESS_TYPE_NONE ||
                cntl->response_checksum_type() != CHECKSUM_TYPE_NONE) {
                cntl->SetFailed(ERESPONSE, "FRPC does not support compression or checksums");
            } else if (!SerializePayload(response, &payload)) {
                cntl->SetFailed(ERESPONSE, "Empty or invalid FlatBuffers response");
            } else {
                attachment_size = cntl->response_attachment().size();
                if (!ValidSizes(payload.size(), attachment_size, kResponseMetaSize)) {
                    cntl->SetFailed(ERESPONSE, "FlatBuffers response is too large");
                }
            }
        }
        if (cntl->Failed()) {
            payload.clear();
            attachment_size = 0;
        }

        unsigned char header[kHeaderSize + kResponseMetaSize] = {};
        PackHeader(header, kResponseMetaSize, payload.size() + attachment_size);
        Store32(header + 12, static_cast<uint32_t>(cntl->ErrorCode()));
        Store32(header + 16, static_cast<uint32_t>(payload.size()));
        Store32(header + 20, static_cast<uint32_t>(attachment_size));
        Store64(header + 24, correlation_id);
        butil::IOBuf wire;
        if (wire.append(header, sizeof(header)) != 0) {
            socket->SetFailed(ENOMEM, "Fail to allocate FlatBuffers response header");
            return;
        }
        wire.append(payload);
        if (attachment_size) {
            wire.append(cntl->response_attachment());
        }
        Socket::WriteOptions options;
        options.ignore_eovercrowded = true;
        if (socket->Write(&wire, &options) != 0) {
            cntl->SetFailed(errno, "Fail to write FlatBuffers response");
        }
    }

    static void Invoke(void* arg) {
        FlatBuffersCall* call = static_cast<FlatBuffersCall*>(arg);
        call->service->FBCallMethod(call->method, call->cntl.get(),
                                   &call->request, &call->response, call);
    }

    uint64_t correlation_id;
    int64_t received_us;
    std::unique_ptr<Controller, LogErrorTextAndDelete> cntl;
    flatbuffers::Message request;
    flatbuffers::Message response;
    flatbuffers::Service* service;
    const flatbuffers::MethodDescriptor* method;
    MethodStatus* status;
};

}  // namespace

ParseResult ParseFlatBuffersMessage(butil::IOBuf* source, Socket*, bool,
                                   const void*) {
    unsigned char header[kHeaderSize] = {};
    const size_t n = source->copy_to(header, sizeof(header));
    if (memcmp(header, "FRPC", std::min(n, size_t(4))) != 0) {
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    if (n < sizeof(header)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const uint32_t body_size = Load32(header + 4, true);
    const uint32_t meta_size = Load32(header + 8, true);
    if (body_size > static_cast<uint64_t>(FLAGS_max_body_size)) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    }
    if (meta_size > body_size) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                              "FRPC metadata exceeds frame body");
    }
    if (source->size() - kHeaderSize < body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    MostCommonMessage* message = MostCommonMessage::Get();
    if (!message) {
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    source->pop_front(kHeaderSize);
    source->cutn(&message->meta, meta_size);
    source->cutn(&message->payload, body_size - meta_size);
    return MakeMessage(message);
}

bool VerifyFlatBuffersRequest(const InputMessageBase* message) {
    const Server* server = static_cast<const Server*>(message->arg());
    // No credential format has been defined for FRPC. Never silently bypass
    // authentication configured for the server's other protocols.
    return server->options().auth == nullptr;
}

void ProcessFlatBuffersRequest(InputMessageBase* message_base) {
    DestroyingPtr<MostCommonMessage> message(
        static_cast<MostCommonMessage*>(message_base));
    SocketUniquePtr socket_guard(message->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(message->arg());
    ScopedNonServiceError non_service_error(server);
    unsigned char meta[kRequestMetaSize];
    if (message->meta.copy_to(meta, sizeof(meta)) != sizeof(meta)) {
        socket->SetFailed(EREQUEST, "Truncated FlatBuffers request metadata");
        return;
    }
    const uint32_t service_id = Load32(meta);
    const uint32_t method_id = Load32(meta + 4);
    const uint32_t message_size = Load32(meta + 8);
    const uint32_t attachment_size = Load32(meta + 12);
    std::unique_ptr<FlatBuffersCall> call(
        new FlatBuffersCall(Load64(meta + 16), message->received_us()));
    Controller* cntl = call->cntl.get();
    ControllerPrivateAccessor accessor(cntl);
    ServerPrivateAccessor server_accessor(server);
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    cntl->set_rpc_received_us(message->received_us());
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_FLATBUFFERS_RPC)
        .set_begin_time_us(message->received_us())
        .move_in_server_receiving_sock(socket_guard);
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    do {
        if (server->options().auth) {
            cntl->SetFailed(ERPCAUTH, "FRPC does not support authentication");
            break;
        }
        if (RejectNonBuiltinAccessFromInternalPort(cntl, *server)) {
            break;
        }
        if (!server->IsRunning()) {
            cntl->SetFailed(ELOGOFF, "Server is stopping");
            break;
        }
        if (!server_accessor.AddConcurrency(cntl)) {
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency");
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code tasks");
            break;
        }
        if (!message_size || !ValidPayloadSizes(message_size, attachment_size,
                                                message->payload.size())) {
            cntl->SetFailed(EREQUEST, "Invalid FlatBuffers request sizes");
            break;
        }
        if (method_id > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            cntl->SetFailed(ENOMETHOD, "Invalid FlatBuffers method ID");
            break;
        }
        const Server::FlatBuffersMethodProperty* property =
            server_accessor.FindFlatBuffersMethodPropertyByIndex(service_id, method_id);
        if (!property) {
            cntl->SetFailed(ENOMETHOD, "Unknown FlatBuffers service=%u method=%u",
                            service_id, method_id);
            break;
        }
        call->service = property->service;
        call->method = property->method;
        accessor.set_flatbuffers_method(property->method);
        if (socket->is_overcrowded() && !server->options().ignore_eovercrowded &&
            !property->ignore_eovercrowded) {
            cntl->SetFailed(EOVERCROWDED, "FlatBuffers connection is overcrowded");
            break;
        }
        non_service_error.release();
        if (property->status) {
            call->status = property->status;
            if (!call->status->OnRequested(nullptr, cntl)) {
                cntl->SetFailed(ELIMIT, "Reached method's max_concurrency");
                break;
            }
        }
        if (!server->AcceptRequest(cntl)) {
            break;
        }
        butil::IOBuf payload;
        message->payload.cutn(&payload, message_size);
        if (!call->request.parse_msg_from_iobuf(payload, message_size, 0)) {
            cntl->SetFailed(EREQUEST, "Fail to parse FlatBuffers request");
            break;
        }
        cntl->request_attachment().swap(message->payload);
        message.reset();
        if (FLAGS_usercode_in_pthread) {
            RunUserCode(&FlatBuffersCall::Invoke, call.release());
        } else {
            FlatBuffersCall::Invoke(call.release());
        }
        return;
    } while (false);
    call.release()->Run();
}

void ProcessFlatBuffersResponse(InputMessageBase* message_base) {
    DestroyingPtr<MostCommonMessage> message(
        static_cast<MostCommonMessage*>(message_base));
    unsigned char meta[kResponseMetaSize];
    if (message->meta.copy_to(meta, sizeof(meta)) != sizeof(meta)) {
        if (message->socket()) {
            message->socket()->SetFailed(ERESPONSE, "Truncated FlatBuffers response metadata");
        }
        return;
    }
    const uint32_t error_code = Load32(meta);
    const uint32_t message_size = Load32(meta + 4);
    const uint32_t attachment_size = Load32(meta + 8);
    const bthread_id_t correlation_id = {Load64(meta + 12)};
    Controller* cntl = nullptr;
    const int rc = bthread_id_lock(correlation_id, reinterpret_cast<void**>(&cntl));
    if (rc != 0) {
        return;
    }
    ControllerPrivateAccessor accessor(cntl);
    const int saved_error = cntl->ErrorCode();
    cntl->set_rpc_received_us(message->received_us());
    do {
        if (!ValidPayloadSizes(message_size, attachment_size, message->payload.size())) {
            cntl->SetFailed(ERESPONSE, "Invalid FlatBuffers response sizes");
            break;
        }
        if (error_code) {
            if (message_size || attachment_size) {
                cntl->SetFailed(ERESPONSE, "FlatBuffers error response has a payload");
                break;
            }
            cntl->SetFailed(static_cast<int32_t>(error_code), "FlatBuffers server error");
            break;
        }
        if (!message_size) {
            cntl->SetFailed(ERESPONSE, "Empty FlatBuffers response");
            break;
        }
        butil::IOBuf payload;
        message->payload.cutn(&payload, message_size);
        if (cntl->response()) {
            if (cntl->response()->GetDescriptor() != flatbuffers::Message::descriptor()) {
                cntl->SetFailed(ERESPONSE, "Response is not a FlatBuffers Message");
                break;
            }
            flatbuffers::Message response;
            if (!response.parse_msg_from_iobuf(payload, message_size, 0)) {
                cntl->SetFailed(ERESPONSE, "Fail to parse FlatBuffers response");
                break;
            }
            *static_cast<flatbuffers::Message*>(cntl->response()) = std::move(response);
        }
        cntl->response_attachment().swap(message->payload);
    } while (false);
    message.reset();
    accessor.OnResponse(correlation_id, saved_error);
}

void SerializeFlatBuffersRequest(butil::IOBuf* buf, Controller* cntl,
                                 const google::protobuf::Message* request) {
    if (!request || request->GetDescriptor() != flatbuffers::Message::descriptor()) {
        cntl->SetFailed(EREQUEST, "Request is not a FlatBuffers Message");
        return;
    }
    if (cntl->request_compress_type() != COMPRESS_TYPE_NONE ||
        cntl->request_checksum_type() != CHECKSUM_TYPE_NONE) {
        cntl->SetFailed(EREQUEST, "FRPC does not support compression or checksums");
        return;
    }
    if (!SerializePayload(*static_cast<const flatbuffers::Message*>(request), buf)) {
        cntl->SetFailed(EREQUEST, "Empty or invalid FlatBuffers request");
    }
}

void PackFlatBuffersRequest(butil::IOBuf* buf, SocketMessage**,
                            uint64_t correlation_id,
                            const google::protobuf::MethodDescriptor* method,
                            Controller* cntl, const butil::IOBuf& request,
                            const Authenticator* auth) {
    const flatbuffers::MethodDescriptor* fb_method = cntl->flatbuffers_method();
    if (method || !fb_method || fb_method->index() < 0) {
        cntl->SetFailed(ENOMETHOD, "Use Channel::FBCallMethod with a FlatBuffers method");
        return;
    }
    if (auth) {
        cntl->SetFailed(EREQUEST, "FRPC does not support authentication");
        return;
    }
    const size_t attachment_size = cntl->request_attachment().size();
    if (!request.size() || !ValidSizes(request.size(), attachment_size, kRequestMetaSize)) {
        cntl->SetFailed(EREQUEST, "Invalid FlatBuffers request size");
        return;
    }
    unsigned char header[kHeaderSize + kRequestMetaSize] = {};
    PackHeader(header, kRequestMetaSize, request.size() + attachment_size);
    Store32(header + 12, fb_method->service()->index());
    Store32(header + 16, static_cast<uint32_t>(fb_method->index()));
    Store32(header + 20, static_cast<uint32_t>(request.size()));
    Store32(header + 24, static_cast<uint32_t>(attachment_size));
    Store64(header + 28, correlation_id);
    if (buf->append(header, sizeof(header)) != 0) {
        cntl->SetFailed(ENOMEM, "Fail to allocate FlatBuffers request header");
        return;
    }
    buf->append(request);
    buf->append(cntl->request_attachment());
}

const std::string& GetFlatBuffersMethodName(
    const google::protobuf::MethodDescriptor*, const Controller* cntl) {
    static const std::string empty;
    return cntl->flatbuffers_method() ? cntl->flatbuffers_method()->full_name() : empty;
}

}  // namespace policy
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
