// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 16:27:41 2015

#ifndef BRPC_DIR_SERVICE_H
#define BRPC_DIR_SERVICE_H

#include "brpc/builtin_service.pb.h"


namespace brpc {

class DirService : public dir {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::DirRequest* request,
                        ::brpc::DirResponse* response,
                        ::google::protobuf::Closure* done);
};

} // namespace brpc



#endif // BRPC_DIR_SERVICE_H

