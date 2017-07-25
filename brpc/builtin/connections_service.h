// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 18:07:12 2015

#ifndef BRPC_CONNECTIONS_SERVICE_H
#define BRPC_CONNECTIONS_SERVICE_H

#include "brpc/socket_id.h"
#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/tabbed.h"


namespace brpc {

class Acceptor;
class ConnectionsService : public connections, public Tabbed {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::ConnectionsRequest* request,
                        ::brpc::ConnectionsResponse* response,
                        ::google::protobuf::Closure* done);

    void GetTabInfo(TabInfoList* info_list) const;
    
private:
    void PrintConnections(std::ostream& os, const std::vector<SocketId>& conns,
                          bool use_html, const Server*, bool need_local) const;
};

} // namespace brpc


#endif // BRPC_CONNECTIONS_SERVICE_H
