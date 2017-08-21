// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:15:57

#ifndef  BRPC_LIST_SERVICE_H
#define  BRPC_LIST_SERVICE_H

#include <ostream>
#include "brpc/builtin_service.pb.h"

namespace brpc {

class Server;

class ListService : public list {
public:
    explicit ListService(Server* server) : _server(server) {}
    
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::ListRequest* request,
                        ::brpc::ListResponse* response,
                        ::google::protobuf::Closure* done);
private:
    Server* _server;
};

} // namespace brpc


#endif  //BRPC_LIST_SERVICE_H
