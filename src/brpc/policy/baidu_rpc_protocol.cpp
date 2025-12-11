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


#include <cinttypes>                            // PRId64, PRIu64
#include <cstdint>                               // UINT32_MAX, INT32_MAX
#include <climits>                               // INT32_MAX
#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/text_format.h>
#include "butil/logging.h"                       // LOG()
#include "butil/iobuf.h"                         // butil::IOBuf
#include "butil/raw_pack.h"                      // RawPacker RawUnpacker
#include "butil/memory/scope_guard.h"
#include "json2pb/json_to_pb.h"
#include "json2pb/pb_to_json.h"
#include "brpc/controller.h"                    // Controller
#include "brpc/socket.h"                        // Socket
#include "brpc/server.h"                        // Server
#include "brpc/span.h"
#include "brpc/compress.h"                      // ParseFromCompressedData
#include "brpc/checksum.h"
#include "brpc/stream_impl.h"
#include "brpc/rpc_dump.h"                      // SampledRequest
#include "brpc/rpc_pb_message_factory.h"
#include "brpc/policy/baidu_rpc_meta.pb.h"      // RpcRequestMeta
#include "brpc/policy/baidu_rpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/streaming_rpc_protocol.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {
namespace policy {

DEFINE_bool(baidu_protocol_use_fullname, true,
            "If this flag is true, baidu_std puts service.full_name in requests"
            ", otherwise puts service.name (required by jprotobuf).");

DEFINE_bool(baidu_std_protocol_deliver_timeout_ms, false,
            "If this flag is true, baidu_std puts timeout_ms in requests.");

DECLARE_bool(pb_enum_as_number);

// Notes:
// 1. Header format:
//    - Normal format (12 bytes): [PRPC][body_size(32bit)][meta_size(32bit)]
//    - Extended format (20 bytes): [PRPC][UINT32_MAX][meta_size(32bit)][body_size(64bit)]
//      Extended format is used when body_size > UINT32_MAX
// 2. body_size and meta_size are in network byte order
// 3. Use service->full_name() + method_name to specify the method to call
// 4. `attachment_size' is set iff request/response has attachment
// 5. Not supported: chunk_info

// Helper function to get attachment size from RpcMeta, with backward compatibility
static int64_t GetAttachmentSize(const RpcMeta& meta) {
    if (meta.has_attachment_size_long()) {
        return meta.attachment_size_long();
    }
    if (meta.has_attachment_size()) {
        return static_cast<int64_t>(meta.attachment_size());
    }
    return 0;
}

// Helper function to set attachment size in RpcMeta, with backward compatibility
static void SetAttachmentSize(RpcMeta* meta, size_t size) {
    const size_t INT32_MAX_VALUE = static_cast<size_t>(INT32_MAX);
    if (size > INT32_MAX_VALUE) {
        meta->set_attachment_size_long(static_cast<int64_t>(size));
    } else {
        meta->set_attachment_size(static_cast<int32_t>(size));
    }
}

// Helper function to get attachment size from RpcDumpMeta, with backward compatibility
// Marked unused to avoid -Werror-unused-function when not referenced.
static __attribute__((unused)) int64_t GetAttachmentSizeFromDump(const RpcDumpMeta& meta) {
    if (meta.has_attachment_size_long()) {
        return meta.attachment_size_long();
    }
    if (meta.has_attachment_size()) {
        return static_cast<int64_t>(meta.attachment_size());
    }
    return 0;
}

// Helper function to set attachment size in RpcDumpMeta, with backward compatibility
static void SetAttachmentSizeInDump(RpcDumpMeta* meta, size_t size) {
    const size_t INT32_MAX_VALUE = static_cast<size_t>(INT32_MAX);
    if (size > INT32_MAX_VALUE) {
        meta->set_attachment_size_long(static_cast<int64_t>(size));
    } else {
        meta->set_attachment_size(static_cast<int32_t>(size));
    }
}

// Pack header into `buf'
// Returns the size of header written (12 for normal, 20 for extended)
inline size_t PackRpcHeader(char* rpc_header, uint32_t meta_size, size_t payload_size) {
    uint32_t* dummy = (uint32_t*)rpc_header;  // suppress strict-alias warning
    *dummy = *(uint32_t*)"PRPC";
    const uint64_t total_size = static_cast<uint64_t>(meta_size) + payload_size;
    if (total_size > UINT32_MAX) {
        // Extended format: use UINT32_MAX as flag, followed by 64-bit total_size
        butil::RawPacker(rpc_header + 4)
            .pack32(UINT32_MAX)
            .pack32(meta_size)
            .pack64(total_size);
        return 20;  // 4 (magic) + 4 (flag) + 4 (meta_size) + 8 (total_size)
    } else {
        // Normal format: 32-bit total_size
        butil::RawPacker(rpc_header + 4)
            .pack32(static_cast<uint32_t>(total_size))
            .pack32(meta_size);
        return 12;  // 4 (magic) + 4 (body_size) + 4 (meta_size)
    }
}

static void SerializeRpcHeaderAndMeta(
    butil::IOBuf* out, const RpcMeta& meta, size_t payload_size) {
    const uint32_t meta_size = GetProtobufByteSize(meta);
    const uint64_t total_size = static_cast<uint64_t>(meta_size) + payload_size;
    const bool use_extended = (total_size > UINT32_MAX);
    
    if (meta_size <= 244 && !use_extended) { 
        // Most common cases with normal format: optimize by combining header and meta
        char header_and_meta[12 + meta_size];
        const size_t actual_header_size = PackRpcHeader(header_and_meta, meta_size, payload_size);
        CHECK_EQ(actual_header_size, 12U);  // Should be 12 for normal format
        ::google::protobuf::io::ArrayOutputStream arr_out(header_and_meta + 12, meta_size);
        ::google::protobuf::io::CodedOutputStream coded_out(&arr_out);
        meta.SerializeWithCachedSizes(&coded_out); // not calling ByteSize again
        CHECK(!coded_out.HadError());
        CHECK_EQ(0, out->append(header_and_meta, sizeof(header_and_meta)));
    } else {
        // Extended format or large meta: write header and meta separately
        char header[20];  // Enough for both normal and extended format
        const size_t actual_header_size = PackRpcHeader(header, meta_size, payload_size);
        CHECK_EQ(0, out->append(header, actual_header_size));
        butil::IOBufAsZeroCopyOutputStream buf_stream(out);
        ::google::protobuf::io::CodedOutputStream coded_out(&buf_stream);
        meta.SerializeWithCachedSizes(&coded_out);
        CHECK(!coded_out.HadError());
    }
}

ParseResult ParseRpcMessage(butil::IOBuf* source, Socket* socket,
                            bool /*read_eof*/, const void*) {
    // First read at least 12 bytes to check magic and determine format
    char header_buf[20];
    const size_t n = source->copy_to(header_buf, 12);
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"PRPC") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "PRPC", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < 12) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    
    uint32_t body_size_32;
    uint32_t meta_size;
    uint64_t body_size;
    size_t header_size;
    
    butil::RawUnpacker unpacker(header_buf + 4);
    unpacker.unpack32(body_size_32).unpack32(meta_size);
    
    if (body_size_32 == UINT32_MAX) {
        // Extended format: read 8 more bytes for 64-bit body_size
        if (source->length() < 20) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        source->copy_to(header_buf, 20);
        unpacker = butil::RawUnpacker(header_buf + 12);
        unpacker.unpack64(body_size);
        header_size = 20;
    } else {
        // Normal format: use 32-bit body_size
        body_size = static_cast<uint64_t>(body_size_32);
        header_size = 12;
    }
    
    if (body_size > FLAGS_max_body_size) {
        // We need this log to report the body_size to give users some clues
        // which is not printed in InputMessenger.
        LOG(ERROR) << "body_size=" << body_size << " from "
                   << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < header_size + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (meta_size > body_size) {
        LOG(ERROR) << "meta_size=" << meta_size << " is bigger than body_size="
                   << body_size;
        // Pop the message
        source->pop_front(header_size + body_size);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    source->pop_front(header_size);
    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, meta_size);
    source->cutn(&msg->payload, body_size - meta_size);
    return MakeMessage(msg);
}

