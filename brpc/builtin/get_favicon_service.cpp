// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:57:02

#include "base/macros.h"                           // ARRAY_SIZE
#include "base/iobuf.h"                            // base::IOBuf
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
static base::IOBuf* s_favicon_buf = NULL;
static void InitFavIcon() {
    s_favicon_buf = new base::IOBuf;
    s_favicon_buf->append((const void *)s_favicon_array, 
                          arraysize(s_favicon_array));
}
static const base::IOBuf& GetFavIcon() {
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

