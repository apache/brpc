// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Dec  3 15:08:36 CST 2014

#ifndef  BRPC_PPROF_SERVICE_H
#define  BRPC_PPROF_SERVICE_H

#include "brpc/builtin_service.pb.h"


namespace brpc {

class PProfService : public pprof {
public:
    void profile(::google::protobuf::RpcController* controller,
                 const ::brpc::ProfileRequest* request,
                 ::brpc::ProfileResponse* response,
                 ::google::protobuf::Closure* done);

    void contention(::google::protobuf::RpcController* controller,
                    const ::brpc::ProfileRequest* request,
                    ::brpc::ProfileResponse* response,
                    ::google::protobuf::Closure* done);
    
    void heap(::google::protobuf::RpcController* controller,
              const ::brpc::ProfileRequest* request,
              ::brpc::ProfileResponse* response,
              ::google::protobuf::Closure* done);

    void growth(::google::protobuf::RpcController* controller,
                const ::brpc::ProfileRequest* request,
                ::brpc::ProfileResponse* response,
                ::google::protobuf::Closure* done);

    void symbol(::google::protobuf::RpcController* controller,
                const ::brpc::ProfileRequest* request,
                ::brpc::ProfileResponse* response,
                ::google::protobuf::Closure* done);

    void cmdline(::google::protobuf::RpcController* controller,
                 const ::brpc::ProfileRequest* request,
                 ::brpc::ProfileResponse* response,
                 ::google::protobuf::Closure* done);
};

} // namespace brpc


#endif  //BRPC_PPROF_SERVICE_H
