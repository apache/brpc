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
//
// Date 2014/10/24 16:44:30

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>

#include "brpc/server.h"
#include "brpc/details/http_message.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "echo.pb.h"

namespace brpc {

DECLARE_bool(allow_chunked_length);
DECLARE_bool(allow_http_1_1_request_without_host);
DECLARE_bool(http_allow_obs_fold);
DECLARE_bool(http_strict_header_token);

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::FLAGS_allow_http_1_1_request_without_host = true;
    return RUN_ALL_TESTS();
}

namespace policy {
DECLARE_bool(http_allow_empty_path_segments);
Server::MethodProperty*
FindMethodPropertyByURI(const std::string& uri_path, const Server* server,
                        std::string* unknown_method_str);
bool ParseHttpServerAddress(butil::EndPoint *point, const char *server_addr_and_port);
}}

namespace {
using brpc::policy::FindMethodPropertyByURI;
using brpc::policy::ParseHttpServerAddress;

TEST(HttpMessageTest, http_method) {
    ASSERT_STREQ("DELETE", brpc::HttpMethod2Str(brpc::HTTP_METHOD_DELETE));
    ASSERT_STREQ("GET", brpc::HttpMethod2Str(brpc::HTTP_METHOD_GET));
    ASSERT_STREQ("POST", brpc::HttpMethod2Str(brpc::HTTP_METHOD_POST));
    ASSERT_STREQ("PUT", brpc::HttpMethod2Str(brpc::HTTP_METHOD_PUT));

    brpc::HttpMethod m;
    ASSERT_TRUE(brpc::Str2HttpMethod("DELETE", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_DELETE, m);
    ASSERT_TRUE(brpc::Str2HttpMethod("GET", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_GET, m);
    ASSERT_TRUE(brpc::Str2HttpMethod("POST", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_POST, m);
    ASSERT_TRUE(brpc::Str2HttpMethod("PUT", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_PUT, m);

    // case-insensitive
    ASSERT_TRUE(brpc::Str2HttpMethod("DeLeTe", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_DELETE, m);
    ASSERT_TRUE(brpc::Str2HttpMethod("get", &m));
    ASSERT_EQ(brpc::HTTP_METHOD_GET, m);

    // non-existed
    ASSERT_FALSE(brpc::Str2HttpMethod("DEL", &m));
    ASSERT_FALSE(brpc::Str2HttpMethod("DELETE ", &m));
    ASSERT_FALSE(brpc::Str2HttpMethod("GOT", &m));
}

TEST(HttpMessageTest, eof) {
    GFLAGS_NAMESPACE::SetCommandLineOption("verbose", "100");
    const char* http_request = 
        "GET /CloudApiControl/HttpServer/telematics/v3/weather?location=%E6%B5%B7%E5%8D%97%E7%9C%81%E7%9B%B4%E8%BE%96%E5%8E%BF%E7%BA%A7%E8%A1%8C%E6%94%BF%E5%8D%95%E4%BD%8D&output=json&ak=0l3FSP6qA0WbOzGRaafbmczS HTTP/1.1\r\n"
        "X-Host: api.map.baidu.com\r\n"
        "X-Forwarded-Proto: http\r\n"
        "Host: api.map.baidu.com\r\n"
        "User-Agent: IME/Android/4.4.2/N80.QHD.LT.X10.V3/N80.QHD.LT.X10.V3.20150812.031915\r\n"
        "Accept: application/json\r\n"
        "Accept-Charset: UTF-8,*;q=0.5\r\n"
        "Accept-Encoding: deflate,sdch\r\n"
        "Accept-Language: zh-CN,en-US;q=0.8,zh;q=0.6\r\n"
        "Bfe-Atk: NORMAL_BROWSER\r\n"
        "Bfe_logid: 8767802212038413243\r\n"
        "Bfeip: 10.26.124.40\r\n"
        "CLIENTIP: 119.29.102.26\r\n"
        "CLIENTPORT: 59863\r\n"
        "Cache-Control: max-age=0\r\n"
        "Content-Type: application/json;charset=utf8\r\n"
        "X-Forwarded-For: 119.29.102.26\r\n"
        "X-Forwarded-Port: 59863\r\n"
        "X-Ime-Imei: 35629601890905\r\n"
        "X_BD_LOGID: 3959476981\r\n"
        "X_BD_LOGID64: 16815814797661447369\r\n"
        "X_BD_PRODUCT: map\r\n"
        "X_BD_SUBSYS: apimap\r\n";
    butil::IOBuf buf;
    buf.append(http_request);
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)buf.size(), http_message.ParseFromIOBuf(buf));
    ASSERT_EQ(2, http_message.ParseFromArray("\r\n", 2));
    ASSERT_TRUE(http_message.Completed());
}


TEST(HttpMessageTest, request_sanity) {
    const char *http_request = 
        "POST /path/file.html?sdfsdf=sdfs&sldf1=sdf HTTP/12.34\r\n"
        "From: someuser@jmarshall.com\r\n"
        "User-Agent: HTTPTool/1.0  \r\n"  // intended ending spaces
        "Content-Type: json\r\n"
        "Content-Length: 19\r\n"
        "Log-ID: 456\r\n"
        "Host: myhost\r\n"
        "Correlation-ID: 123\r\n"
        "Authorization: test\r\n"
        "Accept: */*\r\n"
        "\r\n"
        "Message Body sdfsdf\r\n"
    ;
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)strlen(http_request), 
              http_message.ParseFromArray(http_request, strlen(http_request)));
    const brpc::HttpHeader& header = http_message.header();
    // Check all keys
    ASSERT_EQ("json", header.content_type());
    ASSERT_TRUE(header.GetHeader("HOST"));
    ASSERT_EQ("myhost", *header.GetHeader("host"));
    ASSERT_TRUE(header.GetHeader("CORRELATION-ID"));
    ASSERT_EQ("123", *header.GetHeader("CORRELATION-ID"));
    ASSERT_TRUE(header.GetHeader("User-Agent"));
    ASSERT_EQ("HTTPTool/1.0  ", *header.GetHeader("User-Agent"));
    ASSERT_TRUE(header.GetHeader("Host"));
    ASSERT_EQ("myhost", *header.GetHeader("Host"));
    ASSERT_TRUE(header.GetHeader("Accept"));
    ASSERT_EQ("*/*", *header.GetHeader("Accept"));
    
    ASSERT_EQ(1, header.major_version());
    ASSERT_EQ(34, header.minor_version());
    ASSERT_EQ(brpc::HTTP_METHOD_POST, header.method());
    ASSERT_EQ(brpc::HTTP_STATUS_OK, header.status_code());
    ASSERT_STREQ("OK", header.reason_phrase());

    ASSERT_TRUE(header.GetHeader("log-id"));
    ASSERT_EQ("456", *header.GetHeader("log-id"));
    ASSERT_TRUE(nullptr != header.GetHeader("Authorization"));
    ASSERT_EQ("test", *header.GetHeader("Authorization"));
}

TEST(HttpMessageTest, response_sanity) {
    const char *http_response = 
        "HTTP/12.34 410 GoneBlah\r\n"
        "From: someuser@jmarshall.com\r\n"
        "User-Agent: HTTPTool/1.0  \r\n"  // intended ending spaces
        "Content-Type: json2\r\n"
        "Content-Length: 19\r\n"
        "Log-ID: 456\r\n"
        "Host: myhost\r\n"
        "Correlation-ID: 123\r\n"
        "Authorization: test\r\n"
        "Accept: */*\r\n"
        "\r\n"
        "Message Body sdfsdf\r\n"
    ;
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)strlen(http_response), 
              http_message.ParseFromArray(http_response, strlen(http_response)));
    // Check all keys
    const brpc::HttpHeader& header = http_message.header();
    ASSERT_EQ("json2", header.content_type());
    ASSERT_TRUE(header.GetHeader("HOST"));
    ASSERT_EQ("myhost", *header.GetHeader("host"));
    ASSERT_TRUE(header.GetHeader("CORRELATION-ID"));
    ASSERT_EQ("123", *header.GetHeader("CORRELATION-ID"));
    ASSERT_TRUE(header.GetHeader("User-Agent"));
    ASSERT_EQ("HTTPTool/1.0  ", *header.GetHeader("User-Agent"));
    ASSERT_TRUE(header.GetHeader("Host"));
    ASSERT_EQ("myhost", *header.GetHeader("Host"));
    ASSERT_TRUE(header.GetHeader("Accept"));
    ASSERT_EQ("*/*", *header.GetHeader("Accept"));
    