bool SerializeRpcMessage(const google::protobuf::Message& message,
                         Controller& cntl, ContentType content_type,
                         CompressType compress_type, ChecksumType checksum_type,
                         butil::IOBuf* buf) {
    auto serialize = [&](Serializer& serializer) -> bool {
        bool ok;
        if (COMPRESS_TYPE_NONE == compress_type) {
            butil::IOBufAsZeroCopyOutputStream stream(buf);
            ok = serializer.SerializeTo(&stream);
        } else {
            const CompressHandler* handler = FindCompressHandler(compress_type);
            if (NULL == handler) {
                return false;
            }
            ok = handler->Compress(serializer, buf);
        }
        ChecksumIn checksum_in{buf, &cntl};
        ComputeDataChecksum(checksum_in, checksum_type);
        return ok;
    };

    if (CONTENT_TYPE_PB == content_type) {
        Serializer serializer([&message](google::protobuf::io::ZeroCopyOutputStream* output) -> bool {
            return message.SerializeToZeroCopyStream(output);
        });
        return serialize(serializer);
    } else if (CONTENT_TYPE_JSON == content_type) {
        Serializer serializer([&message, &cntl](google::protobuf::io::ZeroCopyOutputStream* output) -> bool {
            json2pb::Pb2JsonOptions options;
            options.bytes_to_base64 = cntl.has_pb_bytes_to_base64();
            options.jsonify_empty_array = cntl.has_pb_jsonify_empty_array();
            options.always_print_primitive_fields = cntl.has_always_print_primitive_fields();
            options.single_repeated_to_array = cntl.has_pb_single_repeated_to_array();
            options.enum_option = FLAGS_pb_enum_as_number
                                  ? json2pb::OUTPUT_ENUM_BY_NUMBER
                                  : json2pb::OUTPUT_ENUM_BY_NAME;
            std::string error;
            bool ok = json2pb::ProtoMessageToJson(message, output, options, &error);
            if (!ok) {
                LOG(INFO) << "Fail to serialize message="
                          << message.GetDescriptor()->full_name()
                          << " to json :" << error;
            }
            return ok;
        });
        return serialize(serializer);
    } else if (CONTENT_TYPE_PROTO_JSON == content_type) {
        Serializer serializer([&message, &cntl](google::protobuf::io::ZeroCopyOutputStream* output) -> bool {
            json2pb::Pb2ProtoJsonOptions options;
            options.always_print_enums_as_ints = FLAGS_pb_enum_as_number;
            AlwaysPrintPrimitiveFields(options) = cntl.has_always_print_primitive_fields();
            std::string error;
            bool ok = json2pb::ProtoMessageToProtoJson(message, output, options, &error);
            if (!ok) {
                LOG(INFO) << "Fail to serialize message="
                          << message.GetDescriptor()->full_name()
                          << " to proto-json :" << error;
            }
            return ok;
        });
        return serialize(serializer);
    } else if (CONTENT_TYPE_PROTO_TEXT == content_type) {
        Serializer serializer([&message](google::protobuf::io::ZeroCopyOutputStream* output) -> bool {
            return google::protobuf::TextFormat::Print(message, output);
        });
        return serialize(serializer);
    }
    return false;
}

