// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:15:57

#ifndef  BRPC_THREADS_SERVICE_H
#define  BRPC_THREADS_SERVICE_H

#include <ostream>
#include "brpc/builtin_service.pb.h"


namespace brpc {

class ThreadsService : public threads {
public:
    void default_method(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::ThreadsRequest* request,
                        ::brpc::ThreadsResponse* response,
                        ::google::protobuf::Closure* done);
};

} // namespace brpc


#endif  //BRPC_THREADS_SERVICE_H
