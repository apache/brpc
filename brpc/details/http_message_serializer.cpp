// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/19 13:10:04

#include "base/logging.h"                       // DCHECK
#include "base/iobuf.h"                         // base::IOBuf
#include "base/base64.h"
#include "brpc/http_header.h"              // HttpHeader
#include "brpc/http_status_code.h"         // HttpReasonPhrase
#include "brpc/details/http_message_serializer.h"

#define BRPC_CRLF "\r\n"


namespace brpc {

// Request format
// Request       = Request-Line              ; Section 5.1
//                 *(( general-header        ; Section 4.5
//                  | request-header         ; Section 5.3
//                  | entity-header ) CRLF)  ; Section 7.1
//                  CRLF
//                 [ message-body ]          ; Section 4.3
// Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
// Method         = "OPTIONS"                ; Section 9.2
//                | "GET"                    ; Section 9.3
//                | "HEAD"                   ; Section 9.4
//                | "POST"                   ; Section 9.5
//                | "PUT"                    ; Section 9.6
//                | "DELETE"                 ; Section 9.7
//                | "TRACE"                  ; Section 9.8
//                | "CONNECT"                ; Section 9.9
//                | extension-method
// extension-method = token
void HttpMessageSerializer::SerializeAsRequest(base::IOBuf *dest) {
    base::IOBufBuilder os;
    const URI& uri = _http_header->uri();
    os << GetMethodStr(_http_header->method()) << ' ';
    // note: hide host if possible because host is a field in header.
    uri.Print(os, false/*note*/);
    os << " HTTP/" << _http_header->major_version() << '.'
       << _http_header->minor_version() << BRPC_CRLF;
    // Always remove existing "Content-Length" and calculate it by our own.
    _http_header->RemoveHeader("Content-Length");
    if (_http_header->method() != HTTP_METHOD_GET) {
        os << "Content-Length: " << (_content ? _content->length() : 0)
           << BRPC_CRLF;
    }
    //rfc 7230#section-5.4:
    //A client MUST send a Host header field in all HTTP/1.1 request
    //messages. If the authority component is missing or undefined for
    //the target URI, then a client MUST send a Host header field with an
    //empty field-value.
    //rfc 7231#sec4.3:
    //the request-target consists of only the host name and port number of 
    //the tunnel destination, seperated by a colon. For example,
    //Host: server.example.com:80
    if (_http_header->GetHeader("host") == NULL) {
        os << "Host: ";
        if (!uri.host().empty()) {
            os << uri.host();
            if (uri.port() >= 0) {
                os << ':' << uri.port();
            }
        } else if (_remote_side.port != 0) {
            os << _remote_side;
        }
        os << BRPC_CRLF;
    }
    if (!_http_header->content_type().empty()) {
        os << "Content-Type: " << _http_header->content_type()
           << BRPC_CRLF;
    }
    for (HttpHeader::HeaderIterator it = _http_header->HeaderBegin();
         it != _http_header->HeaderEnd(); ++it) {
        os << it->first << ": " << it->second << BRPC_CRLF;
    }
    if (_http_header->GetHeader("Accept") == NULL) {
        os << "Accept: */*" BRPC_CRLF;
    }
    // The fake "curl" user-agent may let servers return plain-text results.
    if (_http_header->GetHeader("User-Agent") == NULL) {
        os << "User-Agent: baidu-rpc/1.0 curl/7.0" BRPC_CRLF;
    }
    const std::string& user_info = _http_header->uri().user_info();
    if (!user_info.empty() && _http_header->GetHeader("Authorization") == NULL) {
        // NOTE: just assume user_info is well formatted, namely
        // "<user_name>:<password>". Users are very unlikely to add extra
        // characters in this part and even if users did, most of them are
        // invalid and rejected by http_parser_parse_url().
        std::string encoded_user_info;
        base::Base64Encode(user_info, &encoded_user_info);
        os << "Authorization: Basic " << encoded_user_info << BRPC_CRLF;
    }
    os << BRPC_CRLF;  // CRLF before content
    os.move_to(*dest);
    if (_http_header->method() != HTTP_METHOD_GET && _content) {
        dest->append(*_content);
    }
}

// Response format
// Response      = Status-Line               ; Section 6.1
//                *(( general-header        ; Section 4.5
//                 | response-header        ; Section 6.2
//                 | entity-header ) CRLF)  ; Section 7.1
//                CRLF
//                [ message-body ]          ; Section 7.2
// 
// Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
void HttpMessageSerializer::SerializeAsResponse(base::IOBuf *dest) {
    base::IOBufBuilder os;
    os << "HTTP/" << _http_header->major_version() << '.'
       << _http_header->minor_version() << ' ' << _http_header->status_code()
       << ' ' << _http_header->reason_phrase() << BRPC_CRLF;
    // Always remove existing "Content-Length" and calculate it by our own.
    _http_header->RemoveHeader("Content-Length");
    if (_content) {
        // Some http server(namely lighttpd) requires "Content-Length" to be set
        // to 0 for empty content. Thus we always set Content-Length.
        os << "Content-Length: " << _content->length() << BRPC_CRLF;
    }
    if (!_http_header->content_type().empty()) {
        os << "Content-Type: " << _http_header->content_type()
           << BRPC_CRLF;
    }
    for (HttpHeader::HeaderIterator it = _http_header->HeaderBegin();
         it != _http_header->HeaderEnd(); ++it) {
        os << it->first << ": " << it->second << BRPC_CRLF;
    }
    os << BRPC_CRLF;  // CRLF before content
    os.move_to(*dest);
    if (_content) {
        dest->append(*_content);
    }
}

} // namespace brpc