    ASSERT_EQ(1, header.major_version());
    ASSERT_EQ(34, header.minor_version());
    // method is undefined for response, in our case, it's set to 0.
    ASSERT_EQ(brpc::HTTP_METHOD_DELETE, header.method());
    ASSERT_EQ(brpc::HTTP_STATUS_GONE, header.status_code());
    ASSERT_STREQ(brpc::HttpReasonPhrase(header.status_code()), /*not GoneBlah*/
                 header.reason_phrase());
    
    ASSERT_TRUE(header.GetHeader("log-id"));
    ASSERT_EQ("456", *header.GetHeader("log-id"));
    ASSERT_TRUE(header.GetHeader("Authorization"));
    ASSERT_EQ("test", *header.GetHeader("Authorization"));
}

TEST(HttpMessageTest, bad_format) {
    const char *http_request =
        "slkdjflksdf skldjf\r\n";
    brpc::HttpMessage http_message;
    ASSERT_EQ(-1, http_message.ParseFromArray(http_request, strlen(http_request)));
}

TEST(HttpMessageTest, incompleted_request_line) {
    const char *http_request = "GE" ;
    brpc::HttpMessage http_message;
    ASSERT_TRUE(http_message.ParseFromArray(http_request, strlen(http_request)) >= 0);
    ASSERT_FALSE(http_message.Completed());
}

TEST(HttpMessageTest, parse_from_iobuf) {
    const size_t content_length = 8192;
    char header[1024];
    snprintf(header, sizeof(header),
            "GET /service/method?key1=value1&key2=value2&key3=value3 HTTP/1.1\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %lu\r\n"
            "\r\n",
            content_length);
    butil::IOBuf content;
    for (size_t i = 0; i < content_length; ++i) {
        content.push_back('2');
    }
    butil::IOBuf request;
    request.append(header);
    request.append(content);

    brpc::HttpMessage http_message;
    ASSERT_TRUE(http_message.ParseFromIOBuf(request) >= 0);
    ASSERT_TRUE(http_message.Completed());
    ASSERT_EQ(content, http_message.body());
    ASSERT_EQ(content, http_message.body().to_string());
    ASSERT_EQ("text/plain", http_message.header().content_type());
}