static bool SerializeResponse(const google::protobuf::Message& res,
                              Controller& cntl, butil::IOBuf& buf) {
    if (res.GetDescriptor() == SerializedResponse::descriptor()) {
        buf.swap(((SerializedResponse&)res).serialized_data());
        return true;
    }

    if (!res.IsInitialized()) {
        cntl.SetFailed(ERESPONSE, "Missing required fields in response: %s",
                       res.InitializationErrorString().c_str());
        return false;
    }

    ContentType content_type = cntl.response_content_type();
    CompressType compress_type = cntl.response_compress_type();
    ChecksumType checksum_type = cntl.response_checksum_type();
    if (!SerializeRpcMessage(res, cntl, content_type, compress_type,
                             checksum_type, &buf)) {
        cntl.SetFailed(ERESPONSE,
                       "Fail to serialize response=%s, "
                       "ContentType=%s, CompressType=%s, ChecksumType=%s",
                       res.GetDescriptor()->full_name().c_str(),
                       ContentTypeToCStr(content_type),
                       CompressTypeToCStr(compress_type),
                       ChecksumTypeToCStr(checksum_type));
        return false;
    }
    return true;
}

namespace {
struct BaiduProxyPBMessages : public RpcPBMessages {
    static BaiduProxyPBMessages* Get() {
        return butil::get_object<BaiduProxyPBMessages>();
    }

    static void Return(BaiduProxyPBMessages* messages) {
        messages->Clear();
        butil::return_object(messages);
    }

    void Clear() {
        request.Clear();
        response.Clear();
    }

    ::google::protobuf::Message* Request() override { return &request; }
    ::google::protobuf::Message* Response() override { return &response; }

    SerializedRequest request;
    SerializedResponse response;
};
}

