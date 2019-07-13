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

#ifndef UTIL_PB_UTIL_H
#define UTIL_PB_UTIL_H
#include "google/protobuf/message.h"
#include "google/protobuf/descriptor.h"
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/compiler/importer.h>

namespace pbrpcframework {
const google::protobuf::MethodDescriptor* find_method_by_name(
        const std::string& service_name, 
        const std::string& method_name, 
        google::protobuf::compiler::Importer* importer); 

const google::protobuf::Message* get_prototype_by_method_descriptor(
        const google::protobuf::MethodDescriptor* descripter, 
        bool is_input,
        google::protobuf::DynamicMessageFactory* factory);

const google::protobuf::Message* get_prototype_by_name(
        const std::string& service_name, 
        const std::string& method_name, 
        bool is_input, 
        google::protobuf::compiler::Importer* importer, 
        google::protobuf::DynamicMessageFactory* factory);
}
#endif //UTIL_PB_UTIL_H
