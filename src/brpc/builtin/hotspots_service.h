// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Sep 25 11:10:03 CST 2015

#ifndef BRPC_HOTSPOTS_SERVICE_H
#define BRPC_HOTSPOTS_SERVICE_H

#include "brpc/builtin/common.h"
#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/tabbed.h"


namespace brpc {

class Server;

class HotspotsService : public hotspots, public Tabbed {
public:
    void cpu(::google::protobuf::RpcController* cntl_base,
             const ::brpc::HotspotsRequest* request,
             ::brpc::HotspotsResponse* response,
             ::google::protobuf::Closure* done);
    
    void heap(::google::protobuf::RpcController* cntl_base,
              const ::brpc::HotspotsRequest* request,
              ::brpc::HotspotsResponse* response,
              ::google::protobuf::Closure* done);

    void growth(::google::protobuf::RpcController* cntl_base,
                const ::brpc::HotspotsRequest* request,
                ::brpc::HotspotsResponse* response,
                ::google::protobuf::Closure* done);

    void contention(::google::protobuf::RpcController* cntl_base,
                    const ::brpc::HotspotsRequest* request,
                    ::brpc::HotspotsResponse* response,
                    ::google::protobuf::Closure* done);

    void cpu_non_responsive(::google::protobuf::RpcController* cntl_base,
                            const ::brpc::HotspotsRequest* request,
                            ::brpc::HotspotsResponse* response,
                            ::google::protobuf::Closure* done);

    void heap_non_responsive(::google::protobuf::RpcController* cntl_base,
                            const ::brpc::HotspotsRequest* request,
                            ::brpc::HotspotsResponse* response,
                            ::google::protobuf::Closure* done);
    
    void growth_non_responsive(::google::protobuf::RpcController* cntl_base,
                               const ::brpc::HotspotsRequest* request,
                               ::brpc::HotspotsResponse* response,
                               ::google::protobuf::Closure* done);

    void contention_non_responsive(::google::protobuf::RpcController* cntl_base,
                                   const ::brpc::HotspotsRequest* request,
                                   ::brpc::HotspotsResponse* response,
                                   ::google::protobuf::Closure* done);

    void GetTabInfo(brpc::TabInfoList*) const;
};

} // namespace brpc



#endif // BRPC_HOTSPOTS_SERVICE_H

