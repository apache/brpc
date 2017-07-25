/***********************************************************
*
* Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
*
* Author: tiankui01@baidu.com
*
* Last modified: 2014-10-29 14:20
*
* Filename: pb_util.h
*
* Description: 
*
**********************************************************/
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
