// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/27 16:01:56

#include <google/protobuf/descriptor.h>             // MethodDescriptor
#include <gflags/gflags.h>
#include <json2pb/pb_to_json.h>                    // ProtoMessageToJson
#include <json2pb/json_to_pb.h>                    // JsonToProtoMessage

#include "base/unique_ptr.h"                       // std::unique_ptr
#include "base/string_splitter.h"                  // StringMultiSplitter
#include "base/string_printf.h"
#include "base/time.h"
#include "brpc/compress.h"
#include "brpc/errno.pb.h"                     // ENOSERVICE, ENOMETHOD
#include "brpc/controller.h"                   // Controller
#include "brpc/server.h"                       // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/socket.h"                       // Socket
#include "brpc/http_status_code.h"             // HTTP_STATUS_*
#include "brpc/details/http_message_serializer.h" // HttpMessageSerializer
#include "brpc/details/controller_private_accessor.h"
#include "brpc/builtin/index_service.h"        // IndexService
#include "brpc/policy/gzip_compress.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data) __THROW;
}

namespace brpc {

int is_failed_after_queries(const http_parser* parser);
int is_failed_after_http_version(const http_parser* parser);
DECLARE_bool(http_verbose);
DECLARE_int32(http_verbose_max_body_length);

namespace policy {

DEFINE_int32(http_max_error_length, 2048, "Max printed length of a http error");

DEFINE_int32(http_body_compress_threshold, 512, "Not compress http body when "
             "it's less than so many bytes.");

DEFINE_string(http_header_of_user_ip, "", "http requests sent by proxies may "
              "set the client ip in http headers. When this flag is non-empty, "
              "baidu-rpc will read ip:port from the specified header for "
              "authorization and set Controller::remote_side()");

DEFINE_bool(pb_enum_as_number, false, "[Not recommended] Convert enums in "
            "protobuf to json as numbers, affecting both client-side and "
            "server-side");

// Read user address from the header specified by -http_header_of_user_ip
inline bool GetUserAddressFromHeader(const HttpHeader& headers,
                                     base::EndPoint* user_addr) {
    if (FLAGS_http_header_of_user_ip.empty()) {
        return false;
    }
    const std::string* user_addr_str =
        headers.GetHeader(FLAGS_http_header_of_user_ip);
    if (user_addr_str == NULL) {
        return false;
    }
    if (user_addr_str->find(':') == std::string::npos) {
        if (base::str2ip(user_addr_str->c_str(), &user_addr->ip) != 0) {
            LOG(WARNING) << "Fail to parse ip from " << *user_addr_str;
            return false;
        }
        user_addr->port = 0;
    } else {
        if (base::str2endpoint(user_addr_str->c_str(), user_addr) != 0) {
            LOG(WARNING) << "Fail to parse ip:port from " << *user_addr_str;
            return false;
        }
    }
    return true;
}

// Put commonly used std::strings (or other constants that need memory
// allocations) in this struct to avoid memory allocations for each request.
struct CommonStrings {
    CommonStrings()
        : CONTENT_TYPE_TEXT("text/plain")
        , CONTENT_TYPE_JSON("application/json")
        , CONTENT_TYPE_PROTO("application/proto")
        , ERROR_CODE("x-bd-error-code")
        , AUTHORIZATION("Authorization")
        , ACCEPT_ENCODING("Accept-Encoding")
        , CONTENT_ENCODING("Content-Encoding")
        , GZIP("gzip")
        , CONNECTION("Connection")
        , KEEP_ALIVE("keep-alive")
        , CLOSE("close")
        , LOG_ID("log-id")
        , DEFAULT_METHOD("default_method")
        , NO_METHOD("no_method")
    {}
    
    std::string CONTENT_TYPE_TEXT;
    std::string CONTENT_TYPE_JSON;
    std::string CONTENT_TYPE_PROTO;
    std::string ERROR_CODE;
    std::string AUTHORIZATION;
    std::string ACCEPT_ENCODING;
    std::string CONTENT_ENCODING;
    std::string GZIP;
    std::string CONNECTION;
    std::string KEEP_ALIVE;
    std::string CLOSE;
    // Many users already GetHeader("log-id") in their code, it's difficult to
    // rename this to `x-bd-log-id'.
    // NOTE: Keep in mind that this name also appears inside `http_message.cpp'
    std::string LOG_ID;
    std::string DEFAULT_METHOD;
    std::string NO_METHOD;
};

static CommonStrings* common = NULL;
static pthread_once_t g_common_strings_once = PTHREAD_ONCE_INIT;
static void CreateCommonStrings() {
    common = new CommonStrings;
}
// Called in global.cpp
int InitCommonStrings() {
    return pthread_once(&g_common_strings_once, CreateCommonStrings);
}
static const int ALLOW_UNUSED force_creation_of_common = InitCommonStrings();

enum HttpContentType{
    HTTP_CONTENT_JSON = 0,
    HTTP_CONTENT_PROTO = 1,
    HTTP_CONTENT_OTHERS = 2,
};

inline HttpContentType ParseContentType(base::StringPiece content_type) {
    const base::StringPiece prefix = "application/";
    const base::StringPiece json = "json";
    const base::StringPiece proto = "proto";

    // According to http://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html#sec3.7
    //   media-type  = type "/" subtype *( ";" parameter )
    //   type        = token
    //   subtype     = token
    if (!content_type.starts_with(prefix)) {
        return HTTP_CONTENT_OTHERS;
    }
    content_type.remove_prefix(prefix.size());
    HttpContentType type = HTTP_CONTENT_OTHERS;
    if (content_type.starts_with(json)) {
        type = HTTP_CONTENT_JSON;
        content_type.remove_prefix(json.size());
    } else if (content_type.starts_with(proto)) {
        type = HTTP_CONTENT_PROTO;
        content_type.remove_prefix(proto.size());
    } else {
        return HTTP_CONTENT_OTHERS;
    }
    return content_type.empty() || content_type.front() == ';' 
            ? type : HTTP_CONTENT_OTHERS;
}

static void PrintMessage(const base::IOBuf& inbuf,
                         bool request_or_response,
                         bool has_content) {
    base::IOBuf buf1 = inbuf;
    base::IOBuf buf2;
    char str[32];
    if (request_or_response) {
        snprintf(str, sizeof(str), "[HTTP REQUEST @%s]", base::my_ip_cstr());
    } else {
        snprintf(str, sizeof(str), "[HTTP RESPONSE @%s]", base::my_ip_cstr());
    }
    buf2.append(str);
    do {
        buf2.append("\r\n> ");
    } while (buf1.cut_until(&buf2, "\r\n") == 0);
    if (buf1.empty()) {
        buf2.pop_back(2);  // remove "> "
    } else if (!has_content) {
        buf2.append(buf1);
    } else {
        size_t nskipped = 0;
        if (buf1.size() > (size_t)FLAGS_http_verbose_max_body_length) {
            nskipped = buf1.size() - (size_t)FLAGS_http_verbose_max_body_length;
            buf1.pop_back(nskipped);
        }
        buf2.append(buf1);
        if (nskipped) {
            snprintf(str, sizeof(str), "\n<skipped %" PRIu64 " bytes>", nskipped);
            buf2.append(str);
        }
    }
    std::cerr << buf2 << std::endl;
}

void ProcessHttpResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = base::cpuwide_time_us();
    DestroyingPtr<HttpInputMessage> http_imsg(static_cast<HttpInputMessage*>(msg_base));
    Socket* socket = http_imsg->socket();
    if (socket == NULL) {
        LOG(ERROR) << "Invalid http_imsg=" << http_imsg.get();
    }

