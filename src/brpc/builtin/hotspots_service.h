// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

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
