// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Jun  2 16:28:03 CST 2015

#ifndef BRPC_IDS_SERVICE_H
#define BRPC_IDS_SERVICE_H

#include "brpc/builtin_service.pb.h"


namespace brpc {

class IdsService: public ids {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::IdsRequest* request,
                        ::brpc::IdsResponse* response,
                        ::google::protobuf::Closure* done);
};

} // namespace brpc


#endif // BRPC_IDS_SERVICE_H
