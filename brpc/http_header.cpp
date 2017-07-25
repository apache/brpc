// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/16 18:54:10

#include "brpc/http_status_code.h"     // HTTP_STATUS_*
#include "brpc/http_header.h"


namespace brpc {

HttpHeader::HttpHeader() 
    : _status_code(HTTP_STATUS_OK)
    , _method(HTTP_METHOD_GET/*FIXME*/)
    , _version(1, 1) {
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

void HttpHeader::Clear() {
    _headers.clear();
    _uri.Clear();
    _status_code = HTTP_STATUS_OK;
    _reason_phrase.clear();
    _content_type.clear();
    _unresolved_path.clear();
    _version = std::make_pair(1, 1);
    _method = HTTP_METHOD_GET;  // FIXME
}

void HttpHeader::Swap(HttpHeader &rhs) {
    _headers.swap(rhs._headers);
    _uri.Swap(rhs._uri);
    std::swap(_status_code, rhs._status_code);
    std::swap(_method, rhs._method);
    _reason_phrase.swap(rhs._reason_phrase);
    _content_type.swap(rhs._content_type);
    _unresolved_path.swap(rhs._unresolved_path);
    std::swap(_version, rhs._version);
}

const char* HttpHeader::reason_phrase() const {
    return !_reason_phrase.empty() ?
        _reason_phrase.c_str() : HttpReasonPhrase(_status_code);
}
    
void HttpHeader::set_status_code(int status_code) {
    _status_code = status_code;
    _reason_phrase.clear();
}
void HttpHeader::set_status_code(int status_code,
                                 const std::string& reason_phrase) {
    _status_code = status_code;
    _reason_phrase = reason_phrase;
}

const HttpHeader& DefaultHttpHeader() {
    static HttpHeader h;
    return h;
}

} // namespace brpc

