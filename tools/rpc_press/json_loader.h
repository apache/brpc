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

#ifndef BRPC_JSON_LOADER_H
#define BRPC_JSON_LOADER_H

#include <string>
#include <deque>
#include <google/protobuf/message.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/dynamic_message.h>
#include <butil/iobuf.h>

namespace brpc {

// This utility loads pb messages in json format from a file or string.
class JsonLoader {
public:
    JsonLoader(google::protobuf::compiler::Importer* importer, 
               google::protobuf::DynamicMessageFactory* factory,
               const std::string& service_name,
               const std::string& method_name);
    ~JsonLoader() {}

    // TODO(gejun): messages should be lazily loaded.
    
    // Load jsons from fd or string, convert them into pb messages, then insert
    // them into `out_msgs'.
    void load_messages(int fd, std::deque<google::protobuf::Message*>* out_msgs);
    void load_messages(const std::string& jsons,
                       std::deque<google::protobuf::Message*>* out_msgs);
    
private:
    class Reader;

    void load_messages(
        JsonLoader::Reader* ctx,
        std::deque<google::protobuf::Message*>* out_msgs);

    google::protobuf::compiler::Importer* _importer;
    google::protobuf::DynamicMessageFactory* _factory;
    std::string _service_name;
    std::string _method_name;
    const google::protobuf::Message* _request_prototype;
};

} // namespace brpc

#endif // BRPC_JSON_LOADER_H
