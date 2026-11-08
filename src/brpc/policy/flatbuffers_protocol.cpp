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
#include "butil/macros.h"
#include "butil/raw_pack.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/flatbuffers/message.h"
#include "brpc/flatbuffers/service.h"
#include "brpc/policy/flatbuffers_rpc_meta_generated.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" void bthread_assign_data(void* data);

namespace brpc {
namespace policy {
namespace {

const size_t kHeaderSize = 12;

// The 12-byte header is "FRPC", body_size and meta_size, both in network byte
// order and packed with butil::RawPacker into alignas(4) buffers. The body
// starts with a frpc::RequestMeta or frpc::ResponseMeta table
// (flatbuffers_rpc_meta.fbs) of meta_size bytes.

// Lets a FlatBufferBuilder build the small metadata tables without touching
// the heap. Larger buffers fall back to new[].
class MetaAllocator : public ::flatbuffers::Allocator {
public:
    MetaAllocator() : _in_use(false) {}

    uint8_t* allocate(size_t size) override {
        if (!_in_use && size <= sizeof(_buf)) {
            _in_use = true;
            return _buf;
        }
        return new uint8_t[size];
    }

    void deallocate(uint8_t* p, size_t) override {
        if (p == _buf) {
            _in_use = false;
        } else {
            delete[] p;
        }
    }

    static const size_t kSize = 128;

private:
    alignas(8) uint8_t _buf[kSize];
    bool _in_use;
};

// A FlatBufferBuilder for one metadata table. The allocator is declared
// first so that it outlives the builder.
struct MetaBuilder {
    MetaBuilder() : fbb(MetaAllocator::kSize, &allocator, false) {}

    MetaAllocator allocator;
    ::flatbuffers::FlatBufferBuilder fbb;

    DISALLOW_COPY_AND_ASSIGN(MetaBuilder);
};

// Copies the metadata out of `meta' into memory aligned for the table,
// verifies it and returns the root table, or nullptr if it is malformed.
class MetaReader {
public:
    template <typename T>
    const T* Read(const butil::IOBuf& meta) {
        const size_t size = meta.size();
        uint8_t* data = reinterpret_cast<uint8_t*>(_small);
        if (size > sizeof(_small)) {
            _large.reset(new uint64_t[(size + 7) / 8]);
            data = reinterpret_cast<uint8_t*>(_large.get());
        }
        if (size == 0 || meta.copy_to(data, size) != size) {
            return nullptr;
        }
        ::flatbuffers::Verifier verifier(data, size);
        if (!verifier.VerifyBuffer<T>(nullptr)) {
            return nullptr;
        }
        return ::flatbuffers::GetRoot<T>(data);
    }

private:
    uint64_t _small[16];
    std::unique_ptr<uint64_t[]> _large;
};

bool ValidSizes(size_t message_size, size_t attachment_size, size_t meta_size) {
    const size_t limit = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    return message_size <= limit && attachment_size <= limit &&
           meta_size <= limit && message_size <= limit - meta_size &&
           attachment_size <= limit - meta_size - message_size &&
           meta_size + message_size + attachment_size <=
               static_cast<size_t>(FLAGS_max_body_size);
}

// Appends the FRPC header and the metadata table in `meta' to `out'.
int AppendHeaderAndMeta(const MetaBuilder& meta, size_t payload_size,
                        butil::IOBuf* out) {
    const size_t meta_size = meta.fbb.GetSize();
    alignas(4) char header[kHeaderSize];
    memcpy(header, "FRPC", 4);
    butil::RawPacker(header + 4)
        .pack32(static_cast<uint32_t>(meta_size + payload_size))
        .pack32(static_cast<uint32_t>(meta_size));
    if (out->append(header, sizeof(header)) != 0) {
        return -1;
    }
    return out->append(meta.fbb.GetBufferPointer(), meta_size);
}

void BuildResponseMeta(MetaBuilder* meta, int error_code, size_t message_size,
                       size_t attachment_size, uint64_t correlation_id) {
    meta->fbb.Clear();
    meta->fbb.Finish(frpc::CreateResponseMeta(
        meta->fbb, error_code, static_cast<uint32_t>(message_size),
        static_cast<uint32_t>(attachment_size), correlation_id));
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
            }
        }
        MetaBuilder meta;
        if (!cntl->Failed()) {
            BuildResponseMeta(&meta, 0, payload.size(), attachment_size,
                              correlation_id);
            if (!ValidSizes(payload.size(), attachment_size, meta.fbb.GetSize())) {
                cntl->SetFailed(ERESPONSE, "FlatBuffers response is too large");
            }
        }
        if (cntl->Failed()) {
            payload.clear();
            attachment_size = 0;
            BuildResponseMeta(&meta, cntl->ErrorCode(), 0, 0, correlation_id);
        }

