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


#include <vector>                           // std::vector
#include <google/protobuf/descriptor.h>     // ServiceDescriptor
#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/list_service.h"


namespace brpc {

void ListService::default_method(::google::protobuf::RpcController*, 
                                 const ::brpc::ListRequest*,
                                 ::brpc::ListResponse* response,
                                 ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    std::vector<google::protobuf::Service*> services;
    _server->ListServices(&services);
    for (size_t i = 0; i < services.size(); ++i) {
        google::protobuf::ServiceDescriptorProto *svc = response->add_service();
        services[i]->GetDescriptor()->CopyTo(svc);
    }
}

} // namespace brpc