TEST(HttpMessageTest, parse_http_head_response) {
    char response1[1024] = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: 1024\r\n"
                          "\r\n";
    butil::IOBuf request;
    request.append(response1);

    brpc::HttpMessage http_message(false, brpc::HTTP_METHOD_HEAD);
    ASSERT_TRUE(http_message.ParseFromIOBuf(request) >= 0);
    ASSERT_TRUE(http_message.Completed()) << http_message.stage();
    ASSERT_EQ("text/plain", http_message.header().content_type());
    const std::string* content_length = http_message.header().GetHeader("Content-Length");
    ASSERT_NE(nullptr, content_length);
    ASSERT_EQ("1024", *content_length);


    char response2[1024] = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Transfer-Encoding: chunked\r\n"
                           "\r\n";
    butil::IOBuf request2;
    request2.append(response2);
    brpc::HttpMessage http_message2(false, brpc::HTTP_METHOD_HEAD);
    ASSERT_TRUE(http_message2.ParseFromIOBuf(request2) >= 0);
    ASSERT_TRUE(http_message2.Completed()) << http_message2.stage();
    ASSERT_EQ("text/plain", http_message2.header().content_type());
    const std::string* transfer_encoding = http_message2.header().GetHeader("Transfer-Encoding");
    ASSERT_NE(nullptr, transfer_encoding);
    ASSERT_EQ("chunked", *transfer_encoding);
}

TEST(HttpMessageTest, parse_http_cookie) {
    const char* http_request =
        "GET /CloudApiControl HTTP/1.1\r\n"
        "Host: api.map.baidu.com\r\n"
        "Accept: application/json\r\n"
        "cookie: a=1\r\n"
        "Cookie: b=2\r\n"
        "\r\n";
    butil::IOBuf buf;
    buf.append(http_request);
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)buf.size(), http_message.ParseFromIOBuf(buf));
    ASSERT_TRUE(http_message.Completed());

    const std::string* cookie
        = http_message.header().GetHeader("cookie");
    ASSERT_NE(nullptr, cookie);
    ASSERT_EQ("a=1; b=2", *cookie);
}

TEST(HttpMessageTest, parse_http_set_cookie) {
    char response[1024] = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: 1024\r\n"
                          "set-cookie: a=1\r\n"
                          "Set-Cookie: b=2\r\n"
                          "\r\n";
    butil::IOBuf request;
    request.append(response);
    brpc::HttpMessage http_message(false, brpc::HTTP_METHOD_HEAD);
    ASSERT_TRUE(http_message.ParseFromIOBuf(request) >= 0);
    ASSERT_TRUE(http_message.Completed()) << http_message.stage();

    const std::string* set_cookie = http_message.header().GetHeader("set-cookie");
    ASSERT_NE(nullptr, set_cookie);
    ASSERT_EQ("a=1", *set_cookie);
    std::vector<const std::string*> all_set_cookie
        = http_message.header().GetAllSetCookieHeader();
    for (const std::string* sc : all_set_cookie) {
        ASSERT_NE(nullptr, sc);
        if (set_cookie == sc) {
            ASSERT_EQ("a=1", *sc);
        } else {
            ASSERT_EQ("b=2", *sc);
        }
        if (http_message.header().IsSetCookie(*sc)) {
        }
    }
    int set_cookie_value1_count = 0;
    int set_cookie_value2_count = 0;
    for (auto iter = http_message.header().HeaderBegin();
         iter != http_message.header().HeaderEnd(); ++iter) {
        if (!http_message.header().IsSetCookie(iter->first)) {
            continue;
        }
        if (iter->second == "b=2") {
            ++set_cookie_value2_count;
        } else if (iter->second == "a=1") {
            ++set_cookie_value1_count;
        }
    }
    ASSERT_EQ(1, set_cookie_value1_count);
    ASSERT_EQ(1, set_cookie_value2_count);
}

TEST(HttpMessageTest, cl_and_te) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;

    // https://datatracker.ietf.org/doc/html/rfc2616#section-14.41
    // If multiple encodings have been applied to an entity, the transfer-
    // codings MUST be listed in the order in which they were applied.
    const char* request_buf1 = "POST /chunked_w_content_length HTTP/1.1\r\n"
                               "Content-Length: 10\r\n"
                               "Transfer-Encoding: gzip,chunked\r\n"
                               "\r\n"
                               "5; ilovew3;whattheluck=aretheseparametersfor\r\nhello\r\n"
                               "6; blahblah; blah\r\n world\r\n"
                               "0\r\n"
                               "\r\n";
    butil::IOBuf request1;
    request1.append(request_buf1);

    const char* request_buf2 = "POST /chunked_w_content_length HTTP/1.1\r\n"
                               "Content-Length: 19\r\n"
                               "Transfer-Encoding: chunked,gzip\r\n"
                               "\r\n"
                               "Message Body sdfsdf";
    butil::IOBuf request2;
    request2.append(request_buf2);

    const char* response_buf1 = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 10\r\n"
                                "Transfer-Encoding: gzip,chunked\r\n"
                                "\r\n"
                                "5; ilovew3;whattheluck=aretheseparametersfor\r\nhello\r\n"
                                "6; blahblah; blah\r\n world\r\n"
                                "0\r\n"
                                "\r\n";
    butil::IOBuf response1;
    response1.append(response_buf1);

    const char* response_buf2 = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 19\r\n"
                                "Transfer-Encoding: chunked,gzip\r\n"
                                "\r\n"
                                "Message Body sdfsdf";
    butil::IOBuf response2;
    response2.append(response_buf2);

    brpc::FLAGS_allow_chunked_length = false;
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(request1), -1)
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(request2), -1)
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(response1), -1)
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(response2), -1)
                        << http_message._parser;
    }

    brpc::FLAGS_allow_chunked_length = true;
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(request1), request1.size())
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(request2), -1)
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(response1), response1.size())
                        << http_message._parser;
    }
    {
        brpc::HttpMessage http_message;
        ASSERT_EQ(http_message.ParseFromIOBuf(response2), -1)
                        << http_message._parser;
    }
}