    if (socket->correlation_id() == 0) {
        LOG(WARNING) << "Fail to find correlation_id from " << *socket;
        return;
    }
    const bthread_id_t cid = { static_cast<uint64_t>(socket->correlation_id()) };
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(FATAL, rc != EINVAL) << "Fail to lock correlation_id="
                                    << cid.value << ": " << berror(rc);
        return;
    }
    
    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(http_imsg->base_real_us());
        span->set_received_us(http_imsg->received_us());
        // TODO: not static when http_imsg->read_body_progressively() is true
        span->set_response_size(http_imsg->parsed_length());
        span->set_start_parse_us(start_parse_us);
    }

    HttpHeader* header = &cntl->http_response();
    header->Swap(http_imsg->header());
    CHECK(cntl->response_attachment().empty());
    const int saved_error = cntl->ErrorCode();

    do {
        // If header has "Connection: close", close the connection.
        const std::string* conn_cmd = header->GetHeader(common->CONNECTION);
        if (conn_cmd != NULL && 0 == strcasecmp(conn_cmd->c_str(), "close")) {
            // Server asked to close the connection.
            if (http_imsg->read_body_progressively()) {
                // Close the socket when reading completes.
                socket->read_will_be_progressive(CONNECTION_TYPE_SHORT);
            } else {
                socket->SetFailed();
            }
        }

        if (http_imsg->read_body_progressively()) {
            // Set RPA if needed
            accessor.set_readable_progressive_attachment(http_imsg.get());
            const int sc = header->status_code();
            if (sc < 200 || sc >= 300) {
                cntl->SetFailed(EHTTP, "HTTP/%d.%d %d %s",
                                header->major_version(),
                                header->minor_version(),
                                static_cast<int>(header->status_code()),
                                header->reason_phrase());
            } else if (cntl->response() != NULL &&
                       cntl->response()->GetDescriptor()->field_count() != 0) {
                cntl->SetFailed(ERESPONSE, "A protobuf response can't be parsed"
                                " from progressively-read HTTP body");
            }
            break;
        }
        
        // Fail RPC if status code is an error in http sense.
        // The ErrorCode is unified to EHTTP. If a http user sees EHTTP, he/she
        // can check http_request()->status_code() for the exact code.
        const int sc = header->status_code();
        if (sc < 200 || sc >= 300) {
            if (!http_imsg->body().empty()) {
                // Use content as error text if it's present. Notice that
                // content may be binary data, so the size limit is a must.
                // TODO: Print body in better way.
                std::string body_str;
                http_imsg->body().copy_to(
                    &body_str, std::min((int)http_imsg->body().size(),
                                        FLAGS_http_max_error_length));
                cntl->SetFailed(EHTTP, "HTTP/%d.%d %d %s: %.*s",
                                header->major_version(),
                                header->minor_version(),
                                static_cast<int>(header->status_code()),
                                header->reason_phrase(),
                                (int)body_str.size(), body_str.c_str());
            } else {
                cntl->SetFailed(EHTTP, "HTTP/%d.%d %d %s",
                                header->major_version(),
                                header->minor_version(),
                                static_cast<int>(header->status_code()),
                                header->reason_phrase());
            }
            if (cntl->response() == NULL ||
                cntl->response()->GetDescriptor()->field_count() == 0) {
                // A http call. Http users may need the body(containing a html,
                // json etc) even if the http call was failed. This is different
                // from protobuf services where responses are undefined when RPC
                // was failed.
                cntl->response_attachment().swap(http_imsg->body());
            }
            break;
        }
        if (cntl->response() == NULL ||
            cntl->response()->GetDescriptor()->field_count() == 0) {
            // a http call, content is the "real response".
            cntl->response_attachment().swap(http_imsg->body());
            break;
        }
        const HttpContentType content_type = ParseContentType(header->content_type());
        if (content_type != HTTP_CONTENT_PROTO && content_type != HTTP_CONTENT_JSON) {
            cntl->SetFailed(ERESPONSE, "Content-Type must contain %s|%s"
                            " when response is non-NULL, actually it's `%s'",
                            common->CONTENT_TYPE_JSON.c_str(),
                            common->CONTENT_TYPE_PROTO.c_str(),
                            header->content_type().c_str());
            break;
        }
        const std::string* encoding =
            header->GetHeader(common->CONTENT_ENCODING);
        if (encoding != NULL && *encoding == common->GZIP) {
            TRACEPRINTF("Decompressing response=%lu",
                        (unsigned long)http_imsg->body().size());
            base::IOBuf uncompressed;
            if (!policy::GzipDecompress(http_imsg->body(), &uncompressed)) {
                cntl->SetFailed(ERESPONSE, "Fail to un-gzip response body");
                break;
            }
            http_imsg->body().swap(uncompressed);
        }
        // message body is json
        if (content_type == HTTP_CONTENT_PROTO) {
            if (!ParsePbFromIOBuf(cntl->response(), http_imsg->body())) {
                cntl->SetFailed(ERESPONSE, "Fail to parse content");
                break;
            }
        } else {
            base::IOBufAsZeroCopyInputStream wrapper(http_imsg->body());
            std::string err;
            if (!json2pb::JsonToProtoMessage(&wrapper, cntl->response(), &err)) {
                cntl->SetFailed(ERESPONSE, "Fail to parse content, %s", err.c_str());
                break;
            }
        }
    } while (0);
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    http_imsg.reset();
    accessor.OnResponse(cid, saved_error);
}