// Used by UT, can't be static.
void SendRpcResponse(int64_t correlation_id, Controller* cntl,
                     RpcPBMessages* messages, const Server* server,
                     MethodStatus* method_status, int64_t received_us) {
    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    Socket* sock = accessor.get_sending_socket();

    const google::protobuf::Message* req = NULL == messages ? NULL : messages->Request();
    const google::protobuf::Message* res = NULL == messages ? NULL : messages->Response();

    // Recycle resources at the end of this function.
    BRPC_SCOPE_EXIT {
        {
            // Remove concurrency and record latency at first.
            ConcurrencyRemover concurrency_remover(method_status, cntl, received_us);
        }

        std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);

        if (NULL == messages) {
            return;
        }

        cntl->CallAfterRpcResp(req, res);
        if (NULL == server->options().baidu_master_service) {
            server->options().rpc_pb_message_factory->Return(messages);
        } else {
            BaiduProxyPBMessages::Return(static_cast<BaiduProxyPBMessages*>(messages));
        }
    };
    
    StreamIds response_stream_ids = accessor.response_streams();

    if (cntl->IsCloseConnection()) {
        for(size_t i = 0; i < response_stream_ids.size(); ++i) {
            StreamClose(response_stream_ids[i]);
        }
        sock->SetFailed();
        return;
    }
    bool append_body = false;
    butil::IOBuf res_body;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    if (res != NULL && !cntl->Failed()) {
        append_body = SerializeResponse(*res, *cntl, res_body);
    }

    // Don't use res->ByteSize() since it may be compressed
    size_t res_size = 0;
    size_t attached_size = 0;
    if (append_body) {
        res_size = res_body.length();
        attached_size = cntl->response_attachment().length();
    }

    int error_code = cntl->ErrorCode();
    if (error_code == -1) {
        // replace general error (-1) with INTERNAL_SERVER_ERROR to make a
        // distinction between server error and client error
        error_code = EINTERNAL;
    }
    RpcMeta meta;
    RpcResponseMeta* response_meta = meta.mutable_response();
    response_meta->set_error_code(error_code);
    if (!cntl->ErrorText().empty()) {
        // Only set error_text when it's not empty since protobuf Message
        // always new the string no matter if it's empty or not.
        response_meta->set_error_text(cntl->ErrorText());
    }
    meta.set_correlation_id(correlation_id);
    meta.set_compress_type(cntl->response_compress_type());
    meta.set_content_type(cntl->response_content_type());
    meta.set_checksum_type(cntl->response_checksum_type());
    meta.set_checksum_value(accessor.checksum_value());
    if (attached_size > 0) {
        SetAttachmentSize(&meta, attached_size);
    }
    StreamId response_stream_id = INVALID_STREAM_ID;
    SocketUniquePtr stream_ptr;
    if (!response_stream_ids.empty()) {
        response_stream_id = response_stream_ids[0];
        if (Socket::Address(response_stream_id, &stream_ptr) == 0) {
            Stream* s = (Stream *) stream_ptr->conn();
            StreamSettings *stream_settings = meta.mutable_stream_settings();
            s->FillSettings(stream_settings);
            s->SetHostSocket(sock);
            for (size_t i = 1; i < response_stream_ids.size(); ++i) {
                stream_settings->mutable_extra_stream_ids()->Add(response_stream_ids[i]);
            }
        } else {
            LOG(WARNING) << "Stream=" << response_stream_id 
                         << " was closed before sending response";
        }
    }

    if (cntl->has_response_user_fields() &&
        !cntl->response_user_fields()->empty()) {
        ::google::protobuf::Map<std::string, std::string>& user_fields
            = *meta.mutable_user_fields();
        user_fields.insert(cntl->response_user_fields()->begin(),
                           cntl->response_user_fields()->end());

    }

    butil::IOBuf res_buf;
    SerializeRpcHeaderAndMeta(&res_buf, meta, res_size + attached_size);
    if (append_body) {
        res_buf.append(res_body.movable());
        if (attached_size > 0) {
            res_buf.append(cntl->response_attachment().movable());
        }
    }

    ResponseWriteInfo args;
    bthread_id_t response_id = INVALID_BTHREAD_ID;
    if (span) {
        span->set_response_size(res_buf.size());
        CHECK_EQ(0, bthread_id_create(&response_id, &args, HandleResponseWritten));
    }

    // Send rpc response over stream even if server side failed to create
    // stream for some reason.
    if (cntl->has_remote_stream()) {
        // Send the response over stream to notify that this stream connection
        // is successfully built.
        // Response_stream can be INVALID_STREAM_ID when error occurs.
        if (SendStreamData(sock, &res_buf,
                           accessor.remote_stream_settings()->stream_id(),
                           response_stream_id, response_id) != 0) {
            error_code = errno;
            PLOG_IF(WARNING, error_code != EPIPE)
                << "Fail to write into " << sock->description();
            cntl->SetFailed(error_code,  "Fail to write into %s",
                            sock->description().c_str());
            Stream::SetFailed(response_stream_ids, error_code,
                              "Fail to write into %s",
                              sock->description().c_str());
            return;
        }

        // Now it's ok the mark these server-side streams as connected as all the
        // written user data would follower the RPC response.
        // Reuse stream_ptr to avoid address first stream id again
        if (stream_ptr) {
            ((Stream*)stream_ptr->conn())->SetConnected();
        }
        for (size_t i = 1; i < response_stream_ids.size(); ++i) {
            StreamId extra_stream_id = response_stream_ids[i];
            SocketUniquePtr extra_stream_ptr;
            if (Socket::Address(extra_stream_id, &extra_stream_ptr) == 0) {
                Stream* extra_stream = (Stream *) extra_stream_ptr->conn();
                extra_stream->SetHostSocket(sock);
                extra_stream->SetConnected();
            } else {
                LOG(WARNING) << "Stream=" << extra_stream_id
                             << " was closed before sending response";
            }
        }
    } else{
        // Have the risk of unlimited pending responses, in which case, tell
        // users to set max_concurrency.
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        if (INVALID_BTHREAD_ID != response_id) {
            wopt.id_wait = response_id;
            wopt.notify_on_success = true;
        }
        if (sock->Write(&res_buf, &wopt) != 0) {
            const int errcode = errno;
            PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
            cntl->SetFailed(errcode, "Fail to write into %s",
                            sock->description().c_str());
            return;
        }
    }

    if (span) {
        bthread_id_join(response_id);
        // Do not care about the result of background writing.
        // TODO: this is not sent
        span->set_sent_us(args.sent_us);
    }
}

namespace {
struct CallMethodInBackupThreadArgs {
    ::google::protobuf::Service* service;
    const ::google::protobuf::MethodDescriptor* method;
    ::google::protobuf::RpcController* controller;
    const ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
    ::google::protobuf::Closure* done;
};
}

static void CallMethodInBackupThread(void* void_args) {
    CallMethodInBackupThreadArgs* args = (CallMethodInBackupThreadArgs*)void_args;
    args->service->CallMethod(args->method, args->controller, args->request,
                              args->response, args->done);
    delete args;
}

// Used by other protocols as well.
void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done) {
    CallMethodInBackupThreadArgs* args = new CallMethodInBackupThreadArgs;
    args->service = service;
    args->method = method;
    args->controller = controller;
    args->request = request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread, args);
};