brpc::http_errno ParseHttpErrno(const char* buf) {
    butil::IOBuf data;
    data.append(buf);
    brpc::HttpMessage http_message;
    if (http_message.ParseFromIOBuf(data) == (ssize_t)data.size() &&
        http_message.Completed()) {
        return brpc::HPE_OK;
    }
    brpc::http_errno err = (brpc::http_errno)http_message._parser.http_errno;
    return err != brpc::HPE_OK ? err : brpc::HPE_UNKNOWN;
}

TEST(HttpMessageTest, space_before_colon_of_framing_headers) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_http_strict_header_token = false;

    const char* rejected[] = {
        "POST / HTTP/1.1\r\nHost: a.com\r\nContent-Length : 5\r\n\r\nhello",
        "POST / HTTP/1.1\r\nHost: a.com\r\nTransfer-Encoding : chunked\r\n\r\n0\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a.com\r\nConnection : close\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a.com\r\nUpgrade : h2c\r\n\r\n",
    };
    for (size_t i = 0; i < arraysize(rejected); ++i) {
        ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN, ParseHttpErrno(rejected[i]))
            << rejected[i];
    }

    // Names without framing semantics keep the historical leniency.
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno(
        "POST / HTTP/1.1\r\nHost: a.com\r\nX-Foo : bar\r\n"
        "Content-Length: 5\r\n\r\nhello"));
}

TEST(HttpMessageTest, strict_header_token) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_http_strict_header_token = false;

    const char* lenient_only[] = {
        "POST / HTTP/1.1\r\nHost: a.com\r\nX-Foo : bar\r\n"
        "Content-Length: 5\r\n\r\nhello",
        // A space anywhere in the name, not just before the colon.
        "GET / HTTP/1.1\r\nHost: a.com\r\nX Foo: bar\r\n\r\n",
        // Names that only look like a framing one until they diverge.
        "GET / HTTP/1.1\r\nHost: a.com\r\nContent-Type : text/plain\r\n\r\n",
    };
    for (size_t i = 0; i < arraysize(lenient_only); ++i) {
        ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno(lenient_only[i]))
            << lenient_only[i];
    }

    brpc::FLAGS_http_strict_header_token = true;
    for (size_t i = 0; i < arraysize(lenient_only); ++i) {
        ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN,
                  ParseHttpErrno(lenient_only[i])) << lenient_only[i];
    }
    // Well-formed names are unaffected.
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno("POST / HTTP/1.1\r\n"
                                           "Host: a.com\r\nX-Foo: bar\r\n"
                                           "Content-Length: 5\r\n\r\n"
                                           "hello"));
}

TEST(HttpMessageTest, transfer_encoding_list_with_space_after_comma) {
    butil::IOBuf response;
    response.append("HTTP/1.1 200 OK\r\n"
                    "Transfer-Encoding: gzip, chunked\r\n\r\n"
                    "5\r\nhello\r\n0\r\n\r\n");
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)response.size(), http_message.ParseFromIOBuf(response))
        << http_message._parser;
    ASSERT_TRUE(http_message.Completed());
    ASSERT_TRUE(http_message._parser.flags & brpc::F_CHUNKED)
        << http_message._parser;
    ASSERT_EQ("hello", http_message.body().to_string());

    // A request would have been rejected outright by RFC 7230 3.3.3 before,
    // because uses_transfer_encoding was set while F_CHUNKED was not.
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno("POST / HTTP/1.1\r\n"
                                           "Host: a.com\r\n"
                                           "Transfer-Encoding: gzip, chunked\r\n\r\n"
                                           "0\r\n\r\n"));
}

TEST(HttpMessageTest, content_length_with_interior_space) {
    ASSERT_EQ(brpc::HPE_INVALID_CONTENT_LENGTH, ParseHttpErrno("POST / HTTP/1.1\r\n"
                                                               "Host: a.com\r\n"
                                                               "Content-Length: 1 3\r\n\r\n"
                                                               "0123456789abc"));

    // Trailing OWS is legal and does not change the value.
    butil::IOBuf data;
    data.append("POST / HTTP/1.1\r\nHost: a.com\r\nContent-Length: 13 \r\n\r\n0123456789abc");
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)data.size(), http_message.ParseFromIOBuf(data))
        << http_message._parser;
    ASSERT_TRUE(http_message.Completed());
    ASSERT_EQ("0123456789abc", http_message.body().to_string());
}

TEST(HttpMessageTest, obs_fold_of_framing_headers) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_http_allow_obs_fold = false;

    const char* folded_te_value = "POST / HTTP/1.1\r\n"
                                  "Host: a.com\r\n"
                                  "Transfer-Encoding:\r\n chunked\r\n\r\n"
                                  "0\r\n\r\n";
    // Folding inside the value restarts the chunked matcher, so this one never
    // reached F_CHUNKED, but we still report "chun ked" where a proxy that
    // unfolds reports "chunked".
    const char* folded_te_token = "GET / HTTP/1.1\r\n"
                                  "Host: a.com\r\n"
                                  "Transfer-Encoding: chun\r\n ked\r\n\r\n";
    const char* folded_cl_value = "POST / HTTP/1.1\r\n"
                                  "Host: a.com\r\n"
                                  "Content-Length:\r\n 13\r\n\r\n"
                                  "0123456789abc";
    const char* folded_cl_digits = "POST / HTTP/1.1\r\n"
                                   "Host: a.com\r\n"
                                   "Content-Length: 1\r\n 3\r\n\r\n"
                                   "0123456789abc";

    ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN, ParseHttpErrno(folded_te_value));
    ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN, ParseHttpErrno(folded_te_token));
    ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN, ParseHttpErrno(folded_cl_value));
    ASSERT_EQ(brpc::HPE_INVALID_HEADER_TOKEN, ParseHttpErrno(folded_cl_digits));

    // Folding a header that decides nothing about framing is still accepted.
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno(
        "POST / HTTP/1.1\r\nHost: a.com\r\nX-Foo: a\r\n b\r\n"
        "Content-Length: 5\r\n\r\nhello"));

    brpc::FLAGS_http_allow_obs_fold = true;
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno(folded_te_value));
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno(folded_cl_value));
    // Unfolding turns this into `Content-Length: 1 3`, which stays invalid.
    ASSERT_EQ(brpc::HPE_INVALID_CONTENT_LENGTH,
              ParseHttpErrno(folded_cl_digits));
    // Unfolding leaves a Transfer-Encoding that is not chunked, which RFC 7230
    // 3.3.3 makes unparseable in a request whatever the fold policy is.
    ASSERT_EQ(brpc::HPE_INVALID_TRANSFER_ENCODING,
              ParseHttpErrno(folded_te_token));
}

