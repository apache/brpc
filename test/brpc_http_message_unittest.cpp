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
    GFLAGS_NS::SetCommandLineOption("verbose", "100");
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
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\naccePT: blahblah\r\nFoo: Bar\r\nHost: MyHost: 4321\r\nUser-Agent: brpc/1.0 curl/7.0\r\n\r\ndata", request);

    // user-set UA
    header.SetHeader("user-AGENT", "myUA");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\naccePT: blahblah\r\nuser-AGENT: myUA\r\nFoo: Bar\r\nHost: MyHost: 4321\r\n\r\ndata", request);

    // user-set Authorization
    header.SetHeader("authorization", "myAuthString");
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("POST / HTTP/1.1\r\nContent-Length: 4\r\naccePT: blahblah\r\nuser-AGENT: myUA\r\nauthorization: myAuthString\r\nFoo: Bar\r\nHost: MyHost: 4321\r\n\r\ndata", request);

    // GET does not serialize content
    header.set_method(brpc::HTTP_METHOD_GET);
    MakeRawHttpRequest(&request, &header, ep, &content);
    ASSERT_EQ("GET / HTTP/1.1\r\naccePT: blahblah\r\nuser-AGENT: myUA\r\nauthorization: myAuthString\r\nFoo: Bar\r\nHost: MyHost: 4321\r\n\r\n", request);
}

TEST(HttpMessageTest, serialize_http_response) {
    brpc::HttpHeader header;
    header.SetHeader("Foo", "Bar");
    header.set_method(brpc::HTTP_METHOD_POST);
    butil::IOBuf response;
    butil::IOBuf content;
    content.append("data");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nFoo: Bar\r\n\r\ndata", response);
    // content is cleared.
    CHECK(content.empty());

    // user-set content-length is ignored.
    content.append("data2");
    header.SetHeader("Content-Length", "100");
    MakeRawHttpResponse(&response, &header, &content);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nFoo: Bar\r\n\r\ndata2", response);

    // null content
    MakeRawHttpResponse(&response, &header, NULL);
    ASSERT_EQ("HTTP/1.1 200 OK\r\nFoo: Bar\r\n\r\n", response);
}

} //namespace
