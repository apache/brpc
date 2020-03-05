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

// mcpack2pb - Make protobuf be front-end of mcpack/compack

// Date: Mon Oct 19 17:17:36 CST 2015

#include "mcpack2pb/field_type.h"

namespace mcpack2pb {

const char* type2str(FieldType type) {
    bool is_short = false;
    if (type & FIELD_SHORT_MASK) {
        is_short = true;
        type = (FieldType)(type & ~FIELD_SHORT_MASK);
    }
    switch (type) {
    case FIELD_OBJECT:         return "object";
    case FIELD_ARRAY:          return "array";
    case FIELD_ISOARRAY:       return "isoarray";
    case FIELD_OBJECTISOARRAY: return "object_isoarray";
    case FIELD_STRING:         return (is_short ? "string(short)" : "string");
    case FIELD_BINARY:         return (is_short ? "binary(short)" : "binary");
    case FIELD_INT8:          return "int8";
    case FIELD_INT16:         return "int16";
    case FIELD_INT32:         return "int32";
    case FIELD_INT64:         return "int64";
    case FIELD_UINT8:         return "uint8";
    case FIELD_UINT16:        return "uint16";
    case FIELD_UINT32:        return "uint32";
    case FIELD_UINT64:        return "uint64";
    case FIELD_BOOL:           return "bool";
    case FIELD_FLOAT:          return "float";
    case FIELD_DOUBLE:         return "double";
    case FIELD_DATE:           return "date";
    case FIELD_NULL:           return "null";
    }
    return "unknown_field_type";
}

} // namespace mcpack2pb
