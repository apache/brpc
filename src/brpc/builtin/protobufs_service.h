// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Sep 14 12:58:35 CST 2015

#ifndef  BRPC_PROTOBUFS_SERVICE_H
#define  BRPC_PROTOBUFS_SERVICE_H

#include <ostream>
#include "brpc/builtin_service.pb.h"


namespace brpc {

class Server;

// Show DebugString of protobuf messages used in the server.
//   /protobufs         : list all supported messages.
//   /protobufs/<msg>/  : Show DebugString() of <msg>

class ProtobufsService : public protobufs {
public:
    explicit ProtobufsService(Server* server);
    
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::ProtobufsRequest* request,
                        ::brpc::ProtobufsResponse* response,
                        ::google::protobuf::Closure* done);
private:
    int Init();
    
    Server* _server;
    typedef std::map<std::string, std::string> Map;
    Map _map;
};

} // namespace brpc


#endif  //BRPC_PROTOBUFS_SERVICE_H
