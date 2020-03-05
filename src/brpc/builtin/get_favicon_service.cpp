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


#include "butil/macros.h"                           // ARRAY_SIZE
#include "butil/iobuf.h"                            // butil::IOBuf
#include "brpc/controller.h"                   // Controller
#include "brpc/builtin/get_favicon_service.h"


namespace brpc {

static unsigned char s_favicon_array[] = {
    71, 73, 70, 56, 57, 97, 16, 0, 16, 0, 241, 0, 0, 0, 0, 0, 153, 153, 153, 255,
    255, 255, 0, 0, 0, 33, 249, 4, 9, 50, 0, 3, 0, 33, 255, 11, 78, 69, 84, 83, 67,
    65, 80, 69, 50, 46, 48, 3, 1, 0, 0, 0, 44, 0, 0, 0, 0, 16, 0, 16, 0, 0, 2, 231,
    4, 0, 0, 0, 0, 0, 0, 0, 0, 132, 16, 66, 8, 33, 132, 16, 4, 65, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 64, 0, 0, 1, 32, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 0, 16, 0,
    0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 0,
    16, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0,
    0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 10, 0, 0, 33, 249, 4, 9, 50, 0, 3, 0, 44, 0, 0, 0, 0, 16, 0, 16,
    0, 0, 2, 231, 4, 0, 0, 0, 0, 0, 0, 0, 0, 132, 16, 66, 8, 33, 132, 16, 4, 65, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 1, 32, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64,
    0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 2,
    0, 64, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 64, 0, 32, 0, 0, 0, 2, 129, 0, 0,
    0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16,
    0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 16, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 59
};

static pthread_once_t s_favicon_buf_once = PTHREAD_ONCE_INIT; 
static butil::IOBuf* s_favicon_buf = NULL;
static void InitFavIcon() {
    s_favicon_buf = new butil::IOBuf;
    s_favicon_buf->append((const void *)s_favicon_array, 
                          arraysize(s_favicon_array));
}
static const butil::IOBuf& GetFavIcon() {
    pthread_once(&s_favicon_buf_once, InitFavIcon);
    return *s_favicon_buf;
}

void GetFaviconService::default_method(
    ::google::protobuf::RpcController* controller,
    const GetFaviconRequest* /*request*/,
    GetFaviconResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("image/x-icon");
    cntl->response_attachment().clear();
    cntl->response_attachment().append(GetFavIcon());
    if (done) {
        done->Run();
    }
}

} // namespace brpc
