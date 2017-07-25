// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:28:43

#include <vector>                           // std::vector
#include <google/protobuf/descriptor.h>     // ServiceDescriptor
#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/list_service.h"


namespace brpc {

void ListService::default_method(::google::protobuf::RpcController*, 
                                 const ::brpc::ListRequest*,
                                 ::brpc::ListResponse* response,
                                 ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    std::vector<google::protobuf::Service*> services;
    _server->ListServices(&services);
    for (size_t i = 0; i < services.size(); ++i) {
        google::protobuf::ServiceDescriptorProto *svc = response->add_service();
        services[i]->GetDescriptor()->CopyTo(svc);
    }
}

} // namespace brpc