bool DeserializeRpcMessage(const butil::IOBuf& data, Controller& cntl,
                           ContentType content_type, CompressType compress_type,
                           ChecksumType checksum_type,
                           google::protobuf::Message* message) {
    auto deserialize = [&](Deserializer& deserializer) -> bool {
        ChecksumIn checksum_in{&data, &cntl};
        bool ok = VerifyDataChecksum(checksum_in, checksum_type);
        if (!ok) {
            return ok;
        }
        if (COMPRESS_TYPE_NONE == compress_type) {
            butil::IOBufAsZeroCopyInputStream stream(data);
            ok = deserializer.DeserializeFrom(&stream);
        } else {
            const CompressHandler* handler = FindCompressHandler(compress_type);
            if (NULL == handler) {
                return false;
            }
            ok = handler->Decompress(data, &deserializer);
        }
        return ok;
    };

    if (CONTENT_TYPE_PB == content_type) {
        Deserializer deserializer([message](
            google::protobuf::io::ZeroCopyInputStream* input) -> bool {
            return message->ParseFromZeroCopyStream(input);
        });
        return deserialize(deserializer);
    } else if (CONTENT_TYPE_JSON == content_type) {
        Deserializer deserializer([message, &cntl](
            google::protobuf::io::ZeroCopyInputStream* input) -> bool {
            json2pb::Json2PbOptions options;
            options.base64_to_bytes = cntl.has_pb_bytes_to_base64();
            options.array_to_single_repeated = cntl.has_pb_single_repeated_to_array();
            std::string error;
            bool ok = json2pb::JsonToProtoMessage(input, message, options, &error);
            if (!ok) {
                LOG(INFO) << "Fail to parse json to "
                          << message->GetDescriptor()->full_name()
                          << ": "<< error;
            }
            return ok;
        });
        return deserialize(deserializer);
    } else if (CONTENT_TYPE_PROTO_JSON == content_type) {
        Deserializer deserializer([message](
            google::protobuf::io::ZeroCopyInputStream* input) -> bool {
            json2pb::ProtoJson2PbOptions options;
            options.ignore_unknown_fields = true;
            std::string error;
            bool ok = json2pb::ProtoJsonToProtoMessage(input, message, options, &error);
            if (!ok) {
                LOG(INFO) << "Fail to parse proto-json to "
                          << message->GetDescriptor()->full_name()
                          << ": "<< error;
            }
            return ok;
        });
        return deserialize(deserializer);
    } else if (CONTENT_TYPE_PROTO_TEXT == content_type) {
        Deserializer deserializer([message](
            google::protobuf::io::ZeroCopyInputStream* input) -> bool {
            return google::protobuf::TextFormat::Parse(input, message);
        });
        return deserialize(deserializer);
    }
    return false;
}

void ProcessRpcRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    RpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse RpcMeta from " << *socket;
        socket->SetFailed(EREQUEST, "Fail to parse RpcMeta from %s",
                          socket->description().c_str());
        return;
    }
    const RpcRequestMeta &request_meta = meta.request();

    SampledRequest* sample = AskToBeSampled();
    if (sample) {
        sample->meta.set_service_name(request_meta.service_name());
        sample->meta.set_method_name(request_meta.method_name());
        sample->meta.set_compress_type((CompressType)meta.compress_type());
        sample->meta.set_protocol_type(PROTOCOL_BAIDU_STD);
        const int64_t attachment_size = GetAttachmentSize(meta);
        // Only set attachment_size if it's valid (non-negative)
        if (attachment_size > 0) {
            SetAttachmentSizeInDump(&sample->meta, static_cast<size_t>(attachment_size));
        } else if (attachment_size < 0) {
            // Log warning for invalid negative attachment_size in sampling
            LOG(WARNING) << "Invalid negative attachment_size=" << attachment_size 
                         << " in sampled request, ignoring";
        }
        sample->meta.set_authentication_data(meta.authentication_data());
        sample->request = msg->payload;
        sample->submit(start_parse_us);
    }

    std::unique_ptr<Controller> cntl(new (std::nothrow) Controller);
    if (NULL == cntl.get()) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }

    RpcPBMessages* messages = NULL;

    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl.get());
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    if (request_meta.has_log_id()) {
        cntl->set_log_id(request_meta.log_id());
    }
    if (request_meta.has_request_id()) {
        cntl->set_request_id(request_meta.request_id());
    }
    if (request_meta.has_timeout_ms()) {
        cntl->set_timeout_ms(request_meta.timeout_ms());
    }
    cntl->set_request_content_type(meta.content_type());
    cntl->set_request_compress_type((CompressType)meta.compress_type());
    cntl->set_request_checksum_type((ChecksumType)meta.checksum_type());
    cntl->set_rpc_received_us(msg->received_us());
    accessor.set_checksum_value(meta.checksum_value());
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_BAIDU_STD)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

    if (meta.has_stream_settings()) {
        accessor.set_remote_stream_settings(meta.release_stream_settings());
    }

    if (!meta.user_fields().empty()) {
        for (const auto& it : meta.user_fields()) {
            (*cntl->request_user_fields())[it.first] = it.second;
        }
    }

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(request_meta.has_trace_id())) {
        span = Span::CreateServerSpan(
            request_meta.trace_id(), request_meta.span_id(),
            request_meta.parent_span_id(), msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(request_meta.log_id());
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_BAIDU_STD);
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

        const size_t req_size = msg->payload.size();
        const int64_t attachment_size = GetAttachmentSize(meta);
        if (attachment_size < 0 || static_cast<size_t>(attachment_size) > req_size) {
            cntl->SetFailed(EREQUEST,
                "attachment_size=%" PRId64 " is invalid or larger than request_size=%zu",
                attachment_size, req_size);
            break;
        }

        google::protobuf::Service* svc = NULL;
        google::protobuf::MethodDescriptor* method = NULL;
        if (NULL != server->options().baidu_master_service) {
          if (socket->is_overcrowded() &&
              !server->options().ignore_eovercrowded &&
              !server->options().baidu_master_service->ignore_eovercrowded()) {
            cntl->SetFailed(EOVERCROWDED, "Connection to %s is overcrowded",
                            butil::endpoint2str(socket->remote_side()).c_str());
            break;
          }
            svc = server->options().baidu_master_service;
            auto sampled_request = new (std::nothrow) SampledRequest;
            if (NULL == sampled_request) {
                cntl->SetFailed(ENOMEM, "Fail to get sampled_request");
                break;
            }
            sampled_request->meta.set_service_name(request_meta.service_name());
            sampled_request->meta.set_method_name(request_meta.method_name());
            cntl->reset_sampled_request(sampled_request);
            // Switch to service-specific error.
            non_service_error.release();
            method_status = server->options().baidu_master_service->_status;
            if (method_status) {
                int rejected_cc = 0;
                if (!method_status->OnRequested(&rejected_cc, cntl.get())) {
                    cntl->SetFailed(
                        ELIMIT,
                        "Rejected by %s's ConcurrencyLimiter, concurrency=%d",
                        butil::class_name<BaiduMasterService>(), rejected_cc);
                    break;
                }
            }
            if (span) {
                span->ResetServerSpanName(sampled_request->meta.method_name());
            }

            messages = BaiduProxyPBMessages::Get();
            // attachment_size already retrieved and validated at line 777
            msg->payload.cutn(
                &((SerializedRequest*)messages->Request())->serialized_data(),
                req_size - static_cast<size_t>(attachment_size));
            if (!msg->payload.empty()) {
                cntl->request_attachment().swap(msg->payload);
            }
        } else {
            // NOTE(gejun): jprotobuf sends service names without packages. So the
            // name should be changed to full when it's not.
            butil::StringPiece svc_name(request_meta.service_name());
            if (svc_name.find('.') == butil::StringPiece::npos) {
                const Server::ServiceProperty* sp =
                    server_accessor.FindServicePropertyByName(svc_name);
                if (NULL == sp) {
                    cntl->SetFailed(ENOSERVICE, "Fail to find service=%s",
                        request_meta.service_name().c_str());
                    break;
                }
                svc_name = sp->service->GetDescriptor()->full_name();
            }
            const Server::MethodProperty* mp =
                server_accessor.FindMethodPropertyByFullName(
                    svc_name, request_meta.method_name());
            if (NULL == mp) {
                cntl->SetFailed(ENOMETHOD, "Fail to find method=%s/%s",
                                request_meta.service_name().c_str(),
                                request_meta.method_name().c_str());
                break;
            } else if (mp->service->GetDescriptor() == BadMethodService::descriptor()) {
                BadMethodRequest breq;
                BadMethodResponse bres;
                breq.set_service_name(request_meta.service_name());
                mp->service->CallMethod(mp->method, cntl.get(), &breq, &bres, NULL);
                break;
            }
            if (socket->is_overcrowded() &&
                !server->options().ignore_eovercrowded &&
                !mp->ignore_eovercrowded) {
              cntl->SetFailed(
                  EOVERCROWDED, "Connection to %s is overcrowded",
                  butil::endpoint2str(socket->remote_side()).c_str());
              break;
            }
            // Switch to service-specific error.
            non_service_error.release();
            method_status = mp->status;
            if (method_status) {
                int rejected_cc = 0;
                if (!method_status->OnRequested(&rejected_cc, cntl.get())) {
                    cntl->SetFailed(
                        ELIMIT,
                        "Rejected by %s's ConcurrencyLimiter, concurrency=%d",
                        mp->method->full_name().c_str(), rejected_cc);
                    break;
                }
            }
            svc = mp->service;
            method = const_cast<google::protobuf::MethodDescriptor*>(mp->method);
            accessor.set_method(method);

            if (span) {
                span->ResetServerSpanName(method->full_name());
            }

            if (!server->AcceptRequest(cntl.get())) {
                break;
            }

            butil::IOBuf req_buf;
            // attachment_size already retrieved and validated at line 772
            const size_t body_without_attachment_size = req_size - static_cast<size_t>(attachment_size);
            msg->payload.cutn(&req_buf, body_without_attachment_size);
            if (attachment_size > 0) {
                cntl->request_attachment().swap(msg->payload);
            }

            ContentType content_type = meta.content_type();
            auto compress_type =
                static_cast<CompressType>(meta.compress_type());
            auto checksum_type =
                static_cast<ChecksumType>(meta.checksum_type());
            messages =
                server->options().rpc_pb_message_factory->Get(*svc, *method);
            if (!DeserializeRpcMessage(req_buf, *cntl, content_type,
                                       compress_type, checksum_type,
                                       messages->Request())) {
                cntl->SetFailed(
                    EREQUEST,
                    "Fail to parse request=%s, ContentType=%s, "
                    "CompressType=%s, ChecksumType=%s, request_size=%zu",
                    messages->Request()->GetDescriptor()->full_name().c_str(),
                    ContentTypeToCStr(content_type),
                    CompressTypeToCStr(compress_type),
                    ChecksumTypeToCStr(checksum_type), req_size);
                break;
            }
            req_buf.clear();
        }

        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            int64_t, Controller*, RpcPBMessages*,
            const Server*, MethodStatus*, int64_t>(
                &SendRpcResponse, meta.correlation_id(),cntl.get(),
                messages, server, method_status, msg->received_us());

        // optional, just release resource ASAP
        msg.reset();

        if (span) {
            span->set_start_callback_us(butil::cpuwide_time_us());
            span->AsParent();
        }
        if (!FLAGS_usercode_in_pthread) {
            return svc->CallMethod(method, cntl.release(), 
                                   messages->Request(),
                                   messages->Response(), done);
        }
        if (BeginRunningUserCode()) {
            svc->CallMethod(method, cntl.release(), 
                            messages->Request(),
                            messages->Response(), done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool(
                svc, method, cntl.release(),
                messages->Request(),
                messages->Response(), done);
        }
    } while (false);
    
    // `cntl', `req' and `res' will be deleted inside `SendRpcResponse'
    // `socket' will be held until response has been sent
    SendRpcResponse(meta.correlation_id(),
                    cntl.release(), messages,
                    server, method_status,
                    msg->received_us());
}

