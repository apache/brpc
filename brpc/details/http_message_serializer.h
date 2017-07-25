// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/18 18:05:16

#ifndef  BRPC_HTTP_MESSAGE_SERIALIZER_H
#define  BRPC_HTTP_MESSAGE_SERIALIZER_H

#include <stddef.h>                 // NULL
#include <stdint.h>                 // int64_t
#include <string>
#include "base/macros.h"
#include "base/endpoint.h"

namespace base {
class IOBuf;
}  // namespace base


namespace brpc {

class HttpHeader;

class HttpMessageSerializer {
    DISALLOW_COPY_AND_ASSIGN(HttpMessageSerializer);
public:
    // Notice that serializer may remove headers from the mutable `header'.
    explicit HttpMessageSerializer(HttpHeader *header)
        : _http_header(header)
        , _content(NULL)
        , _chunked_mode(false)
        , _remote_side(base::int2ip(0), 0)
    {}

    // If this function is not called, "Content-Length" is not set (to
    // enable chunked mode)
    HttpMessageSerializer &set_content(const base::IOBuf *content) {
        _content = content;
        return *this;
    }
    bool has_content() const { return _content; }
    
    HttpMessageSerializer &set_remote_side(base::EndPoint remote_side) {
        _remote_side = remote_side;
        return *this;
    }
    void SerializeAsRequest(base::IOBuf *dest);
    void SerializeAsResponse(base::IOBuf *dest);
private:
    HttpHeader *_http_header;
    const base::IOBuf *_content;
    bool _chunked_mode;
    base::EndPoint _remote_side;
};

} // namespace brpc


#endif  //BRPC_HTTP_MESSAGE_SERIALIZER_H
