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

#include "protobuf_map.h"
#include <stdio.h>

namespace json2pb {

using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;

bool IsProtobufMap(const FieldDescriptor* field) {
    if (field->type() != FieldDescriptor::TYPE_MESSAGE || !field->is_repeated()) {
        return false;
    }
    const Descriptor* entry_desc = field->message_type();
    if (entry_desc == NULL) {
        return false;
    }
    if (entry_desc->field_count() != 2) {
        return false;
    }
    const FieldDescriptor* key_desc = entry_desc->field(KEY_INDEX);
    if (NULL == key_desc
        || key_desc->is_repeated()
        || key_desc->cpp_type() != FieldDescriptor::CPPTYPE_STRING
        || strcmp(KEY_NAME, key_desc->name().c_str()) != 0) {
        return false;
    }
    const FieldDescriptor* value_desc = entry_desc->field(VALUE_INDEX);
    if (NULL == value_desc
        || strcmp(VALUE_NAME, value_desc->name().c_str()) != 0) {
        return false;
    }
    return true;
}

} // namespace json2pb
