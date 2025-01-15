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
#include <google/protobuf/descriptor.h>

#include "brpc/server.h"
#include "brpc/details/http_message.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "echo.pb.h"

namespace brpc {

DECLARE_bool(allow_chunked_length);
DECLARE_bool(allow_http_1_1_request_without_host);

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    brpc::FLAGS_allow_http_1_1_request_without_host = true;
    return RUN_ALL_TESTS();
}

namespace policy {
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
    ASSERT_TRUE(NULL != header.GetHeader("Authorization"));
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
    std::string content;
    for (size_t i = 0; i < content_length; ++i) content.push_back('2');
    butil::IOBuf request;
    request.append(header);
    request.append(content);

    brpc::HttpMessage http_message;
    ASSERT_TRUE(http_message.ParseFromIOBuf(request) >= 0);
    ASSERT_TRUE(http_message.Completed());
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

TEST(HttpMessageTest, find_method_property_by_uri) {
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(new test::EchoService(),
                                   brpc::SERVER_OWNS_SERVICE));
    ASSERT_EQ(0, server.Start(9237, NULL));
    std::string unknown_method;
    brpc::Server::MethodProperty* mp = NULL;
              
    mp = FindMethodPropertyByURI("", &server, NULL);
    ASSERT_TRUE(mp);
    ASSERT_EQ("index", mp->method->service()->name());

    mp = FindMethodPropertyByURI("/", &server, NULL);
    ASSERT_TRUE(mp);
    ASSERT_EQ("index", mp->method->service()->name());

    mp = FindMethodPropertyByURI("//", &server, NULL);
    ASSERT_TRUE(mp);
    ASSERT_EQ("index", mp->method->service()->name());

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

    // NULL content
    header.SetHeader("Content-Length", "100");
    MakeRawHttpResponse(&response, &header, NULL);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\nContent-Length: 100\r\n\r\n", response)
        << butil::ToPrintable(response);

    header.SetHeader("Transfer-Encoding", "chunked");
    MakeRawHttpResponse(&response, &header, NULL);
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
    MakeRawHttpResponse(&response, &header, NULL);
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

TEST(HttpMessageTest, http_1_1_request_without_host) {
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
        ASSERT_GE(http_message.ParseFromArray(NULL, 0), 0);
        ASSERT_TRUE(http_message.Completed());
        ASSERT_EQ("text/plain", http_message.header().content_type());
    }
    brpc::FLAGS_allow_http_1_1_request_without_host = true;
}

} //namespace