static void UpdateResponseHeader(int status_code, Controller* cntl) {
    DCHECK(cntl->Failed());
    HttpHeader* resp_header = &cntl->http_response();
    resp_header->set_status_code(status_code);
    cntl->response_attachment().clear();
    cntl->response_attachment().append(cntl->ErrorText());
    cntl->response_attachment().append("\n");
}

void SerializeHttpRequest(base::IOBuf* /*not used*/,
                          Controller* cntl,
                          const google::protobuf::Message* request) {
    if (cntl->connection_type() == CONNECTION_TYPE_SINGLE) {
        cntl->SetFailed(EREQUEST, "http can't work with CONNECTION_TYPE_SINGLE");
        UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
        return;
    }
    if (request != NULL) {
        // If request is not NULL, message body will be serialized json,
        if (!request->IsInitialized()) {
            cntl->SetFailed(
                EREQUEST, "Missing required fields in request: %s",
                request->InitializationErrorString().c_str());
            UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
            return;
        }
        if (!cntl->request_attachment().empty()) {
            cntl->SetFailed(EREQUEST, "request_attachment must be empty "
                            "when request is non-NULL");
            UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
            return;
        }
        base::IOBufAsZeroCopyOutputStream wrapper(&cntl->request_attachment());
        const HttpContentType content_type
                = ParseContentType(cntl->http_request().content_type());
        if (content_type == HTTP_CONTENT_PROTO) {
            // Serialize content as protobuf
            if (!request->SerializeToZeroCopyStream(&wrapper)) {
                cntl->request_attachment().clear();
                cntl->SetFailed(EREQUEST, "Fail to serialize %s",
                                request->GetTypeName().c_str());
                UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
                return;
            }
        } else {
            // Serialize content as json
            std::string err;
            json2pb::Pb2JsonOptions opt;
            opt.enum_option = (FLAGS_pb_enum_as_number ? json2pb::OUTPUT_ENUM_BY_NUMBER
                               : json2pb::OUTPUT_ENUM_BY_NAME);
            if (!json2pb::ProtoMessageToJson(*request, &wrapper, opt, &err)) {
                cntl->request_attachment().clear();
                cntl->SetFailed(EREQUEST, "Fail to convert request to json, %s",
                                err.c_str());
                UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
                return;
            }
            // Set content-type if user did not.
            if (cntl->http_request().content_type().empty()) {
                cntl->http_request().set_content_type(common->CONTENT_TYPE_JSON);
            }
        }
    } else {
        // Use request_attachment.
        // TODO: Checking required fields of http header.
    }
    // Make RPC fail if uri() is not OK (previous SetHttpURL/operator= failed)
    if (!cntl->http_request().uri().status().ok()) {
        cntl->SetFailed(EREQUEST, "%s",
                        cntl->http_request().uri().status().error_cstr());
        UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
        return;
    }
    if (cntl->request_compress_type() != COMPRESS_TYPE_NONE) {
        if (cntl->request_compress_type() != COMPRESS_TYPE_GZIP) {
            cntl->SetFailed(EREQUEST, "http does not support %s",
                            CompressTypeToCStr(cntl->request_compress_type()));
            UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
            return;
        }
        const size_t request_size = cntl->request_attachment().size();
        if (request_size >= (size_t)FLAGS_http_body_compress_threshold) {
            TRACEPRINTF("Compressing request=%lu", (unsigned long)request_size);
            base::IOBuf compressed;
            if (GzipCompress(cntl->request_attachment(), &compressed, NULL)) {
                cntl->request_attachment().swap(compressed);
                cntl->http_request().SetHeader(common->CONTENT_ENCODING, common->GZIP);
            } else {
                cntl->SetFailed("Fail to gzip the request body, skip compressing");
            }
        }
    }

    HttpHeader* header = &cntl->http_request();
    ControllerPrivateAccessor accessor(cntl);

    // Fill log-id if user set it.
    if (cntl->has_log_id()) {
        header->SetHeader(common->LOG_ID,
                          base::string_printf(
                              "%llu", (unsigned long long)cntl->log_id()));
    }

    // HTTP before 1.1 needs to set keep-alive explicitly.
    if (header->before_http_1_1() &&
        cntl->connection_type() != CONNECTION_TYPE_SHORT &&
        header->GetHeader(common->CONNECTION) == NULL) {
        header->SetHeader(common->CONNECTION, common->KEEP_ALIVE);
    }

    // Set url to /ServiceName/MethodName when we're about to call protobuf
    // services (indicated by non-NULL method).
    const google::protobuf::MethodDescriptor* method = cntl->method();
    if (method != NULL) {
        header->set_method(HTTP_METHOD_POST);
        std::string path;
        path.reserve(2 + method->service()->full_name().size()
                     + method->name().size());
        path.push_back('/');
        path.append(method->service()->full_name());
        path.push_back('/');
        path.append(method->name());
        header->uri().set_path(path);
    }
    Span* span = accessor.span();
    if (span) {
        header->SetHeader("x-bd-trace-id", base::string_printf(
                              "%llu", (unsigned long long)span->trace_id()));
        header->SetHeader("x-bd-span-id", base::string_printf(
                              "%llu", (unsigned long long)span->span_id()));
        header->SetHeader("x-bd-parent-span-id", base::string_printf(
                              "%llu", (unsigned long long)span->parent_span_id()));
    }
}

void PackHttpRequest(base::IOBuf* buf,
                     SocketMessage**,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor*,
                     Controller* cntl,
                     const base::IOBuf& /*unused*/,
                     const Authenticator* auth) {
    ControllerPrivateAccessor accessor(cntl);
    HttpHeader* header = &cntl->http_request();
    if (auth != NULL && header->GetHeader(common->AUTHORIZATION) == NULL) {
        std::string auth_data;
        if (auth->GenerateCredential(&auth_data) != 0) {
            cntl->SetFailed(EREQUEST, "Fail to GenerateCredential");
            UpdateResponseHeader(HTTP_STATUS_BAD_REQUEST, cntl);
            return;
        }
        header->SetHeader(common->AUTHORIZATION, auth_data);
    }

    // Store `correlation_id' into Socket since http server
    // may not echo back this field. But we send it anyway.
    accessor.set_socket_correlation_id(correlation_id);

    HttpMessageSerializer(header)
        .set_content(&cntl->request_attachment())
        .set_remote_side(cntl->remote_side())
        .SerializeAsRequest(buf);
    if (FLAGS_http_verbose) {
        PrintMessage(*buf, true, true);
    }
}

