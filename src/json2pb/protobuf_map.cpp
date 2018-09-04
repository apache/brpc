// Copyright (c) 2014 Baidu, Inc.

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
