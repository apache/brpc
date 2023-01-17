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

#ifndef MCPACK2PB_MCPACK_FIELD_TYPE_H
#define MCPACK2PB_MCPACK_FIELD_TYPE_H

#include <stddef.h>
#include <stdint.h>

namespace mcpack2pb {

enum FieldType {
    // NOTE: All names end with 0 and all name_size count 0.
    
    // Group of fields.
    // | FieldLongHead | Name | ItemsHead | Item1 | Item2 | ...
    FIELD_OBJECT = 0x10,
    FIELD_ARRAY = 0x20,

    // Isomorphic array of primitive type. Notice that the items are values
    // without any header. E.g. If type is int32_t, an items occupies 4 bytes.
    // | FieldLongHead | Name | IsoItemsHead | Item1 | Item2 | ...
    FIELD_ISOARRAY = 0x30,

    // Layout of repeated objects.
    // [{a=1,b=2},{a=3,b=4}] => {a=[1,3],b=[2,4]}
    FIELD_OBJECTISOARRAY = 0x40,

    // C-style strings (ending with 0)
    // strlen <= 254 : | FieldShortHead | Name | String |
    // otherwise     : | FieldLongHead  | Name | String |
    //                                             ^ ending with 0
    FIELD_STRING = 0x50,

    // Binary data
    // length <= 255: | FieldShortHead | Name | Data |
    // otherwise:     | FieldLongHead  | Name | Data |
    FIELD_BINARY = 0x60,

    // Primitive types.
    // | FieldFixedHead | Name | Value |
    FIELD_INT8 = 0x11,
    FIELD_INT16 = 0x12,
    FIELD_INT32 = 0x14,
    FIELD_INT64 = 0x18,
    FIELD_UINT8 = 0x21,
    FIELD_UINT16 = 0x22,
    FIELD_UINT32 = 0x24,
    FIELD_UINT64 = 0x28,
    FIELD_BOOL = 0x31,
    FIELD_FLOAT = 0x44,
    FIELD_DOUBLE = 0x48,
    
    // TODO(gejun): Don't know what this is. Seems to be timestamp, but grep nothing
    // from public/idlcompiler and public/mcpack
    FIELD_DATE = 0x58,

    // Represent absent field in OBJECTISOARRAY
    // | FieldFixedHead | Name | char(0) |
    FIELD_NULL = 0x61
};

// Get description of the type.
const char* type2str(FieldType type);

inline const char* type2str(uint8_t type) {
    return type2str((FieldType)type);
}

// Masks of types.
// String <= 254 or BinaryData <= 255 may have this bit set to save 3 bytes.
static const uint8_t FIELD_SHORT_MASK = 0x80;

// primitive-types & FIELD_FIXED_MASK = non-zero.
// non-primitive-types & FIELD_FIXED_MASK = 0.
static const uint8_t FIELD_FIXED_MASK = 0xf;

// Valid field & FIELD_NON_DELETED_MASK = non-zero.
// Deleted fields should be skipped.
static const uint8_t FIELD_NON_DELETED_MASK = 0x70;

// Represent fields unknown/unset/invalid.
static const FieldType FIELD_UNKNOWN = (FieldType)0;

// Maximum of level of array/object
// Parsing/Serialization would fail if the level exceeds this value.
static const int MAX_DEPTH = 128;

enum PrimitiveFieldType {
    PRIMITIVE_FIELD_INT8 = FIELD_INT8,
    PRIMITIVE_FIELD_INT16 = FIELD_INT16,
    PRIMITIVE_FIELD_INT32 = FIELD_INT32,
    PRIMITIVE_FIELD_INT64 = FIELD_INT64,
    PRIMITIVE_FIELD_UINT8 = FIELD_UINT8,
    PRIMITIVE_FIELD_UINT16 = FIELD_UINT16,
    PRIMITIVE_FIELD_UINT32 = FIELD_UINT32,
    PRIMITIVE_FIELD_UINT64 = FIELD_UINT64,
    PRIMITIVE_FIELD_BOOL = FIELD_BOOL,
    PRIMITIVE_FIELD_FLOAT = FIELD_FLOAT,
    PRIMITIVE_FIELD_DOUBLE = FIELD_DOUBLE
};

static const PrimitiveFieldType PRIMITIVE_FIELD_UNKNOWN =
    (PrimitiveFieldType)FIELD_UNKNOWN;

inline const char* type2str(PrimitiveFieldType type) {
    return type2str((FieldType)type);
}

inline bool is_primitive(FieldType type) {
    return type & 0xF;
}

inline bool is_integral(PrimitiveFieldType type) {
    return (type & 0xF0) < 0x40;
}

inline bool is_floating_point(PrimitiveFieldType type) {
    return (type & 0xF0) == 0x40;
}

inline size_t get_primitive_type_size(PrimitiveFieldType type) {
    return (type & 0xF);
}

inline size_t get_primitive_type_size(FieldType type) {
    return (type & 0xF);
}

} // namespace mcpack2pb

#endif // MCPACK2PB_MCPACK_FIELD_TYPE_H