inline bool SupportGzip(Controller* cntl) {
    const std::string* encodings =
        cntl->http_request().GetHeader(common->ACCEPT_ENCODING);
    if (encodings == NULL) {
        return false;
    }
    return encodings->find(common->GZIP) != std::string::npos;
}

inline int ErrorCode2StatusCode(int error_code) {
    switch (error_code) {
    case ENOSERVICE:
    case ENOMETHOD:
        return HTTP_STATUS_NOT_FOUND;
    case EAUTH:
        return HTTP_STATUS_UNAUTHORIZED;
    case EREQUEST:
    case EINVAL:
        return HTTP_STATUS_BAD_REQUEST;
    case ELIMIT:
    case ELOGOFF:
        return HTTP_STATUS_SERVICE_UNAVAILABLE;
    case EPERM:
        return HTTP_STATUS_FORBIDDEN;
    case ERPCTIMEDOUT:
    case ETIMEDOUT:
        return HTTP_STATUS_GATEWAY_TIMEOUT;
    default:
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
}

static void SendHttpResponse(Controller *cntl,
                             const google::protobuf::Message *req,
                             const google::protobuf::Message *res,
                             const Server* server,
                             MethodStatus* method_status_raw,
                             long start_parse_us) {
    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(base::cpuwide_time_us());
    }
    ScopedMethodStatus method_status(method_status_raw);
    std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);
    std::unique_ptr<const google::protobuf::Message> recycle_req(req);
    std::unique_ptr<const google::protobuf::Message> recycle_res(res);
    Socket* socket = accessor.get_sending_sock();
    ScopedRemoveConcurrency remove_concurrency_dummy(server, cntl);
    
    if (cntl->IsCloseConnection()) {
        socket->SetFailed();
        return;
    }
    
    HttpHeader* res_header = &cntl->http_response();
    res_header->set_version(cntl->http_request().major_version(),
                        cntl->http_request().minor_version());

    // Convert response to json if needed.
    // Notice: we don't check res->IsInitialized() which should be checked
    // by the conversion function.
    if (res != NULL &&
        res->GetDescriptor()->field_count() > 0 &&
        // ^ a pb service must have fields in response.
        !cntl->Failed()) {
        // ^ pb response in failed RPC is undefined, no need to convert.

        if (!cntl->response_attachment().empty()) {
            if (res->ByteSize() != 0) { // fields in `res' were set.
                LOG(ERROR) << "Service on " << cntl->http_request().uri().path()
                           << " sets both response_attachment and response(pb)"
                    ", you can set only one of them.";
            } // else no fields in `res' were set, user is intended to fill
            // the http body by him/herself.
        } else {
            base::IOBufAsZeroCopyOutputStream wrapper(&cntl->response_attachment());
            const std::string* content_type_str = &res_header->content_type();
            if (content_type_str->empty()) {
                content_type_str = &cntl->http_request().content_type();
            }
            const HttpContentType content_type = ParseContentType(*content_type_str);
            if (content_type == HTTP_CONTENT_PROTO) {
                if (res->SerializeToZeroCopyStream(&wrapper)) {
                    // Set content-type if user did not
                    if (res_header->content_type().empty()) {
                        res_header->set_content_type(common->CONTENT_TYPE_PROTO);
                    }
                } else {
                    cntl->SetFailed(ERESPONSE, "Fail to serialize %s",
                                    res->GetTypeName().c_str());
                }
            } else {
                std::string err;
                json2pb::Pb2JsonOptions opt;
                opt.enum_option = (FLAGS_pb_enum_as_number ? json2pb::OUTPUT_ENUM_BY_NUMBER
                                   : json2pb::OUTPUT_ENUM_BY_NAME);
                if (json2pb::ProtoMessageToJson(*res, &wrapper, opt, &err)) {
                    // Set content-type if user did not
                    if (res_header->content_type().empty()) {
                        res_header->set_content_type(common->CONTENT_TYPE_JSON);
                    }
                } else {
                    cntl->SetFailed(ERESPONSE, "Fail to convert response to json, %s",
                                    err.c_str());
                }
            }
        }
    }

    // In HTTP 0.9, the server always closes the connection after sending the
    // response. The client must close its end of the connection after
    // receiving the response.
    // In HTTP 1.0, the server always closes the connection after sending the
    // response UNLESS the client sent a Connection: keep-alive request header
    // and the server sent a Connection: keep-alive response header. If no
    // such response header exists, the client must close its end of the
    // connection after receiving the response.
    // In HTTP 1.1, the server does not close the connection after sending
    // the response UNLESS the client sent a Connection: close request header,
    // or the server sent a Connection: close response header. If such a
    // response header exists, the client must close its end of the connection
    // after receiving the response.
    const std::string* res_conn = res_header->GetHeader(common->CONNECTION);
    if (res_conn == NULL || strcasecmp(res_conn->c_str(), "close") != 0) {
        const std::string* req_conn =
            cntl->http_request().GetHeader(common->CONNECTION);
        if (cntl->http_request().before_http_1_1()) {
            if (req_conn != NULL &&
                strcasecmp(req_conn->c_str(), "keep-alive") == 0) {
                res_header->SetHeader(common->CONNECTION, common->KEEP_ALIVE);
            }
        } else {
            if (req_conn != NULL &&
                strcasecmp(req_conn->c_str(), "close") == 0) {
                res_header->SetHeader(common->CONNECTION, common->CLOSE);
            }
        }
    } // else user explicitly set Connection:close, clients of HTTP 1.1/1.0/0.9
    // should all close the connection.

    HttpMessageSerializer serializer(res_header);
    if (cntl->Failed()) {
        // Set status-code with default value(converted from error code)
        // if user did not set it.
        if (res_header->status_code() == HTTP_STATUS_OK) {
            res_header->set_status_code(ErrorCode2StatusCode(cntl->ErrorCode()));
        }
        // Fill ErrorCode into header
        res_header->SetHeader(common->ERROR_CODE,
                          base::string_printf("%d", cntl->ErrorCode()));

        // user may compress the output, remove the header, otherwise web
        // browser cannot parse the response.
        res_header->RemoveHeader(common->CONTENT_ENCODING);
        
        // Fill body with ErrorText.
        res_header->set_content_type(common->CONTENT_TYPE_TEXT);
        cntl->response_attachment().clear();
        cntl->response_attachment().append(cntl->ErrorText());
        cntl->response_attachment().append("\n");
        serializer.set_content(&cntl->response_attachment());
    } else if (cntl->has_progressive_writer()) {
        // Transfer-Encoding is supported since HTTP/1.1
        if (!res_header->before_http_1_1()) {
            res_header->SetHeader("Transfer-Encoding", "chunked");
        }
        if (!cntl->response_attachment().empty()) {
            LOG(ERROR) << "response_attachment(size="
                       << cntl->response_attachment().size() << ") will be"
                " ignored when CreateProgressiveAttachment() was called";
        }
        // not set_content to enable chunked mode.
    } else {
        if (cntl->response_compress_type() == COMPRESS_TYPE_GZIP) {
            const size_t response_size = cntl->response_attachment().size();
            if (response_size >= (size_t)FLAGS_http_body_compress_threshold
                && SupportGzip(cntl)) {
                TRACEPRINTF("Compressing response=%lu", (unsigned long)response_size);
                base::IOBuf tmpbuf;
                if (GzipCompress(cntl->response_attachment(), &tmpbuf, NULL)) {
                    cntl->response_attachment().swap(tmpbuf);
                    res_header->SetHeader(common->CONTENT_ENCODING, common->GZIP);
                } else {
                    LOG(ERROR) << "Fail to gzip the http response, skip compression.";
                }
            }
        } else {
            LOG_IF(ERROR, cntl->response_compress_type() != COMPRESS_TYPE_NONE)
                << "Unknown compress_type=" << cntl->response_compress_type()
                << ", skip compression.";
        }
        serializer.set_content(&cntl->response_attachment());
    }
    base::IOBuf res_buf;
    serializer.SerializeAsResponse(&res_buf);
    if (FLAGS_http_verbose) {
        PrintMessage(res_buf, false, serializer.has_content());
    }

    if (span) {
        span->set_response_size(res_buf.size());
    }
    cntl->response_attachment().clear();

    // Have the risk of unlimited pending responses, in which case, tell
    // users to set max_concurrency.
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (socket->Write(&res_buf, &wopt) != 0) {
        // EPIPE is common in pooled connections + backup requests.
        const int errcode = errno;
        PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *socket;
        cntl->SetFailed(errcode, "Fail to write into %s",
                        socket->description().c_str());
        return;
    }
    if (span) {
        // TODO: this is not sent
        span->set_sent_us(base::cpuwide_time_us());
    }
    if (method_status) {
        method_status.release()->OnResponded(
            !cntl->Failed(), base::cpuwide_time_us() - start_parse_us);
    }
}

