// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

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
