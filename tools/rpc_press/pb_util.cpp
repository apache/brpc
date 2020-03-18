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

#include <butil/logging.h>
#include "pb_util.h"

using google::protobuf::ServiceDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::DescriptorPool;
using google::protobuf::MessageFactory;
using google::protobuf::Message;
using google::protobuf::MethodDescriptor;
using google::protobuf::compiler::Importer;
using google::protobuf::DynamicMessageFactory;
using google::protobuf::DynamicMessageFactory;
using std::string;

namespace pbrpcframework {

const MethodDescriptor* find_method_by_name(const string& service_name, 
                                            const string& method_name, 
                                            Importer* importer) {
    const ServiceDescriptor* descriptor =
        importer->pool()->FindServiceByName(service_name);
    if (NULL == descriptor) {
        LOG(FATAL) << "Fail to find service=" << service_name;
        return NULL;
    }
    return descriptor->FindMethodByName(method_name);
}

const Message* get_prototype_by_method_descriptor(
    const MethodDescriptor* descripter,
    bool is_input, 
    DynamicMessageFactory* factory) {
    if (NULL == descripter) {
        LOG(FATAL) <<"Param[descripter] is NULL";
        return NULL;
    }   
    const Descriptor* message_descriptor = NULL;
    if (is_input) {
        message_descriptor = descripter->input_type();
    } else {
        message_descriptor = descripter->output_type();
    }   
    return factory->GetPrototype(message_descriptor);
}

const Message* get_prototype_by_name(const string& service_name,
                                     const string& method_name, bool is_input, 
                                     Importer* importer,
                                     DynamicMessageFactory* factory){
    const MethodDescriptor* descripter = find_method_by_name(
        service_name, method_name, importer);
    return get_prototype_by_method_descriptor(descripter, is_input, factory);
}

}  // pbrpcframework
