// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
