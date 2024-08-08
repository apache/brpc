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

#include "brpc/rpc_pb_message_factory.h"
#include "butil/object_pool.h"

namespace brpc {

struct DefaultRpcPBMessages : public RpcPBMessages {
    DefaultRpcPBMessages() : request(NULL), response(NULL) {}
    ::google::protobuf::Message* Request() override { return request; }
    ::google::protobuf::Message* Response() override { return response; }

    ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
};

class DefaultRpcPBMessageFactory : public RpcPBMessageFactory {
public:
    RpcPBMessages* Get(const ::google::protobuf::Service& service,
        const ::google::protobuf::MethodDescriptor& method) override {
        auto messages = butil::get_object<DefaultRpcPBMessages>();
        messages->request = service.GetRequestPrototype(&method).New();
        messages->response = service.GetResponsePrototype(&method).New();
        return messages;
    }

    void Return(RpcPBMessages* messages) override {
        auto default_messages = static_cast<DefaultRpcPBMessages*>(messages);
        delete default_messages->request;
        delete default_messages->response;
        default_messages->request = NULL;
        default_messages->response = NULL;
        butil::return_object(default_messages);
    }
};

RpcPBMessageFactory* GetDefaultRpcPBMessageFactory() {
    static DefaultRpcPBMessageFactory factory;
    return &factory;
}


} // namespace brpc