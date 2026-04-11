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

#include <cstring>
#include "brpc/details/http_parser.h"
#include "brpc/http_method.h"

#define kMinInputLength 5
#define kMaxInputLength 4096

static int on_url_cb(brpc::http_parser* p, const char* at, size_t length) { return 0; }
static int on_header_field_cb(brpc::http_parser* p, const char* at, size_t length) { return 0; }
static int on_header_value_cb(brpc::http_parser* p, const char* at, size_t length) { return 0; }
static int on_body_cb(brpc::http_parser* p, const char* at, size_t length) { return 0; }
static int on_message_begin_cb(brpc::http_parser* p) { return 0; }
static int on_headers_complete_cb(brpc::http_parser* p) { return 0; }
static int on_message_complete_cb(brpc::http_parser* p) { return 0; }

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < kMinInputLength || size > kMaxInputLength){
        return 1;
    }

    // Use first byte to select mode
    uint8_t mode = data[0] % 4;
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    switch (mode) {
        case 0: {
            // Fuzz low-level HTTP request parsing
            brpc::http_parser parser;
            brpc::http_parser_init(&parser, brpc::HTTP_REQUEST);
            brpc::http_parser_settings settings;
            memset(&settings, 0, sizeof(settings));
            settings.on_url = on_url_cb;
            settings.on_header_field = on_header_field_cb;
            settings.on_header_value = on_header_value_cb;
            settings.on_body = on_body_cb;
            settings.on_message_begin = on_message_begin_cb;
            settings.on_headers_complete = on_headers_complete_cb;
            settings.on_message_complete = on_message_complete_cb;
            brpc::http_parser_execute(&parser, &settings,
                               reinterpret_cast<const char*>(payload), payload_size);
            break;
        }
        case 1: {
            // Fuzz low-level HTTP response parsing
            brpc::http_parser parser;
            brpc::http_parser_init(&parser, brpc::HTTP_RESPONSE);
            brpc::http_parser_settings settings;
            memset(&settings, 0, sizeof(settings));
            settings.on_url = on_url_cb;
            settings.on_header_field = on_header_field_cb;
            settings.on_header_value = on_header_value_cb;
            settings.on_body = on_body_cb;
            settings.on_message_begin = on_message_begin_cb;
            settings.on_headers_complete = on_headers_complete_cb;
            settings.on_message_complete = on_message_complete_cb;
            brpc::http_parser_execute(&parser, &settings,
                               reinterpret_cast<const char*>(payload), payload_size);
            break;
        }
        case 2: {
            // Fuzz URL parsing (not connect)
            brpc::http_parser_url u;
            brpc::http_parser_parse_url(reinterpret_cast<const char*>(payload),
                                       payload_size, 0, &u);
            break;
        }
        case 3: {
            // Fuzz URL parsing (connect mode)
            brpc::http_parser_url u;
            brpc::http_parser_parse_url(reinterpret_cast<const char*>(payload),
                                       payload_size, 1, &u);
            break;
        }
    }

    return 0;
}