TEST(HttpMessageTest, chunked_trailer_is_not_a_header) {
    butil::IOBuf request;
    request.append("POST / HTTP/1.1\r\n"
                   "Host: a.com\r\n"
                   "X-Forwarded-For: 10.0.0.1\r\n"
                   "Transfer-Encoding: chunked\r\n\r\n"
                   "5\r\nhello\r\n"
                   "0\r\n"
                   "X-Injected: evil\r\n"
                   "Authorization: Bearer stolen\r\n"
                   "X-Forwarded-For: 6.6.6.6\r\n"
                   "\r\n");
    brpc::HttpMessage http_message;
    ASSERT_EQ((ssize_t)request.size(), http_message.ParseFromIOBuf(request))
        << http_message._parser;
    ASSERT_TRUE(http_message.Completed());
    // The body is unaffected by dropping the trailer.
    ASSERT_EQ("hello", http_message.body().to_string());

    brpc::HttpHeader& header = http_message.header();
    ASSERT_EQ(nullptr, header.GetHeader("X-Injected"));
    ASSERT_EQ(nullptr, header.GetHeader("Authorization"));
    // A trailer repeating a real header must not be appended to it either,
    // which is where a comma-folded name like X-Forwarded-For would land.
    const std::string* xff = header.GetHeader("X-Forwarded-For");
    ASSERT_TRUE(xff != nullptr);
    ASSERT_EQ("10.0.0.1", *xff);
}

TEST(HttpMessageTest, htab_is_ows_in_header_values) {
    // Trailing OWS after the number, alone and mixed with SP.
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno("POST / HTTP/1.1\r\n"
                                           "Host: a.com\r\n"
                                           "Content-Length: 13\t\r\n\r\n"
                                           "0123456789abc"));
    ASSERT_EQ(brpc::HPE_OK, ParseHttpErrno("POST / HTTP/1.1\r\n"
                                           "Host: a.com\r\n"
                                           "Content-Length: 13 \t\r\n\r\n"
                                           "0123456789abc"));
    // A tab between digits ends the number just like a space, so the value
    // stays invalid rather than reading as 13.
    ASSERT_EQ(brpc::HPE_INVALID_CONTENT_LENGTH,
              ParseHttpErrno("POST / HTTP/1.1\r\n"
                             "Host: a.com\r\n"
                             "Content-Length: 1\t3\r\n\r\n"
                             "0123456789abc"));

    // Tabs around a transfer-coding and after `Connection: close` used to leave
    // the matcher in a state that never set the flag. The Connection case
    // parsed without an error, so only the flag catches it.
    struct { const char* buf; unsigned int flag; } flagged[] = {
        {"POST / HTTP/1.1\r\nHost: a.com\r\n"
         "Transfer-Encoding: gzip,\tchunked\r\n\r\n0\r\n\r\n", brpc::F_CHUNKED},
        {"POST / HTTP/1.1\r\nHost: a.com\r\n"
         "Transfer-Encoding: chunked\t\r\n\r\n0\r\n\r\n", brpc::F_CHUNKED},
        {"GET / HTTP/1.1\r\nHost: a.com\r\nConnection: close\t\r\n\r\n", brpc::F_CONNECTION_CLOSE},
    };
    for (size_t i = 0; i < arraysize(flagged); ++i) {
        butil::IOBuf data;
        data.append(flagged[i].buf);
        brpc::HttpMessage http_message;
        ASSERT_EQ((ssize_t)data.size(), http_message.ParseFromIOBuf(data))
            << http_message._parser;
        ASSERT_TRUE(http_message._parser.flags & flagged[i].flag)
            << http_message._parser;
    }
}