bool VerifyRpcRequest(const InputMessageBase* msg_base) {
    const MostCommonMessage* msg =
        static_cast<const MostCommonMessage*>(msg_base);
    const Server* server = static_cast<const Server*>(msg->arg());
    Socket* socket = msg->socket();
    
    RpcMeta request_meta;
    if (!ParsePbFromIOBuf(&request_meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse RpcRequestMeta";
        return false;
    }
    const Authenticator* auth = server->options().auth;
    if (NULL == auth) {
        // Fast pass (no authentication)
        return true;
    }
    if (auth->VerifyCredential(request_meta.authentication_data(),
                               socket->remote_side(),
                               socket->mutable_auth_context()) == 0) {
        return true;
    }

    // Send `ERPCAUTH' to client.
    RpcMeta response_meta;
    response_meta.set_correlation_id(request_meta.correlation_id());
    response_meta.mutable_response()->set_error_code(ERPCAUTH);
    response_meta.mutable_response()->set_error_text("Fail to authenticate");
    std::string user_error_text = auth->GetUnauthorizedErrorText();
    if (!user_error_text.empty()) {
        response_meta.mutable_response()->mutable_error_text()->append(": ");
        response_meta.mutable_response()->mutable_error_text()->append(user_error_text);
    }
    butil::IOBuf res_buf;
    SerializeRpcHeaderAndMeta(&res_buf, response_meta, 0);
    Socket::WriteOptions opt;
    opt.ignore_eovercrowded = true;
    if (socket->Write(&res_buf, &opt) != 0) {
        PLOG_IF(WARNING, errno != EPIPE) << "Fail to write into " << *socket;
    }

    return false;
}

void ProcessRpcResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    RpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse from response meta";
        return;
    }

    const bthread_id_t cid = { static_cast<uint64_t>(meta.correlation_id()) };
    Controller* cntl = NULL;

    StreamId remote_stream_id = meta.has_stream_settings() ? meta.stream_settings().stream_id(): INVALID_STREAM_ID;

    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        if (remote_stream_id != INVALID_STREAM_ID) {
            SendStreamRst(msg->socket(), remote_stream_id);
            const auto & extra_stream_ids = meta.stream_settings().extra_stream_ids();
            for (int i = 0; i < extra_stream_ids.size(); ++i) {
                policy::SendStreamRst(msg->socket(), extra_stream_ids[i]);
            }
        }
        return;
    }
    
    ControllerPrivateAccessor accessor(cntl);
    if (remote_stream_id != INVALID_STREAM_ID) {
        accessor.set_remote_stream_settings(
                new StreamSettings(meta.stream_settings()));
    }

    if (!meta.user_fields().empty()) {
        for (const auto& it : meta.user_fields()) {
            (*cntl->response_user_fields())[it.first] = it.second;
        }
    }

    cntl->set_rpc_received_us(msg->received_us());
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(msg->base_real_us());
        span->set_received_us(msg->received_us());
        span->set_response_size(msg->meta.size() + msg->payload.size() + 12);
        span->set_start_parse_us(start_parse_us);
    }
    const RpcResponseMeta &response_meta = meta.response();
    const int saved_error = cntl->ErrorCode();
    do {
        if (response_meta.error_code() != 0) {
            // If error_code is unset, default is 0 = success.
            cntl->SetFailed(response_meta.error_code(), 
                                  "%s", response_meta.error_text().c_str());
            break;
        } 
        // Parse response message iff error code from meta is 0
        butil::IOBuf res_buf;
        const size_t res_size = msg->payload.length();
        butil::IOBuf* res_buf_ptr = &msg->payload;
        const int64_t attachment_size = GetAttachmentSize(meta);
        // Validate attachment_size: check for negative values and size overflow
        if (attachment_size < 0 || static_cast<size_t>(attachment_size) > res_size) {
            cntl->SetFailed(
                ERESPONSE, "attachment_size=%" PRId64 " is invalid or larger than response_size=%zu",
                attachment_size, res_size);
            break;
        }
        if (attachment_size > 0) {
            const size_t body_without_attachment_size = res_size - static_cast<size_t>(attachment_size);
            msg->payload.cutn(&res_buf, body_without_attachment_size);
            res_buf_ptr = &res_buf;
            cntl->response_attachment().swap(msg->payload);
        }

        ContentType content_type = meta.content_type();
        auto compress_type = (CompressType)meta.compress_type();
        auto checksum_type = (ChecksumType)meta.checksum_type();
        cntl->set_response_content_type(content_type);
        cntl->set_response_compress_type(compress_type);
        cntl->set_response_checksum_type(checksum_type);
        accessor.set_checksum_value(meta.checksum_value());
        if (cntl->response()) {
            if (cntl->response()->GetDescriptor() == SerializedResponse::descriptor()) {
                ((SerializedResponse*)cntl->response())->
                    serialized_data().append(*res_buf_ptr);
            } else if (!DeserializeRpcMessage(*res_buf_ptr, *cntl, content_type,
                                              compress_type, checksum_type,
                                              cntl->response())) {
                cntl->SetFailed(
                    EREQUEST,
                    "Fail to parse response=%s, ContentType=%s, "
                    "CompressType=%s, ChecksumType=%s, response_size=%zu",
                    cntl->response()->GetDescriptor()->full_name().c_str(),
                    ContentTypeToCStr(content_type),
                    CompressTypeToCStr(compress_type),
                    ChecksumTypeToCStr(checksum_type), res_size);
            }
        } // else silently ignore the response.
    } while (0);
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resource ASAP
    accessor.OnResponse(cid, saved_error);
}

