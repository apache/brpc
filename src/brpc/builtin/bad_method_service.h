// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 16:27:41 2015

#ifndef BRPC_BADMETHOD_SERVICE_H
#define BRPC_BADMETHOD_SERVICE_H

#include "brpc/builtin_service.pb.h"


namespace brpc {

class BadMethodService : public badmethod {
public:
    void no_method(::google::protobuf::RpcController* cntl_base,
                   const ::brpc::BadMethodRequest* request,
                   ::brpc::BadMethodResponse* response,
                   ::google::protobuf::Closure* done);
};

} // namespace brpc



#endif // BRPC_BADMETHOD_SERVICE_H

