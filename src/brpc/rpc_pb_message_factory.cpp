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

namespace brpc {

struct DefaultRpcPBMessages : public RpcPBMessages {
    DefaultRpcPBMessages() : request(NULL), response(NULL) {}
    ::google::protobuf::Message* Request() override { return request; }
    ::google::protobuf::Message* Response() override { return response; }

    ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
};


RpcPBMessages* DefaultRpcPBMessageFactory::Get(
        const ::google::protobuf::Service& service,
        const ::google::protobuf::MethodDescriptor& method) {
    auto messages = butil::get_object<DefaultRpcPBMessages>();
    messages->request = service.GetRequestPrototype(&method).New();
    messages->response = service.GetResponsePrototype(&method).New();
    return messages;
}

void DefaultRpcPBMessageFactory::Return(RpcPBMessages* messages) {
    auto default_messages = static_cast<DefaultRpcPBMessages*>(messages);
    delete default_messages->request;
    delete default_messages->response;
    default_messages->request = NULL;
    default_messages->response = NULL;
    butil::return_object(default_messages);
}

} // namespace brpc