static void SendHttpResponse(Controller *cntl, const Server* svr,
                             MethodStatus* method_status) {
    SendHttpResponse(cntl, NULL, NULL, svr, method_status, -1);
}


// Normalize the sub string of `uri_path' covered by `splitter' and
// put it into `unresolved_path'
inline void FillUnresolvedPath(std::string* unresolved_path,
                               const std::string& uri_path,
                               base::StringSplitter& splitter) {
    if (unresolved_path == NULL) {
        return;
    }
    if (splitter) {
        // Normalize unresolve_path.
        const size_t path_len =
            uri_path.c_str() + uri_path.size() - splitter.field();
        unresolved_path->reserve(path_len);
        unresolved_path->clear();
        for (base::StringSplitter slash_sp(
                 splitter.field(), splitter.field() + path_len, '/');
             slash_sp != NULL; ++slash_sp) {
            if (!unresolved_path->empty()) {
                unresolved_path->push_back('/');
            }
            unresolved_path->append(slash_sp.field(), slash_sp.length());
        }
    } else {
        unresolved_path->clear();
    }
}

inline const Server::MethodProperty*
FindMethodPropertyByURIImpl(const std::string& uri_path, const Server* server,
                            std::string* unresolved_path) {
    ServerPrivateAccessor wrapper(server);
    base::StringSplitter splitter(uri_path.c_str(), '/');
    // Show index page for empty URI
    if (NULL == splitter) {
        return wrapper.FindMethodPropertyByFullName(
            IndexService::descriptor()->full_name(), common->DEFAULT_METHOD);
    }
    base::StringPiece service_name(splitter.field(), splitter.length());
    const bool full_service_name =
        (service_name.find('.') != base::StringPiece::npos);
    const Server::ServiceProperty* const sp = 
        (full_service_name ?
         wrapper.FindServicePropertyByFullName(service_name) :
         wrapper.FindServicePropertyByName(service_name));
    if (NULL == sp) {
        // normal for urls matching _global_restful_map
        return NULL;
    }
    // Find restful methods by uri.
    if (sp->restful_map) {
        ++splitter;
        base::StringPiece left_path;
        if (splitter) {
            // The -1 is for including /, always safe because of ++splitter
            left_path.set(splitter.field() - 1, uri_path.c_str() +
                          uri_path.size() - splitter.field() + 1);
        }
        return sp->restful_map->FindMethodProperty(left_path, unresolved_path);
    }
    if (!full_service_name) {
        // Change to service's fullname.
        service_name = sp->service->GetDescriptor()->full_name();
    }

    // Regard URI as [service_name]/[method_name]
    const Server::MethodProperty* mp = NULL;
    base::StringPiece method_name;
    if (++splitter != NULL) {
        method_name.set(splitter.field(), splitter.length());
        // Copy splitter rather than modifying it directly since it's used
        // in later branches.
        mp = wrapper.FindMethodPropertyByFullName(service_name, method_name);
        if (mp) {
            ++splitter; // skip method name
            FillUnresolvedPath(unresolved_path, uri_path, splitter);
            return mp;
        }
    }
    
    // Try [service_name]/default_method
    mp = wrapper.FindMethodPropertyByFullName(service_name, common->DEFAULT_METHOD);
    if (mp) {
        FillUnresolvedPath(unresolved_path, uri_path, splitter);
        return mp;
    }

    // Call BadMethodService::no_method for service_name-only URL.
    if (method_name.empty()) {
        return wrapper.FindMethodPropertyByFullName(
            BadMethodService::descriptor()->full_name(), common->NO_METHOD);
    }

    // Called an existing service w/o default_method with an unknown method.
    return NULL;
}

