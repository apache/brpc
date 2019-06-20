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

#include <gtest/gtest.h>
#include <iostream>

#include "butil/time.h"
#include "butil/logging.h"
#include "brpc/details/http_parser.h"
#include "brpc/builtin/common.h"  // AppendFileName

using brpc::http_parser;
using brpc::http_parser_init;
using brpc::http_parser_settings;

class HttpParserTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
};

TEST_F(HttpParserTest, init_perf) {
    const size_t loops = 10000000;
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < loops; ++i) {
        http_parser parser;
        http_parser_init(&parser, brpc::HTTP_REQUEST);
    }
    timer.stop();
    std::cout << "It takes " << timer.n_elapsed() / loops
              << "ns to init a http_parser"
              << std::endl;
}

int on_message_begin(http_parser *) {
    LOG(INFO) << "Start parsing message";
    return 0;
}

int on_url(http_parser *, const char *at, const size_t length) {
    LOG(INFO) << "Get url " << std::string(at, length);
    return 0;
}

int on_headers_complete(http_parser *) {
    LOG(INFO) << "Header complete";
    return 0;
}

int on_message_complete(http_parser *) {
    LOG(INFO) << "Message complete";
    return 0;
}

int on_header_field(http_parser *, const char *at, const size_t length) {
    LOG(INFO) << "Get header field " << std::string(at, length);
    return 0;
}

int on_header_value(http_parser *, const char *at, const size_t length) {
    LOG(INFO) << "Get header value " << std::string(at, length);
    return 0;
}

int on_body(http_parser *, const char *at, const size_t length) {
    LOG(INFO) << "Get body " << std::string(at, length);
    return 0;
}

TEST_F(HttpParserTest, http_example) {
    const char *http_request = 
        "GET /path/file.html?sdfsdf=sdfs HTTP/1.0\r\n"
        "From: someuser@jmarshall.com\r\n"
        "User-Agent: HTTPTool/1.0\r\n"
        "Content-Type: json\r\n"
        "Content-Length: 19\r\n"
        "Host: sdlfjslfd\r\n"
        "Accept: */*\r\n"
        "\r\n"
        "Message Body sdfsdf\r\n"
    ;
    std::cout << http_request << std::endl;

    http_parser parser;
    http_parser_init(&parser, brpc::HTTP_REQUEST);
    http_parser_settings settings;
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_headers_complete = on_headers_complete;
    settings.on_message_complete = on_message_complete;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_body = on_body;
    LOG(INFO) << http_parser_execute(&parser, &settings, http_request, strlen(http_request));
}

TEST_F(HttpParserTest, append_filename) {
    std::string dir;

    dir = "/home/someone/.bsvn/..";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/home", dir);

    dir = "/home/someone/.bsvn/../";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/home", dir);

    dir = "/home/someone/./..";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/", dir);
    
    dir = "/home/someone/./../";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/", dir);

    dir = "/foo/bar";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/foo", dir);

    dir = "/foo/bar/";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/foo", dir);

    dir = "/foo";
    brpc::AppendFileName(&dir, ".");
    ASSERT_EQ("/foo", dir);

    dir = "/foo/";
    brpc::AppendFileName(&dir, ".");
    ASSERT_EQ("/foo/", dir);

    dir = "foo";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("", dir);

    dir = "foo/";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("", dir);

    dir = "foo/..";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("..", dir);

    dir = "foo/../";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("..", dir);
    
    dir = "/foo";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/", dir);

    dir = "/foo/";
    brpc::AppendFileName(&dir, "..");
    ASSERT_EQ("/", dir);
}
