// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:54:42

#ifndef  BRPC_GET_JAVASCRIPT_SERVICE_H
#define  BRPC_GET_JAVASCRIPT_SERVICE_H

#include "brpc/get_js.pb.h"


namespace brpc {

// Get packed js.
//   "/js/sorttable"  : http://www.kryogenix.org/code/browser/sorttable/
//   "/js/jquery_min" : jquery 1.8.3
//   "/js/flot_min"   : ploting library for jquery.
class GetJsService : public ::brpc::js {
public:
    void sorttable(::google::protobuf::RpcController* controller,
                   const GetJsRequest* request,
                   GetJsResponse* response,
                   ::google::protobuf::Closure* done);
    
    void jquery_min(::google::protobuf::RpcController* controller,
                    const GetJsRequest* request,
                    GetJsResponse* response,
                    ::google::protobuf::Closure* done);

    void flot_min(::google::protobuf::RpcController* controller,
                  const GetJsRequest* request,
                  GetJsResponse* response,
                  ::google::protobuf::Closure* done);

    void viz_min(::google::protobuf::RpcController* controller,
                 const GetJsRequest* request,
                 GetJsResponse* response,
                 ::google::protobuf::Closure* done);
};

} // namespace brpc


#endif  // BRPC_GET_JAVASCRIPT_SERVICE_H