// Used in UT, don't be static
const Server::MethodProperty*
FindMethodPropertyByURI(const std::string& uri_path, const Server* server,
                        std::string* unresolved_path) {
    const Server::MethodProperty* mp =
        FindMethodPropertyByURIImpl(uri_path, server, unresolved_path);
    if (mp != NULL) {
        if (mp->http_url != NULL) {
            // the restful method is accessed from its
            // default url (SERVICE/METHOD) which should be rejected.
            return NULL;
        }
        return mp;
    }
    // uri_path cannot match any methods with exact service_name. Match
    // the fuzzy patterns in global restful map which often matches
    // extension names. Say "*.txt => get_text_file, *.mp4 => download_mp4".
    ServerPrivateAccessor accessor(server);
    if (accessor.global_restful_map()) {
        return accessor.global_restful_map()->FindMethodProperty(
            uri_path, unresolved_path);
    }
    return NULL;
}

ParseResult ParseHttpMessage(base::IOBuf *source, Socket *socket, bool read_eof,
                             const void* /*arg*/) {
    HttpInputMessage* http_imsg = 
        static_cast<HttpInputMessage*>(socket->parsing_context());
    if (http_imsg == NULL) {
        if (read_eof || source->empty()) {
            // 1. read_eof: Read EOF after intact HTTP messages, a common case.
            //    Notice that errors except NOT_ENOUGH_DATA can't be returned
            //    otherwise the Socket will be SetFailed() and messages just
            //    in ProcessHttpXXX() may be dropped.
            // 2. source->empty(): also common, InputMessage tries parse
            //    handlers until error is met. If a message was consumed,
            //    source is likely to be empty.
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        http_imsg = new (std::nothrow) HttpInputMessage(
            socket->is_read_progressive());
        if (http_imsg == NULL) {
            LOG(FATAL) << "Fail to new HttpInputMessage";
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        // Parsing http is costly, parsing an incomplete http message from the
        // beginning repeatedly should be avoided, otherwise the cost may reach
        // O(n^2) in the worst case. Save incomplete http messages in sockets
        // to prevent re-parsing. The message will be released when it is
        // completed or destroyed along with the socket.
        socket->reset_parsing_context(http_imsg);
    }
    ssize_t rc = 0;
    if (read_eof) {
        // Send EOF to HttpInputMessage, check comments in http_message.h
        rc = http_imsg->ParseFromArray(NULL, 0);
    } else {
        // Empty `source' is sliently ignored and 0 is returned, check
        // comments in http_message.h
        rc = http_imsg->ParseFromIOBuf(*source);
    }
    if (http_imsg->is_stage2()) {
        // The header part is already parsed as an intact HTTP message
        // to the ProcessHttpXXX. Here parses the body part.
        if (rc >= 0) {
            source->pop_front(rc);
            if (http_imsg->Completed()) {
                // Already returned the message before, don't return again.
                CHECK_EQ(http_imsg, socket->release_parsing_context());
                // NOTE: calling http_imsg->Destroy() is wrong which can only
                // be called from ProcessHttpXXX
                http_imsg->RemoveOneRefForStage2();
                socket->OnProgressiveReadCompleted();
                return MakeMessage(NULL);
            } else {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
        } else {
            // Fail to parse the body. Since headers were parsed successfully,
            // the message is assumed to be HTTP, stop trying other protocols.
            const char* err = http_errno_description(
                HTTP_PARSER_ERRNO(&http_imsg->parser()));
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG, err);
        }
    } else if (rc >= 0) {
        // Normal or stage1 of progressive-read http message.
        source->pop_front(rc);
        if (http_imsg->Completed()) {
            socket->release_parsing_context();
            ParseResult result = MakeMessage(http_imsg);
            if (socket->is_read_progressive()) {
                socket->OnProgressiveReadCompleted();
            }
            return result;
        } else if (socket->is_read_progressive() &&
                   http_imsg->stage() >= HTTP_ON_HEADERS_COMPLELE) {
            // header part of a progressively-read http message is complete,
            // go on to ProcessHttpXXX w/o waiting for full body.
            http_imsg->AddOneRefForStage2(); // released when body is fully read
            return MakeMessage(http_imsg);
        } else {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
    } else if (!socket->CreatedByConnect()) {
        // Note: If the parser fails at query-string/fragment/the-following
        // -"HTTP/x.y", the message is very likely to be in http format (not
        // other protocols registered after http). We send 400 back to client
        // which is more informational than just closing the connection (may
        // cause OP's alarms if the remote side is baidu's nginx). To do this,
        // We make InputMessenger do nothing by cheating it with
        // PARSE_ERROR_NOT_ENOUGH_DATA and remove the addtitional ref of the
        // socket so that it will be recycled when the response is written.
        // We can't use SetFailed which interrupts the writing.
        // Tricky: Socket::ReleaseAdditionalReference() does not remove the
        // internal fd from epoll thus we can still get EPOLLIN and read
        // in more data. If the second read happens, parsing_context()
        // should return the same InputMessage that we see now because we
        // don't reset_parsing_context(NULL) in this branch, and following
        // ParseFromXXX should return -1 immediately because of the non-zero
        // parser.http_errno, and ReleaseAdditionalReference() here should
        // return -1 to prevent us from sending another 400.
        if (is_failed_after_queries(&http_imsg->parser())) {
            int rc = socket->ReleaseAdditionalReference();
            if (rc < 0) {
                // Already released, leave the socket to be recycled
                // by itself.
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            } else if (rc > 0) {
                LOG(ERROR) << "Impossible: Recycled!";
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            // Send 400 back.
            base::IOBuf bad_req;
            HttpHeader header;
            header.set_status_code(HTTP_STATUS_BAD_REQUEST);
            HttpMessageSerializer(&header)
                .set_remote_side(socket->remote_side())
                .SerializeAsResponse(&bad_req);
            Socket::WriteOptions wopt;
            wopt.ignore_eovercrowded = true;
            CHECK_EQ(0, socket->Write(&bad_req, &wopt));
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        } else {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (is_failed_after_http_version(&http_imsg->parser())) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                                  "invalid http response");
        }
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
}

bool VerifyHttpRequest(const InputMessageBase* msg) {
    Server* server = (Server*)msg->arg();
    Socket* socket = msg->socket();
    
    HttpInputMessage* http_request = (HttpInputMessage*)msg;
    const Authenticator* auth = server->options().auth;
    if (NULL == auth) {
        // Fast pass
        return true;
    }
    const Server::MethodProperty* mp = FindMethodPropertyByURI(
        http_request->header().uri().path(), server, NULL);
    if (mp != NULL &&
        mp->is_builtin_service &&
        mp->service->GetDescriptor() != BadMethodService::descriptor()) {
        // BuiltinService doesn't need authentication
        // TODO: Fix backdoor that sends BuiltinService at first
        // and then sends other requests without authentication
        return true;
    }

    const std::string *authorization 
        = http_request->header().GetHeader("Authorization");
    if (authorization == NULL) {
        return false;
    }
    base::EndPoint user_addr;
    if (!GetUserAddressFromHeader(http_request->header(), &user_addr)) {
        user_addr = socket->remote_side();
    }
    return auth->VerifyCredential(*authorization, user_addr,
                                  socket->mutable_auth_context()) == 0;
}

// Defined in baidu_rpc_protocol.cpp
void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done);

void ProcessHttpRequest(InputMessageBase *msg) {
    const int64_t start_parse_us = base::cpuwide_time_us();
    DestroyingPtr<HttpInputMessage> http_imsg(static_cast<HttpInputMessage*>(msg));
    SocketUniquePtr socket_guard(http_imsg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg->arg());
    ScopedNonServiceError non_service_error(server);

    std::unique_ptr<Controller> cntl(new (std::nothrow) Controller);
    if (NULL == cntl.get()) {
        LOG(FATAL) << "Fail to new Controller";
        return;
    }
    base::EndPoint user_addr;
    if (!GetUserAddressFromHeader(http_imsg->header(), &user_addr)) {
        user_addr = socket->remote_side();
    }
    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl.get());
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(user_addr)
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_HTTP)
        .move_in_server_receiving_sock(socket_guard);

    
    // Read log-id. errno may be set when input to strtoull overflows.
    // atoi/atol/atoll don't support 64-bit integer and can't be used.
    const std::string* log_id_str =
        http_imsg->header().GetHeader(common->LOG_ID);
    if (log_id_str) {
        char* logid_end = NULL;
        errno = 0;
        uint64_t logid = strtoull(log_id_str->c_str(), &logid_end, 10);
        if (*logid_end || errno) {
            LOG(ERROR) << "Invalid " << common->LOG_ID << '=' 
                       << *log_id_str << " in http request";
        } else {
            cntl->set_log_id(logid);
        }
    }

    // Tag the bthread with this server's key for
    // thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    const std::string& path = http_imsg->header().uri().path();
    if (IsTraceable(false)) {
        uint64_t trace_id = 0;
        const std::string* trace_id_str =
            http_imsg->header().GetHeader("x-bd-trace-id");
        if (trace_id_str) {
            trace_id = strtoull(trace_id_str->c_str(), NULL, 10);
        }
        uint64_t span_id = 0;
        const std::string* span_id_str =
            http_imsg->header().GetHeader("x-bd-span-id");
        if (span_id_str) {
            span_id = strtoull(span_id_str->c_str(), NULL, 10);
        }
        uint64_t parent_span_id = 0;
        const std::string* parent_span_id_str =
            http_imsg->header().GetHeader("x-bd-parent-span-id");
        if (parent_span_id_str) {
            parent_span_id = strtoull(parent_span_id_str->c_str(), NULL, 10);
        }
        span = Span::CreateServerSpan(
            path, trace_id, span_id, parent_span_id, msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(cntl->log_id());
        span->set_remote_side(user_addr);
        span->set_protocol(PROTOCOL_HTTP);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(http_imsg->parsed_length());
    }
    
    if (!server->IsRunning()) {
        cntl->SetFailed(ELOGOFF, "Server is stopping");
        cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
        cntl->http_response().SetHeader(common->CONNECTION, common->CLOSE);
        return SendHttpResponse(cntl.release(), server, NULL);
    }

    if (server->options().http_master_service) {
        // If http_master_service is on, just call it.
        google::protobuf::Service* svc = server->options().http_master_service;
        const google::protobuf::MethodDescriptor* md =
            svc->GetDescriptor()->FindMethodByName(common->DEFAULT_METHOD);
        if (md == NULL) {
            cntl->SetFailed(ENOMETHOD, "No default_method in http_master_service");
            return SendHttpResponse(cntl.release(), server, NULL);
        }
        accessor.set_method(md);
        cntl->http_request().Swap(http_imsg->header());
        cntl->request_attachment().swap(http_imsg->body());
        google::protobuf::Closure* done = brpc::NewCallback<
            Controller*, const google::protobuf::Message*,
            const google::protobuf::Message*, const Server*,
            MethodStatus *, long>(
                &SendHttpResponse, cntl.get(), NULL, NULL, server,
                NULL, start_parse_us);
        if (span) {
            span->ResetServerSpanName(md->full_name());
            span->set_start_callback_us(base::cpuwide_time_us());
            span->AsParent();
        }
        // `cntl', `req' and `res' will be deleted inside `done'
        return svc->CallMethod(md, cntl.release(), NULL, NULL, done);
    }
    
    std::string unresolved_path;
    const Server::MethodProperty* sp =
        FindMethodPropertyByURI(path, server, &unresolved_path);
    if (NULL == sp) {
        if (security_mode) {
            std::string escape_path;
            WebEscape(http_imsg->header().uri().path(), &escape_path);
            cntl->SetFailed(ENOMETHOD, "Fail to find method on `%s'", escape_path.c_str());
        } else {
            cntl->SetFailed(ENOMETHOD, "Fail to find method on `%s'",
                            http_imsg->header().uri().path().c_str());
        }
        cntl->http_response().set_status_code(HTTP_STATUS_NOT_FOUND);
        return SendHttpResponse(cntl.release(), server, NULL);
    } else if (sp->service->GetDescriptor() == BadMethodService::descriptor()) {
        BadMethodRequest breq;
        BadMethodResponse bres;
        base::StringSplitter split(http_imsg->header().uri().path().c_str(), '/');
        breq.set_service_name(std::string(split.field(), split.length()));
        sp->service->CallMethod(sp->method, cntl.get(), &breq, &bres, NULL);
        return SendHttpResponse(cntl.release(), server, NULL);
    }
    // Switch to service-specific error.
    non_service_error.release();
    MethodStatus* method_status = sp->status;
    if (method_status) {
        if (!method_status->OnRequested()) {
            cntl->SetFailed(ELIMIT, "Reached %s's max_concurrency=%d",
                            sp->method->full_name().c_str(),
                            method_status->max_concurrency());
            return SendHttpResponse(cntl.release(), server, method_status);
        }
    }
    
    http_imsg->header().set_unresolved_path(unresolved_path);
    if (span) {
        span->ResetServerSpanName(sp->method->full_name());
    }
    // NOTE: accesses to builtin services are not counted as part of
    // concurrency, therefore are not limited by ServerOptions.max_concurrency.
    if (!sp->is_builtin_service && !sp->is_tabbed) {
        if (!server_accessor.AddConcurrency(cntl.get())) {
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                            server->options().max_concurrency);
            return SendHttpResponse(cntl.release(), server, method_status);
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                            " -usercode_in_pthread is on");
            return SendHttpResponse(cntl.release(), server, method_status);
        }
    } else if (security_mode) {
        cntl->SetFailed(EPERM, "Not allowed to access builtin services, try "
                        "ServerOptions.internal_port=%d instead if you're inside"
                        " Baidu's network", server->options().internal_port);
        cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
        return SendHttpResponse(cntl.release(), server, method_status);
    }
    
    google::protobuf::Service* svc = sp->service;
    const google::protobuf::MethodDescriptor* method = sp->method;
    accessor.set_method(method);
    std::unique_ptr<google::protobuf::Message> req(
        svc->GetRequestPrototype(method).New());
    std::unique_ptr<google::protobuf::Message> res(
        svc->GetResponsePrototype(method).New());
    if (__builtin_expect(!req || !res, 0)) {
        PLOG(FATAL) << "Fail to new req or res";
        cntl->SetFailed("Fail to new req or res");
        return SendHttpResponse(cntl.release(), server, method_status);
    }
    if (method->input_type()->field_count() > 0) {
        // A protobuf service. No matter if Content-type is set to
        // applcation/json or body is empty, we have to treat body as a json
        // and try to convert it to pb, which guarantees that a protobuf
        // service is always accessed with valid requests.
        if (http_imsg->body().empty()) {
            // Treat empty body specially since parsing it results in error
            if (!req->IsInitialized()) {
                cntl->SetFailed(EREQUEST, "%s needs to be created from a"
                                " non-empty json, it has required fields.",
                                req->GetDescriptor()->full_name().c_str());
                cntl->http_response().set_status_code(HTTP_STATUS_BAD_REQUEST);
                return SendHttpResponse(cntl.release(), server, method_status);
            } // else all fields of the request are optional.
        } else {
            const std::string* encoding =
                http_imsg->header().GetHeader(common->CONTENT_ENCODING);
            if (encoding != NULL && *encoding == common->GZIP) {
                TRACEPRINTF("Decompressing request=%lu",
                            (unsigned long)http_imsg->body().size());
                base::IOBuf uncompressed;
                if (!policy::GzipDecompress(http_imsg->body(), &uncompressed)) {
                    cntl->SetFailed(EREQUEST, "Fail to un-gzip request body");
                    return SendHttpResponse(cntl.release(), server, method_status);
                }
                http_imsg->body().swap(uncompressed);
            }
            if (ParseContentType(http_imsg->header().content_type()) == HTTP_CONTENT_PROTO) {
                if (!ParsePbFromIOBuf(req.get(), http_imsg->body())) {
                    cntl->SetFailed(EREQUEST, "Fail to parse http body as %s",
                                    req->GetDescriptor()->full_name().c_str());
                    return SendHttpResponse(cntl.release(), server, method_status);
                }
            } else {
                base::IOBufAsZeroCopyInputStream wrapper(http_imsg->body());
                std::string err;
                if (!json2pb::JsonToProtoMessage(&wrapper, req.get(), &err)) {
                    cntl->SetFailed(EREQUEST, "Fail to parse http body as %s, %s",
                                    req->GetDescriptor()->full_name().c_str(), err.c_str());
                    return SendHttpResponse(cntl.release(), server, method_status);
                }
            }
        }
    } else {
        // A http server, just keep content as it is.
        cntl->request_attachment().swap(http_imsg->body());
    }
    // Always set http header. Even a pb service may need it.
    cntl->http_request().Swap(http_imsg->header());
    
    http_imsg.reset();  // optional, just release resourse ASAP

    google::protobuf::Closure* done = brpc::NewCallback<
        Controller*, const google::protobuf::Message*,
        const google::protobuf::Message*, const Server*,
          MethodStatus *, long>(
            &SendHttpResponse, cntl.get(),
            req.get(), res.get(), server,
            method_status, start_parse_us);
    if (span) {
        span->set_start_callback_us(base::cpuwide_time_us());
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
}

// We don't use URI::SetHttpURL because we want to make Channel::Init fast
// however the structure has many internal fields that we don't need.
// We can't avoid memory allocation here(using string now) because host may
// only be part of the given url and succeeding functions(say str2endpoint)
// do not support a length parameter.
static int ParseHostFromURL(const char* url, std::string* host, int* port) {
    const char* p = url;
    for (; isspace(*p); ++p);
    if (strncmp("http", p, 4) != 0 ||
        (strncmp("://", p + 4, 3) != 0 &&
         strncmp("s://", p + 4, 4) != 0)) {
        host->assign("http://");
        host->append(p);
    } else {
        host->assign(p);
    }
    struct http_parser_url layout;
    // Have to initialize layout because http_parser may use undefined
    // field_data[UF_HOST] when there's no host field in `url'.
    memset(&layout, 0, sizeof(layout));
    const int rc = http_parser_parse_url(
        host->c_str(), host->size(), 0, &layout);
    if (rc) {
        LOG(ERROR) << "Invalid url=`" << url << "'";
        return -1;
    }
    if (!(layout.field_set & (1 << UF_HOST))) {
        LOG(ERROR) << "No host in url=`" << url << "'";
        return -1;
    }
    // We already modified host at the beginning, it's safe to memmove.
    memmove(const_cast<char*>(host->data()),
            host->data() + layout.field_data[UF_HOST].off,
            layout.field_data[UF_HOST].len);
    host->resize(layout.field_data[UF_HOST].len);
    if (layout.field_set & (1 << UF_PORT)) {
        *port = layout.port;
    } else {
        *port = 80;  // default port of http.
    }
    return 0;
}

bool ParseHttpServerAddress(base::EndPoint* point, const char* server_addr_and_port) {
    std::string host;
    int port = -1;
    if (ParseHostFromURL(server_addr_and_port, &host, &port) != 0) {
        LOG(ERROR) << "Fail to ParseHostFromURL";
        return false;
    }
    if (str2endpoint(host.c_str(), port, point) != 0 &&
        hostname2endpoint(host.c_str(), port, point) != 0) {
        LOG(ERROR) << "Invalid address=`" << host << '\'';
        return false;
    }
    return true;
}

const std::string& GetHttpMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller* cntl) {
    return cntl->http_request().uri().path();
}

}  // namespace policy
} // namespace brpc