TEST(HttpMessageTest, find_method_property_by_uri) {
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(new test::EchoService(),
                                   brpc::SERVER_OWNS_SERVICE));
    ASSERT_EQ(0, server.Start(9237, nullptr));
    std::string unknown_method;
    brpc::Server::MethodProperty* mp = nullptr;
              
    mp = FindMethodPropertyByURI("", &server, nullptr);
    ASSERT_TRUE(mp);
    ASSERT_EQ("index", mp->method->service()->name());

    mp = FindMethodPropertyByURI("/", &server, nullptr);
    ASSERT_TRUE(mp);
    ASSERT_EQ("index", mp->method->service()->name());

    mp = FindMethodPropertyByURI("//", &server, nullptr);
    ASSERT_FALSE(mp);

    mp = FindMethodPropertyByURI("flags", &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("flags", mp->method->service()->name());
    
    mp = FindMethodPropertyByURI("/flags/port", &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("flags", mp->method->service()->name());
    ASSERT_EQ("port", unknown_method);
    
    mp = FindMethodPropertyByURI("/flags/foo/bar", &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("flags", mp->method->service()->name());
    ASSERT_EQ("foo/bar", unknown_method);
    
    mp = FindMethodPropertyByURI("/brpc.flags/$*",
                                 &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("flags", mp->method->service()->name());
    ASSERT_EQ("$*", unknown_method);

    mp = FindMethodPropertyByURI("EchoService/Echo", &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("test.EchoService.Echo", mp->method->full_name());
    
    mp = FindMethodPropertyByURI("/EchoService/Echo",
                                 &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("test.EchoService.Echo", mp->method->full_name());
    
    mp = FindMethodPropertyByURI("/test.EchoService/Echo",
                                 &server, &unknown_method);
    ASSERT_TRUE(mp);
    ASSERT_EQ("test.EchoService.Echo", mp->method->full_name());
    
    mp = FindMethodPropertyByURI("/test.EchoService/no_such_method",
                                 &server, &unknown_method);
    ASSERT_FALSE(mp);
}

// A path with empty segments is a different path per RFC 3986, but the
// splitter used to resolve it skips them, so //flags used to reach the same
// builtin service as /flags. That difference is what lets a request slip past
// a front proxy whose ACL only matches the collapsed form, so such paths are
// rejected rather than collapsed.
TEST(HttpMessageTest, reject_empty_path_segments) {
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(new test::EchoService(),
                                   brpc::SERVER_OWNS_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
    std::string unknown_method;

    const char* const kRejected[] = {
        "//",
        "//flags",
        "///flags",
        "/flags//port",
        "//EchoService/Echo",
        "/EchoService//Echo",
        "/EchoService/Echo//",
    };
    for (const char* path : kRejected) {
        ASSERT_FALSE(FindMethodPropertyByURI(path, &server, &unknown_method))
            << "path=" << path;
    }

    // The collapsed forms keep working.
    ASSERT_TRUE(FindMethodPropertyByURI("/", &server, nullptr));
    ASSERT_TRUE(FindMethodPropertyByURI("/flags/port", &server,
                                        &unknown_method));
    ASSERT_TRUE(FindMethodPropertyByURI("/EchoService/Echo", &server,
                                        &unknown_method));

    // -http_allow_empty_path_segments restores the old lenient behavior for
    // deployments that depend on it.
    brpc::policy::FLAGS_http_allow_empty_path_segments = true;
    for (const char* path : kRejected) {
        ASSERT_TRUE(FindMethodPropertyByURI(path, &server, &unknown_method))
            << "path=" << path;
    }
    brpc::policy::FLAGS_http_allow_empty_path_segments = false;
}

TEST(HttpMessageTest, http_header) {
    brpc::HttpHeader header;
    
    header.set_version(10, 100);
    ASSERT_EQ(10, header.major_version());
    ASSERT_EQ(100, header.minor_version());

    ASSERT_TRUE(header.content_type().empty());
    header.set_content_type("text/plain");
    ASSERT_EQ("text/plain", header.content_type());
    ASSERT_FALSE(header.GetHeader("content-type"));
    header.set_content_type("application/json");
    ASSERT_EQ("application/json", header.content_type());
    ASSERT_FALSE(header.GetHeader("content-type"));
    
    ASSERT_FALSE(header.GetHeader("key1"));
    header.AppendHeader("key1", "value1");
    const std::string* value = header.GetHeader("key1");
    ASSERT_TRUE(value && *value == "value1");
    header.AppendHeader("key1", "value2");
    value = header.GetHeader("key1");
    ASSERT_TRUE(value && *value == "value1,value2");
    header.SetHeader("key1", "value3");
    value = header.GetHeader("key1");
    ASSERT_TRUE(value && *value == "value3");
    header.RemoveHeader("key1");
    ASSERT_FALSE(header.GetHeader("key1"));

    ASSERT_FALSE(header.GetHeader(brpc::HttpHeader::COOKIE));
    header.AppendHeader(brpc::HttpHeader::COOKIE, "value1=1");
    value = header.GetHeader(brpc::HttpHeader::COOKIE);
    ASSERT_TRUE(value && *value == "value1=1");
    header.AppendHeader(brpc::HttpHeader::COOKIE, "value2=2");
    value = header.GetHeader(brpc::HttpHeader::COOKIE);
    ASSERT_TRUE(value && *value == "value1=1; value2=2");
    header.SetHeader(brpc::HttpHeader::COOKIE, "value3");
    value = header.GetHeader(brpc::HttpHeader::COOKIE);
    ASSERT_TRUE(value && *value == "value3");
    header.RemoveHeader(brpc::HttpHeader::COOKIE);
    ASSERT_FALSE(header.GetHeader(brpc::HttpHeader::COOKIE));

    std::string set_cookie_value1 = "a=1";
    std::string set_cookie_value2 = "b=2";
    std::string set_cookie_value3 = "c=3";
    ASSERT_FALSE(header.GetHeader(brpc::HttpHeader::SET_COOKIE));
    header.SetHeader(brpc::HttpHeader::SET_COOKIE, set_cookie_value1);
    value = header.GetHeader(brpc::HttpHeader::SET_COOKIE);
    ASSERT_TRUE(value && *value == set_cookie_value1);
    header.AppendHeader(brpc::HttpHeader::SET_COOKIE, set_cookie_value2);
    value = header.GetHeader(brpc::HttpHeader::SET_COOKIE);
    ASSERT_TRUE(value && *value == set_cookie_value1);
    header.SetHeader(brpc::HttpHeader::SET_COOKIE, set_cookie_value3);
    value = header.GetHeader(brpc::HttpHeader::SET_COOKIE);
    ASSERT_TRUE(value && *value == set_cookie_value3);
    std::vector<const std::string*> all_set_cookie
        = header.GetAllSetCookieHeader();
    ASSERT_EQ(2u, all_set_cookie.size());
    for (const std::string* sc : all_set_cookie) {
        ASSERT_TRUE(sc);
        ASSERT_TRUE(*sc == set_cookie_value2 || *sc == set_cookie_value3);
    }
    int set_cookie_value2_count = 0;
    int set_cookie_value3_count = 0;
    for (auto iter = header.HeaderBegin(); iter != header.HeaderEnd(); ++iter) {
        if (!header.IsSetCookie(brpc::HttpHeader::SET_COOKIE)) {
            continue;
        }
        if (iter->second == set_cookie_value2) {
            ++set_cookie_value2_count;
        } else if (iter->second == set_cookie_value3) {
            ++set_cookie_value3_count;
        }
    }
    ASSERT_EQ(1, set_cookie_value2_count);
    ASSERT_EQ(1, set_cookie_value3_count);
    header.RemoveHeader(brpc::HttpHeader::SET_COOKIE);
    ASSERT_FALSE(header.GetHeader(brpc::HttpHeader::SET_COOKIE));
    ASSERT_EQ(header._first_set_cookie, nullptr);

    ASSERT_EQ(brpc::HTTP_METHOD_GET, header.method());
    header.set_method(brpc::HTTP_METHOD_POST);
    ASSERT_EQ(brpc::HTTP_METHOD_POST, header.method());

    ASSERT_EQ(brpc::HTTP_STATUS_OK, header.status_code());
    ASSERT_STREQ(brpc::HttpReasonPhrase(header.status_code()),
                 header.reason_phrase());
    header.set_status_code(brpc::HTTP_STATUS_CONTINUE);
    ASSERT_EQ(brpc::HTTP_STATUS_CONTINUE, header.status_code());
    ASSERT_STREQ(brpc::HttpReasonPhrase(header.status_code()),
                 header.reason_phrase());
    
    header.set_status_code(brpc::HTTP_STATUS_GONE);
    ASSERT_EQ(brpc::HTTP_STATUS_GONE, header.status_code());
    ASSERT_STREQ(brpc::HttpReasonPhrase(header.status_code()),
                 header.reason_phrase());
}

TEST(HttpMessageTest, empty_url) {
    butil::EndPoint host;
    ASSERT_FALSE(ParseHttpServerAddress(&host, ""));
}

TEST(HttpMessageTest, serialize_http_request) {
    brpc::HttpHeader header;
    ASSERT_EQ(0u, header.HeaderCount());
    header.SetHeader("Foo", "Bar");
    ASSERT_EQ(1u, header.HeaderCount());
    header.set_method(brpc::HTTP_METHOD_POST);
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:1234", &ep));
    butil::IOBuf request;
    butil::IOBuf content;
    content.append("data");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nHost: 127.0.0.1:1234\r\nFoo: Bar\r\nAccept: */*\r\nUser-Agent: brpc/1.0 curl/7.0\r\n\r\ndata", request);

    // user-set content-length is ignored.
    header.SetHeader("Content-Length", "100");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nHost: 127.0.0.1:1234\r\nFoo: Bar\r\nAccept: */*\r\nUser-Agent: brpc/1.0 curl/7.0\r\n\r\ndata", request);

    // user-host overwrites passed-in remote_side
    header.SetHeader("Host", "MyHost: 4321");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nFoo: Bar\r\nHost: MyHost: 4321\r\nAccept: */*\r\nUser-Agent: brpc/1.0 curl/7.0\r\n\r\ndata", request);

    // user-set accept
    header.SetHeader("accePT"/*intended uppercase*/, "blahblah");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nFoo: Bar\r\naccePT: blahblah\r\nHost: MyHost: 4321\r\nUser-Agent: brpc/1.0 curl/7.0\r\n\r\ndata", request);

    // user-set UA
    header.SetHeader("user-AGENT", "myUA");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nFoo: Bar\r\naccePT: blahblah\r\nHost: MyHost: 4321\r\nuser-AGENT: myUA\r\n\r\ndata", request);

    // user-set Authorization
    header.SetHeader("authorization", "myAuthString");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\nFoo: Bar\r\naccePT: blahblah\r\nHost: MyHost: 4321\r\nuser-AGENT: myUA\r\nauthorization: myAuthString\r\n\r\ndata", request);

    header.SetHeader("Transfer-Encoding", "chunked");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nFoo: Bar\r\naccePT: blahblah\r\nTransfer-Encoding: chunked\r\nHost: MyHost: 4321\r\nuser-AGENT: myUA\r\nauthorization: myAuthString\r\n\r\ndata", request);

    // GET does not serialize content and user-set content-length is ignored.
    header.set_method(brpc::HTTP_METHOD_GET);
    header.SetHeader("Content-Length", "100");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("GET / HTTP/1.1\r\nFoo: Bar\r\naccePT: blahblah\r\nHost: MyHost: 4321\r\nuser-AGENT: myUA\r\nauthorization: myAuthString\r\n\r\n", request);
}

TEST(HttpMessageTest, serialize_http_response) {
    brpc::HttpHeader header;
    header.SetHeader("Foo", "Bar");
    header.set_method(brpc::HTTP_METHOD_POST);
    butil::IOBuf response;
    butil::IOBuf content;
    content.append("data");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nFoo: Bar\r\n\r\ndata", response)
        << butil::ToPrintable(response);
    // Content is cleared.
    CHECK(content.empty());

    // nullptr content
    header.SetHeader("Content-Length", "100");
    MakeRawHttpResponse(&response, &header, nullptr);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\nContent-Length: 100\r\n\r\n", response)
        << butil::ToPrintable(response);

    header.SetHeader("Transfer-Encoding", "chunked");
    MakeRawHttpResponse(&response, &header, nullptr);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\nTransfer-Encoding: chunked\r\n\r\n", response)
                    << butil::ToPrintable(response);
    header.RemoveHeader("Transfer-Encoding");

    // User-set content-length is ignored.
    content.append("data2");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nFoo: Bar\r\n\r\ndata2", response)
        << butil::ToPrintable(response);

    header.SetHeader("Content-Length", "100");
    header.SetHeader("Transfer-Encoding", "chunked");
    MakeRawHttpResponse(&response, &header, nullptr);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\nTransfer-Encoding: chunked\r\n\r\n", response)
                    << butil::ToPrintable(response);
    header.RemoveHeader("Transfer-Encoding");

    // User-set content-length and transfer-encoding is ignored when status code is 204 or 1xx.
    // 204 No Content.
    header.SetHeader("Content-Length", "100");
    header.SetHeader("Transfer-Encoding", "chunked");
    header.set_status_code(brpc::HTTP_STATUS_NO_CONTENT);
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 204 No Content\r\nFoo: Bar\r\n\r\n", response);
    // 101 Continue
    header.SetHeader("Content-Length", "100");
    header.SetHeader("Transfer-Encoding", "chunked");
    header.set_status_code(brpc::HTTP_STATUS_CONTINUE);
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 100 Continue\r\nFoo: Bar\r\n\r\n", response)
        << butil::ToPrintable(response);

    // when request method is HEAD:
    // 1. There isn't user-set content-length, length of content is used.
    header.set_method(brpc::HTTP_METHOD_HEAD);
    header.set_status_code(brpc::HTTP_STATUS_OK);content.append("data2");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nFoo: Bar\r\n\r\n", response)
        << butil::ToPrintable(response);
    // 2. User-set content-length is not ignored .
    header.SetHeader("Content-Length", "100");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\nContent-Length: 100\r\n\r\n", response)
        << butil::ToPrintable(response);
}

TEST(HttpMessageTest, serialize_header_with_crlf_is_not_injected) {
    // A header value carrying CR/LF must not terminate the current line and
    // introduce extra header fields (HTTP request/response splitting).
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:1234", &ep));

    brpc::HttpHeader req_header;
    req_header.set_method(brpc::HTTP_METHOD_POST);
    req_header.SetHeader("X-Evil", "a\r\nInjected: 1");
    butil::IOBuf req_content;
    req_content.append("data");
    butil::IOBuf request;
    MakeRawHttpRequest(&request, &req_header, ep, &req_content);
    std::string request_str = request.to_string();
    ASSERT_EQ(std::string::npos, request_str.find("Injected: 1")) << request_str;

    brpc::HttpHeader res_header;
    res_header.SetHeader("X-Evil", "a\r\nInjected: 1");
    butil::IOBuf res_content;
    res_content.append("data");
    butil::IOBuf response;
    MakeRawHttpResponse(&response, &res_header, &res_content);
    std::string response_str = response.to_string();
    ASSERT_EQ(std::string::npos, response_str.find("Injected: 1")) << response_str;
}

TEST(HttpMessageTest, serialize_content_type_with_crlf_is_not_injected) {
    // Content-Type goes through the same emission path and must be dropped
    // (not written) when it carries CR/LF.
    butil::EndPoint ep;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:1234", &ep));

    brpc::HttpHeader req_header;
    req_header.set_method(brpc::HTTP_METHOD_POST);
    req_header.set_content_type("text/plain\r\nInjected: 1");
    butil::IOBuf req_content;
    req_content.append("data");
    butil::IOBuf request;
    MakeRawHttpRequest(&request, &req_header, ep, &req_content);
    std::string request_str = request.to_string();
    ASSERT_EQ(std::string::npos, request_str.find("Injected: 1")) << request_str;

    brpc::HttpHeader res_header;
    res_header.set_content_type("text/plain\r\nInjected: 1");
    butil::IOBuf res_content;
    res_content.append("data");
    butil::IOBuf response;
    MakeRawHttpResponse(&response, &res_header, &res_content);
    std::string response_str = response.to_string();
    ASSERT_EQ(std::string::npos, response_str.find("Injected: 1")) << response_str;
}

TEST(HttpMessageTest, http_1_1_request_without_host) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_allow_http_1_1_request_without_host = false;
    {
        butil::IOBuf request;
        request.append("GET /service/method HTTP/1.1\r\n"
                       "Content-Type: text/plain\r\n\r\n");

        brpc::HttpMessage http_message;
        ASSERT_TRUE(http_message.ParseFromIOBuf(request) < 0);
    }
    {
        butil::IOBuf request;
        request.append("GET http://baidu.com/service/method HTTP/1.1\r\n"
                       "Content-Type: text/plain\r\n\r\n");

        brpc::HttpMessage http_message;
        ASSERT_TRUE(http_message.ParseFromIOBuf(request) >= 0);
        ASSERT_TRUE(http_message.Completed());
        ASSERT_EQ("text/plain", http_message.header().content_type());
    }
    {
        butil::IOBuf request;
        request.append("GET /service/method HTTP/1.1\r\n"
                       "Content-Type: text/plain\r\n"
                       "Host: baidu.com\r\n\r\n");

        brpc::HttpMessage http_message;
        ASSERT_GE(http_message.ParseFromIOBuf(request), 0);
        ASSERT_GE(http_message.ParseFromArray(nullptr, 0), 0);
        ASSERT_TRUE(http_message.Completed());
        ASSERT_EQ("text/plain", http_message.header().content_type());
    }
}

} //namespace
