// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:15:57

#ifndef  BRPC_VERSION_SERVICE_H
#define  BRPC_VERSION_SERVICE_H

#include <ostream>
#include "brpc/builtin_service.pb.h"


namespace brpc {

class Server;

class VersionService : public version {
public:
    explicit VersionService(Server* server) : _server(server) {}
    
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::VersionRequest* request,
                        ::brpc::VersionResponse* response,
                        ::google::protobuf::Closure* done);
private:
    Server* _server;
};

} // namespace brpc


#endif  //BRPC_VERSION_SERVICE_H
