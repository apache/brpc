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


#include "brpc/http_status_code.h"     // HTTP_STATUS_*
#include "brpc/http_header.h"


namespace brpc {

const char* HttpHeader::SET_COOKIE = "set-cookie";
const char* HttpHeader::COOKIE = "cookie";
const char* HttpHeader::CONTENT_TYPE = "content-type";

HttpHeader::HttpHeader() 
    : _status_code(HTTP_STATUS_OK)
    , _method(HTTP_METHOD_GET)
    , _version(1, 1) {}

HttpHeader::HttpHeader(const HttpHeader& rhs)
    : _headers(rhs._headers)
    , _uri(rhs._uri)
    , _status_code(rhs._status_code)
    , _method(rhs._method)
    , _content_type(rhs._content_type)
    , _unresolved_path(rhs._unresolved_path)
    , _version(rhs._version) {
    if (rhs._set_cookie_headers) {
        _set_cookie_headers.reset(new SetCookieHeaders(*rhs._set_cookie_headers));
    }
}

HttpHeader& HttpHeader::operator=(const HttpHeader& rhs) {
    if (this != &rhs) {
        HttpHeader tmp(rhs);
        Swap(tmp);
    }
    return *this;
}

void HttpHeader::Swap(HttpHeader &rhs) {
    _headers.swap(rhs._headers);
    _uri.Swap(rhs._uri);
    std::swap(_status_code, rhs._status_code);
    std::swap(_method, rhs._method);
    _content_type.swap(rhs._content_type);
    _unresolved_path.swap(rhs._unresolved_path);
    std::swap(_version, rhs._version);
    _set_cookie_headers.swap(rhs._set_cookie_headers);
}

void HttpHeader::Clear() {
    _headers.clear();
    _uri.Clear();
    _status_code = HTTP_STATUS_OK;
    _method = HTTP_METHOD_GET;
    _content_type.clear();
    _unresolved_path.clear();
    _version = std::make_pair(1, 1);
    _set_cookie_headers.reset();
}

const std::string* HttpHeader::GetHeader(const char* key) const {
    return GetHeader(std::string(key));
}

const std::string* HttpHeader::GetHeader(const std::string& key) const {
    if (IsSetCookie(key)) {
        return HasSetCookieHeaders()
            ? &_set_cookie_headers->front().second : nullptr;
    }
    std::string* val = _headers.seek(key);
    return val;
}

std::vector<const std::string*> HttpHeader::GetAllSetCookieHeader() const {
    std::vector<const std::string*> headers;
    if (_set_cookie_headers) {
        headers.reserve(_set_cookie_headers->size());
        for (const auto& header : *_set_cookie_headers) {
            headers.push_back(&header.second);
        }
    }
    return headers;
}

void HttpHeader::SetHeader(const std::string& key,
                           const std::string& value) {
    if (IsSetCookie(key)) {
        if (!HasSetCookieHeaders()) {
            if (!_set_cookie_headers) {
                _set_cookie_headers.reset(new SetCookieHeaders);
            }
            _set_cookie_headers->push_back(std::make_pair(key, value));
        } else {
            _set_cookie_headers->front().second = value;
        }
        SyncSetCookieHeaders();
        return;
    }
    GetOrAddHeader(key) = value;
}

void HttpHeader::RemoveHeader(const char* key) {
    if (IsContentType(key)) {
        _content_type.clear();
    } else {
        _headers.erase(key);
        if (IsSetCookie(key)) {
            _set_cookie_headers.reset();
        }
    }
}

void HttpHeader::AppendHeader(const std::string& key,
    const butil::StringPiece& value) {
    if (!CanFoldedInLine(key)) {
        // Add a new Set-Cookie header field.
        if (!_set_cookie_headers) {
            _set_cookie_headers.reset(new SetCookieHeaders);
        }
        _set_cookie_headers->push_back(
            std::make_pair(key, std::string(value.data(), value.size())));
        SyncSetCookieHeaders();
    } else {
        std::string& slot = GetOrAddHeader(key);
        if (slot.empty()) {
            slot.assign(value.data(), value.size());
        } else {
            slot.reserve(slot.size() + 1 + value.size());
            slot.append(HeaderValueDelimiter(key));
            slot.append(value.data(), value.size());
        }
    }
}

const char* HttpHeader::reason_phrase() const {
    return HttpReasonPhrase(_status_code);
}
    
void HttpHeader::set_status_code(int status_code) {
    _status_code = status_code;
}

std::string& HttpHeader::GetOrAddHeader(const std::string& key) {
    if (IsContentType(key)) {
        return _content_type;
    }

    std::string* val = _headers.seek(key);
    if (nullptr == val) {
        val = _headers.insert({ key, "" });
    }
    return *val;
}

std::string& HttpHeader::AddHeader(const std::string& key) {
    if (!IsSetCookie(key)) {
        return *_headers.insert({ key, "" });
    }
    if (!_set_cookie_headers) {
        _set_cookie_headers.reset(new SetCookieHeaders);
    }
    _set_cookie_headers->push_back(std::make_pair(key, std::string()));
    SyncSetCookieHeaders();
    return _set_cookie_headers->back().second;
}

void HttpHeader::SyncSetCookieHeaders() {
    _headers.erase(SET_COOKIE);
    if (_set_cookie_headers) {
        for (const auto& header : *_set_cookie_headers) {
            _headers.insert(header);
        }
    }
}

const HttpHeader& DefaultHttpHeader() {
    static HttpHeader h;
    return h;
}

} // namespace brpc