void SerializeRpcRequest(butil::IOBuf* request_buf, Controller* cntl,
                         const google::protobuf::Message* request) {
    // Check sanity of request.
    if (NULL == request) {
        return cntl->SetFailed(EREQUEST, "`request' is NULL");
    }
    if (request->GetDescriptor() == SerializedRequest::descriptor()) {
        request_buf->append(((SerializedRequest*)request)->serialized_data());
        return;
    }
    if (!request->IsInitialized()) {
        return cntl->SetFailed(EREQUEST, "Missing required fields in request: %s",
                               request->InitializationErrorString().c_str());
    }

    ContentType content_type = cntl->request_content_type();
    CompressType compress_type = cntl->request_compress_type();
    ChecksumType checksum_type = cntl->request_checksum_type();
    if (!SerializeRpcMessage(*request, *cntl, content_type, compress_type,
                             checksum_type, request_buf)) {
        return cntl->SetFailed(
            EREQUEST,
            "Fail to compress request=%s, "
            "ContentType=%s, CompressType=%s, ChecksumType=%s",
            request->GetDescriptor()->full_name().c_str(),
            ContentTypeToCStr(content_type), CompressTypeToCStr(compress_type),
            ChecksumTypeToCStr(checksum_type));
    }
}

void PackRpcRequest(butil::IOBuf* req_buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method,
                    Controller* cntl,
                    const butil::IOBuf& request_body,
                    const Authenticator* auth) {
    RpcMeta meta;
    if (auth && auth->GenerateCredential(
            meta.mutable_authentication_data()) != 0) {
        return cntl->SetFailed(EREQUEST, "Fail to generate credential");
    }

    ControllerPrivateAccessor accessor(cntl);
    RpcRequestMeta* request_meta = meta.mutable_request();
    if (method) {
        request_meta->set_service_name(FLAGS_baidu_protocol_use_fullname ?
                                       method->service()->full_name() :
                                       method->service()->name());
        request_meta->set_method_name(method->name());
        meta.set_compress_type(cntl->request_compress_type());
        meta.set_checksum_type(cntl->request_checksum_type());
        meta.set_checksum_value(accessor.checksum_value());
    } else if (NULL != cntl->sampled_request()) {
        // Replaying. Keep service-name as the one seen by server.
        request_meta->set_service_name(cntl->sampled_request()->meta.service_name());
        request_meta->set_method_name(cntl->sampled_request()->meta.method_name());
        meta.set_compress_type(cntl->sampled_request()->meta.has_compress_type() ?
                               cntl->sampled_request()->meta.compress_type() :
                               cntl->request_compress_type());
    } else {
        return cntl->SetFailed(ENOMETHOD, "%s.method is NULL", __func__ );
    }
    if (cntl->has_log_id()) {
        request_meta->set_log_id(cntl->log_id());
    }
    if (!cntl->request_id().empty()) {
        request_meta->set_request_id(cntl->request_id());
    }
    meta.set_correlation_id(correlation_id);
    StreamIds request_stream_ids = accessor.request_streams();
    if (!request_stream_ids.empty()) {
        StreamSettings* stream_settings = meta.mutable_stream_settings();
        StreamId request_stream_id = request_stream_ids[0];
        SocketUniquePtr ptr;
        if (Socket::Address(request_stream_id, &ptr) != 0) {
            return cntl->SetFailed(EREQUEST, "Stream=%" PRIu64 " was closed",
                                   request_stream_id);
        }
        Stream* s = (Stream*) ptr->conn();
        s->FillSettings(stream_settings);
        for (size_t i = 1; i < request_stream_ids.size(); ++i) {
            stream_settings->mutable_extra_stream_ids()->Add(request_stream_ids[i]);
        }
    }

    if (cntl->has_request_user_fields() && !cntl->request_user_fields()->empty()) {
        ::google::protobuf::Map<std::string, std::string>& user_fields
            = *meta.mutable_user_fields();
        user_fields.insert(cntl->request_user_fields()->begin(),
                           cntl->request_user_fields()->end());
    }

    // Don't use res->ByteSize() since it may be compressed
    const size_t req_size = request_body.length(); 
    const size_t attached_size = cntl->request_attachment().length();
    if (attached_size) {
        SetAttachmentSize(&meta, attached_size);
    }

    if (FLAGS_baidu_std_protocol_deliver_timeout_ms) {
        if (accessor.real_timeout_ms() > 0) {
            request_meta->set_timeout_ms(accessor.real_timeout_ms());
        }
    }
    meta.set_content_type(cntl->request_content_type());

    Span* span = accessor.span();
    if (span) {
        request_meta->set_trace_id(span->trace_id());
        request_meta->set_span_id(span->span_id());
        request_meta->set_parent_span_id(span->parent_span_id());
    }

    SerializeRpcHeaderAndMeta(req_buf, meta, req_size + attached_size);
    req_buf->append(request_body);
    if (attached_size) {
        req_buf->append(cntl->request_attachment());
    }
}

const char* ContentTypeToCStr(ContentType content_type) {
    switch (content_type) {
    case CONTENT_TYPE_PB:
        return "pb";
    case CONTENT_TYPE_JSON:
        return "json";
    case CONTENT_TYPE_PROTO_JSON:
        return "proto-json";
    case CONTENT_TYPE_PROTO_TEXT:
        return "proto-text";
    default:
        return "unknown";
    }
}

}  // namespace policy
} // namespace brpc
