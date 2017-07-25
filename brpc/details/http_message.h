// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date 2014/10/22 15:12:46

#ifndef BRPC_HTTP_MESSAGE_H
#define BRPC_HTTP_MESSAGE_H

#include <string>                           // std::string
#include "base/macros.h"
#include "base/iobuf.h"                     // base::IOBuf
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
    explicit HttpMessage(bool read_body_progressively = false);
    ~HttpMessage();

    // Fields for general headers
    const base::IOBuf &body() const { return _body; }
    base::IOBuf &body() { return _body; }

    // Uri related stuff
    const std::string &url() const { return _url; }

    const std::string &reason_phrase() const  { return _header._reason_phrase; }

    // Parse from array, length=0 is treated as EOF.
    // Returns bytes parsed, -1 on failure.
    ssize_t ParseFromArray(const char *data, const size_t length);
    
    // Parse from base::IOBuf.
    // Emtpy `buf' is sliently ignored, which is different from ParseFromArray.
    // Returns bytes parsed, -1 on failure.
    ssize_t ParseFromIOBuf(const base::IOBuf &buf);

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
    static int on_body(http_parser *, const char *, const size_t);
    static int on_message_complete(http_parser *);

    const http_parser& parser() const { return _parser; }

    bool read_body_progressively() const { return _read_body_progressively; }

    // Send new parts of the body to the reader. If the body already has some
    // data, feed them to the reader immediately.
    // Any error during the setting will destroy the reader.
    void SetBodyReader(ProgressiveReader* r);

private:
    DISALLOW_COPY_AND_ASSIGN(HttpMessage);

    int UnlockAndFlushToBodyReader(std::unique_lock<pthread_mutex_t>& locked);

    size_t _parsed_length;
    bool _read_body_progressively;
    std::string _url;
    HttpHeader _header;
    // For mutual exclusion between on_body and SetBodyReader.
    pthread_mutex_t _body_mutex;
    // Read body progressively
    ProgressiveReader* _body_reader;
    base::IOBuf _body;

    // Parser related members
    struct http_parser _parser;
    std::string _cur_header;
    std::string *_cur_value;
    HttpParserStage _stage;

    // Only valid when -http_verbose is on
    base::IOBufBuilder* _vmsgbuilder;
    size_t _body_length;
};

std::ostream& operator<<(std::ostream& os, const http_parser& parser);

} // namespace brpc


#endif  // BRPC_HTTP_MESSAGE_H
