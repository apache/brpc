// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:15:57

#ifndef  BRPC_VLOG_SERVICE_H
#define  BRPC_VLOG_SERVICE_H

#include <ostream>
#include "brpc/builtin_service.pb.h"


namespace brpc {

class VLogService : public vlog {
public:
    void default_method(::google::protobuf::RpcController* controller,
                        const ::brpc::VLogRequest* request,
                        ::brpc::VLogResponse* response,
                        ::google::protobuf::Closure* done);

};

} // namespace brpc


#endif  //BRPC_VLOG_SERVICE_H