        butil::IOBuf wire;
        if (AppendHeaderAndMeta(meta, payload.size() + attachment_size, &wire) != 0) {
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

private:
    DISALLOW_COPY_AND_ASSIGN(FlatBuffersCall);
};

}  // namespace

ParseResult ParseFlatBuffersMessage(butil::IOBuf* source, Socket*, bool,
                                   const void*) {
    alignas(4) char header[kHeaderSize] = {};
    const size_t n = source->copy_to(header, sizeof(header));
    if (memcmp(header, "FRPC", std::min(n, size_t(4))) != 0) {
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    if (n < sizeof(header)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size = 0;
    uint32_t meta_size = 0;
    butil::RawUnpacker(header + 4).unpack32(body_size).unpack32(meta_size);
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
    MetaReader reader;
    const frpc::RequestMeta* meta = reader.Read<frpc::RequestMeta>(message->meta);
    if (meta == nullptr) {
        socket->SetFailed(EREQUEST, "Invalid FlatBuffers request metadata");
        return;
    }
    const uint32_t service_id = meta->service_id();
    const uint32_t method_id = meta->method_id();
    const uint32_t message_size = meta->message_size();
    const uint32_t attachment_size = meta->attachment_size();
    auto call = std::make_unique<FlatBuffersCall>(meta->correlation_id(),
                                                  message->received_us());
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
        const Server::MethodProperty* property =
            server_accessor.FindFlatBuffersMethodPropertyByIndex(service_id, method_id);
        if (!property) {
            cntl->SetFailed(ENOMETHOD, "Unknown FlatBuffers service=%u method=%u",
                            service_id, method_id);
            break;
        }
        call->service = property->fb_service;
        call->method = property->fb_method;
        accessor.set_flatbuffers_method(property->fb_method);
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
    MetaReader reader;
    const frpc::ResponseMeta* meta = reader.Read<frpc::ResponseMeta>(message->meta);
    if (meta == nullptr) {
        if (message->socket()) {
            message->socket()->SetFailed(ERESPONSE, "Invalid FlatBuffers response metadata");
        }
        return;
    }
    const int32_t error_code = meta->error_code();
    const uint32_t message_size = meta->message_size();
    const uint32_t attachment_size = meta->attachment_size();
    const bthread_id_t correlation_id = {meta->correlation_id()};
    Controller* cntl = nullptr;
    const int rc = bthread_id_lock(correlation_id, reinterpret_cast<void**>(&cntl));
    if (rc != 0) {
        return;
    }
    // A live correlation id alone does not prove that this response belongs
    // to the attempt: it must also arrive on the socket the attempt used.
    const SocketId response_socket_id =
        message->socket() ? message->socket()->id() : INVALID_SOCKET_ID;
    if (cntl == nullptr || !ControllerPrivateAccessor(cntl)
            .does_response_match_request_socket(correlation_id, response_socket_id)) {
        LOG(WARNING) << "correlation_id=" << correlation_id.value
                     << " of the FlatBuffers response from socket "
                     << response_socket_id
                     << " does not match a request sent over it, drop it";
        CHECK_EQ(0, bthread_id_unlock(correlation_id));
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
            cntl->SetFailed(error_code, "FlatBuffers server error");
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
    MetaBuilder meta;
    meta.fbb.Finish(frpc::CreateRequestMeta(
        meta.fbb, fb_method->service()->index(),
        static_cast<uint32_t>(fb_method->index()),
        static_cast<uint32_t>(request.size()),
        static_cast<uint32_t>(attachment_size), correlation_id));
    if (!request.size() ||
        !ValidSizes(request.size(), attachment_size, meta.fbb.GetSize())) {
        cntl->SetFailed(EREQUEST, "Invalid FlatBuffers request size");
        return;
    }
    if (AppendHeaderAndMeta(meta, request.size() + attachment_size, buf) != 0) {
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
