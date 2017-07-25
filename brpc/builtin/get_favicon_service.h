// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// File: get_favicon_service.h
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:54:42

#ifndef  BRPC_GET_FAVICON_SERVICE_H
#define  BRPC_GET_FAVICON_SERVICE_H

#include "brpc/get_favicon.pb.h"

namespace brpc {

class GetFaviconService : public ico {
public:
    void default_method(::google::protobuf::RpcController* controller,
                        const GetFaviconRequest* request,
                        GetFaviconResponse* response,
                        ::google::protobuf::Closure* done);
};

} // namespace brpc


#endif  // BRPC_GET_FAVICON_SERVICE_H
