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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)
//          Ge,Jun (gejun@baidu.com)

#ifndef BRPC_HTTP_MESSAGE_H
#define BRPC_HTTP_MESSAGE_H

#include <string>                      // std::string
#include "butil/macros.h"
#include "butil/iobuf.h"               // butil::IOBuf
#include "butil/scoped_lock.h"         // butil::unique_lock
#include "butil/endpoint.h"
#include "brpc/details/http_parser.h"  // http_parser
#include "brpc/http_header.h"          // HttpHeader
#include "brpc/progressive_reader.h"   // ProgressiveReader


namespace brpc {

enum HttpParserStage {
    HTTP_ON_MESSAGE_BEGIN,
    HTTP_ON_URL,
    HTTP_ON_STATUS,
    HTTP_ON_HEADER_FIELD, 
    HTTP_ON_HEADER_VALUE,
    HTTP_ON_HEADERS_COMPLELE,
    HTTP_ON_BODY,
    HTTP_ON_MESSAGE_COMPLELE
};

class HttpMessage {
public:
    // If read_body_progressively is true, the body will be read progressively
    // by using SetBodyReader().
    HttpMessage(bool read_body_progressively = false);
    ~HttpMessage();

    const butil::IOBuf &body() const { return _body; }
    butil::IOBuf &body() { return _body; }

    // Parse from array, length=0 is treated as EOF.
    // Returns bytes parsed, -1 on failure.
    ssize_t ParseFromArray(const char *data, const size_t length);
    
    // Parse from butil::IOBuf.
    // Emtpy `buf' is sliently ignored, which is different from ParseFromArray.
    // Returns bytes parsed, -1 on failure.
    ssize_t ParseFromIOBuf(const butil::IOBuf &buf);

    bool Completed() const { return _stage == HTTP_ON_MESSAGE_COMPLELE; }
    HttpParserStage stage() const { return _stage; }

    HttpHeader &header() { return _header; }
    const HttpHeader &header() const { return _header; }
    size_t parsed_length() const { return _parsed_length; }
    
    // Http parser callback functions
    static int on_message_begin(http_parser *);
    static int on_url(http_parser *, const char *, const size_t);
    static int on_status(http_parser*, const char *, const size_t);
    static int on_header_field(http_parser *, const char *, const size_t);
    static int on_header_value(http_parser *, const char *, const size_t);
    static int on_headers_complete(http_parser *);
    static int on_body_cb(http_parser*, const char *, const size_t);
    static int on_message_complete_cb(http_parser *);

    const http_parser& parser() const { return _parser; }

    bool read_body_progressively() const { return _read_body_progressively; }

    // Send new parts of the body to the reader. If the body already has some
    // data, feed them to the reader immediately.
    // Any error during the setting will destroy the reader.
    void SetBodyReader(ProgressiveReader* r);

protected:
    int OnBody(const char* data, size_t size);
    int OnMessageComplete();
    size_t _parsed_length;
    
private:
    DISALLOW_COPY_AND_ASSIGN(HttpMessage);
    int UnlockAndFlushToBodyReader(std::unique_lock<butil::Mutex>& locked);

    HttpParserStage _stage;
    std::string _url;
    HttpHeader _header;
    bool _read_body_progressively;
    // For mutual exclusion between on_body and SetBodyReader.
    butil::Mutex _body_mutex;
    // Read body progressively
    ProgressiveReader* _body_reader;
    butil::IOBuf _body;

    // Parser related members
    struct http_parser _parser;
    std::string _cur_header;
    std::string *_cur_value;

protected:
    // Only valid when -http_verbose is on
    butil::IOBufBuilder* _vmsgbuilder;
    size_t _vbodylen;
};

std::ostream& operator<<(std::ostream& os, const http_parser& parser);

// Serialize a http request.
// header: may be modified in some cases
// remote_side: used when "Host" is absent
// content: could be NULL.
void MakeRawHttpRequest(butil::IOBuf* request,
                        HttpHeader* header,
                        const butil::EndPoint& remote_side,
                        const butil::IOBuf* content);

// Serialize a http response.
// header: may be modified in some cases
// content: cleared after usage. could be NULL. 
void MakeRawHttpResponse(butil::IOBuf* response,
                         HttpHeader* header,
                         butil::IOBuf* content);

} // namespace brpc

#endif  // BRPC_HTTP_MESSAGE_H
