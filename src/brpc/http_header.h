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


#ifndef  BRPC_HTTP_HEADER_H
#define  BRPC_HTTP_HEADER_H

#include <vector>
#include "butil/strings/string_piece.h"  // StringPiece
#include "butil/containers/case_ignored_flat_map.h"
#include "brpc/uri.h"              // URI
#include "brpc/http_method.h"      // HttpMethod
#include "brpc/http_status_code.h"
#include "brpc/http2.h"

// To rpc developers: DON'T put impl. details here, use opaque pointers instead.


namespace brpc {
class InputMessageBase;
namespace policy {
void ProcessHttpRequest(InputMessageBase *msg);
class H2StreamContext;
}

// Non-body part of a HTTP message.
class HttpHeader {
public:
    typedef butil::CaseIgnoredMultiFlatMap<std::string> HeaderMap;
    typedef HeaderMap::const_iterator HeaderIterator;
    typedef HeaderMap::key_equal HeaderKeyEqual;

    HttpHeader();

    // Exchange internal fields with another HttpHeader.
    void Swap(HttpHeader &rhs);

    // Reset internal fields as if they're just default-constructed.
    void Clear();

    // Get http version, 1.1 by default.
    int major_version() const { return _version.first; }
    int minor_version() const { return _version.second; }
    // Change the http version
    void set_version(int http_major, int http_minor)
    { _version = std::make_pair(http_major, http_minor); }

    // True if version of http is earlier than 1.1
    bool before_http_1_1() const
    { return (major_version() * 10000 +  minor_version()) <= 10000; }

    // True if the message is from HTTP2.
    bool is_http2() const { return major_version() == 2; }

    // Get/set "Content-Type".
    // possible values: "text/plain", "application/json" ...
    // NOTE: Equal to `GetHeader("Content-Type")', ·SetHeader("Content-Type")‘ (case-insensitive).
    const std::string& content_type() const { return _content_type; }
    void set_content_type(const std::string& type) { _content_type = type; }
    void set_content_type(const char* type) { _content_type = type; }
    std::string& mutable_content_type() { return _content_type; }
    
    // Get value of a header which is case-insensitive according to:
    //   https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
    // Namely, GetHeader("log-id"), GetHeader("Log-Id"), GetHeader("LOG-ID")
    // point to the same value.
    // Return pointer to the value, NULL on not found.
    // NOTE: If the key is "Content-Type", `GetHeader("Content-Type")'
    // (case-insensitive) is equal to `content_type()'.
    const std::string* GetHeader(const char* key) const;
    const std::string* GetHeader(const std::string& key) const;

    std::vector<const std::string*> GetAllSetCookieHeader() const;

    // Set value of a header.
    // NOTE: If the key is "Content-Type", `SetHeader("Content-Type", ...)'
    // (case-insensitive) is equal to `set_content_type(...)'.
    void SetHeader(const std::string& key, const std::string& value);

    // Remove all headers of key.
    void RemoveHeader(const char* key);
    void RemoveHeader(const std::string& key) { RemoveHeader(key.c_str()); }

    // Append value to a header. If the header already exists, separate
    // old value and new value with comma(,), two-byte delimiter of "; "
    // or into a new header field, according to:
    //
    // https://datatracker.ietf.org/doc/html/rfc2616#section-4.2
    // Multiple message-header fields with the same field-name MAY be
    // present in a message if and only if the entire field-value for that
    // header field is defined as a comma-separated list [i.e., #(values)].
    //
    // https://datatracker.ietf.org/doc/html/rfc9114#section-4.2.1
    // If a decompressed field section contains multiple cookie field lines,
    // these MUST be concatenated into a single byte string using the two-byte
    // delimiter of "; " (ASCII 0x3b, 0x20) before being passed into a context
    // other than HTTP/2 or HTTP/3, such as an HTTP/1.1 connection, or a generic
    // HTTP server application.
    //
    // https://datatracker.ietf.org/doc/html/rfc6265#section-3
    // Origin servers SHOULD NOT fold multiple Set-Cookie header
    // fields into a single header field.
    void AppendHeader(const std::string& key, const butil::StringPiece& value);
    
    // Get header iterators which are invalidated after calling AppendHeader()
    HeaderIterator HeaderBegin() const { return _headers.begin(); }
    HeaderIterator HeaderEnd() const { return _headers.end(); }
    // #headers
    size_t HeaderCount() const { return _headers.size(); }

    // Get the URI object, check src/brpc/uri.h for details.
    const URI& uri() const { return _uri; }
    URI& uri() { return _uri; }

    // Get/set http method.
    HttpMethod method() const { return _method; }
    void set_method(const HttpMethod method) { _method = method; }

    // Get/set status-code and reason-phrase. Notice that the const char*
    // returned by reason_phrase() will be invalidated after next call to
    // set_status_code().
    int status_code() const { return _status_code; }
    const char* reason_phrase() const;
    void set_status_code(int status_code);

    // The URL path removed with matched prefix.
    // NOTE: always normalized and NOT started with /.
    //
    // Accessing HttpService.Echo
    //   [URL]                               [unresolved_path]
    //   "/HttpService/Echo"                 ""
    //   "/HttpService/Echo/Foo"             "Foo"
    //   "/HttpService/Echo/Foo/Bar"         "Foo/Bar"
    //   "/HttpService//Echo///Foo//"        "Foo"
    //
    // Accessing FileService.default_method:
    //   [URL]                               [unresolved_path]
    //   "/FileService"                      ""
    //   "/FileService/123.txt"              "123.txt"
    //   "/FileService/mydir/123.txt"        "mydir/123.txt"
    //   "/FileService//mydir///123.txt//"   "mydir/123.txt"
    const std::string& unresolved_path() const { return _unresolved_path; }

private:
friend class HttpMessage;
friend class HttpMessageSerializer;
friend class policy::H2StreamContext;
friend void policy::ProcessHttpRequest(InputMessageBase *msg);

    static const char* SET_COOKIE;
    static const char* COOKIE;
    static const char* CONTENT_TYPE;

    std::vector<const std::string*> GetMultiLineHeaders(const std::string& key) const;

    std::string& GetOrAddHeader(const std::string& key);

    std::string& AddHeader(const std::string& key);

    bool IsSetCookie(const std::string& key) const {
        return _header_key_equal(key, SET_COOKIE);
    }

    bool IsCookie(const std::string& key) const {
        return _header_key_equal(key, COOKIE);
    }

    bool IsContentType(const std::string& key) const {
        return _header_key_equal(key, CONTENT_TYPE);
    }

    // Return true if the header can be folded in line,
    // otherwise, returns false, i.e., Set-Cookie header.
    // See comments of `AppendHeader'.
    bool CanFoldedInLine(const std::string& key) {
        return !IsSetCookie(key);
    }

    const char* HeaderValueDelimiter(const std::string& key) {
        return IsCookie(key) ? "; " : ",";
    }

    HeaderKeyEqual _header_key_equal;
    HeaderMap _headers;
    URI _uri;
    int _status_code;
    HttpMethod _method;
    std::string _content_type;
    std::string _unresolved_path;
    std::pair<int, int> _version;
    std::string* _first_set_cookie;
};

const HttpHeader& DefaultHttpHeader();

} // namespace brpc


#endif  //BRPC_HTTP_HEADER_H
