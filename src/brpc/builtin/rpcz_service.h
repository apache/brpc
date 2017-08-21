// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Apr  8 18:07:12 2015

#ifndef BRPC_RPCZ_SERVICE_H
#define BRPC_RPCZ_SERVICE_H

#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/tabbed.h"


namespace brpc {

class RpczService : public rpcz, public Tabbed {
public:
    void enable(::google::protobuf::RpcController* cntl_base,
                const ::brpc::RpczRequest* request,
                ::brpc::RpczResponse* response,
                ::google::protobuf::Closure* done);

    void disable(::google::protobuf::RpcController* cntl_base,
                 const ::brpc::RpczRequest* request,
                 ::brpc::RpczResponse* response,
                 ::google::protobuf::Closure* done);

    void stats(::google::protobuf::RpcController* cntl_base,
               const ::brpc::RpczRequest* request,
               ::brpc::RpczResponse* response,
               ::google::protobuf::Closure* done);

    void hex_log_id(::google::protobuf::RpcController* cntl_base,
                    const ::brpc::RpczRequest* request,
                    ::brpc::RpczResponse* response,
                    ::google::protobuf::Closure* done);

    void dec_log_id(::google::protobuf::RpcController* cntl_base,
                    const ::brpc::RpczRequest* request,
                    ::brpc::RpczResponse* response,
                    ::google::protobuf::Closure* done);

    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::RpczRequest* request,
                        ::brpc::RpczResponse* response,
                        ::google::protobuf::Closure* done);

    void GetTabInfo(brpc::TabInfoList*) const;
};

} // namespace brpc


#endif // BRPC_RPCZ_SERVICE_H
