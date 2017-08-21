// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/16 18:54:10

#include "brpc/http_status_code.h"     // HTTP_STATUS_*
#include "brpc/http_header.h"


namespace brpc {

HttpHeader::HttpHeader() 
    : _status_code(HTTP_STATUS_OK)
    , _method(HTTP_METHOD_GET)
    , _version(1, 1) {
    // NOTE: don't forget to clear the field in Clear() as well.
}

void HttpHeader::AppendHeader(const std::string& key,
                              const base::StringPiece& value) {
    std::string& slot = GetOrAddHeader(key);
    if (slot.empty()) {
        slot.assign(value.data(), value.size());
    } else {
        slot.reserve(slot.size() + 1 + value.size());
        slot.push_back(',');
        slot.append(value.data(), value.size());
    }
}

void HttpHeader::Swap(HttpHeader &rhs) {
    _headers.swap(rhs._headers);
    _uri.Swap(rhs._uri);
    std::swap(_status_code, rhs._status_code);
    std::swap(_method, rhs._method);
    _content_type.swap(rhs._content_type);
    _unresolved_path.swap(rhs._unresolved_path);
    std::swap(_version, rhs._version);
}

void HttpHeader::Clear() {
    _headers.clear();
    _uri.Clear();
    _status_code = HTTP_STATUS_OK;
    _method = HTTP_METHOD_GET;
    _content_type.clear();
    _unresolved_path.clear();
    _version = std::make_pair(1, 1);
}

const char* HttpHeader::reason_phrase() const {
    return HttpReasonPhrase(_status_code);
}
    
void HttpHeader::set_status_code(int status_code) {
    _status_code = status_code;
}

const HttpHeader& DefaultHttpHeader() {
    static HttpHeader h;
    return h;
}

} // namespace brpc

