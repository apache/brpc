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


#include <google/protobuf/descriptor.h>
#include "butil/sys_byteorder.h"
#include "butil/logging.h"
#include "butil/find_cstr.h"
#include "brpc/log.h"
#include "brpc/amf.h"

namespace brpc {

const char* marker2str(AMFMarker marker) {
    switch (marker) {
    case AMF_MARKER_NUMBER:          return "number";
    case AMF_MARKER_BOOLEAN:         return "boolean";
    case AMF_MARKER_STRING:          return "string";
    case AMF_MARKER_OBJECT:          return "object";
    case AMF_MARKER_MOVIECLIP:       return "movieclip";
    case AMF_MARKER_NULL:            return "null";
    case AMF_MARKER_UNDEFINED:       return "undefined";
    case AMF_MARKER_REFERENCE:       return "reference";
    case AMF_MARKER_ECMA_ARRAY:      return "ecma-array";
    case AMF_MARKER_OBJECT_END:      return "object-end";
    case AMF_MARKER_STRICT_ARRAY:    return "strict-array";
    case AMF_MARKER_DATE:            return "date";
    case AMF_MARKER_LONG_STRING:     return "long-string";
    case AMF_MARKER_UNSUPPORTED:     return "unsupported";
    case AMF_MARKER_RECORDSET:       return "recordset";
    case AMF_MARKER_XML_DOCUMENT:    return "xml-document";
    case AMF_MARKER_TYPED_OBJECT:    return "typed-object";
    case AMF_MARKER_AVMPLUS_OBJECT:  return "avmplus-object";
    }
    return "Unknown marker";
}

const char* marker2str(uint8_t marker) {
    return marker2str((AMFMarker)marker);
}

// =============== AMFField ==============

AMFField::AMFField()
    : _type(AMF_MARKER_UNDEFINED), _is_shortstr(false), _strsize(0) {
}

AMFField::AMFField(const AMFField& rhs)
    : _type(rhs._type), _is_shortstr(rhs._is_shortstr), _strsize(rhs._strsize) {
    _num = rhs._num;
    if (rhs.IsString()) {
        if (!rhs._is_shortstr) {
            _str = (char*)malloc(rhs._strsize + 1);
            memcpy(_str, rhs._str, rhs._strsize + 1);
        }
    } else if (rhs.IsObject()) {
        _obj = new AMFObject(*rhs._obj);
    } else if (rhs.IsArray()) {
        _arr = new AMFArray(*rhs._arr);
    }
}

AMFField& AMFField::operator=(const AMFField& rhs) {
    Clear();
    _type = rhs._type;
    _is_shortstr = rhs._is_shortstr;
    _strsize = rhs._strsize;
    _num = rhs._num;
    if (rhs.IsString()) {
        if (!_is_shortstr) {
            _str = (char*)malloc(rhs._strsize + 1);
            memcpy(_str, rhs._str, rhs._strsize + 1);
        }
    } else if (rhs.IsObject()) {
        _obj = new AMFObject(*rhs._obj);
    } else if (rhs.IsArray()) {
        _arr = new AMFArray(*rhs._arr);
    }
    return *this;
}

void AMFField::SlowerClear() {
    switch (type()) {
    case AMF_MARKER_NUMBER:
    case AMF_MARKER_BOOLEAN:
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_NULL:
    case AMF_MARKER_UNDEFINED:
    case AMF_MARKER_REFERENCE:
    case AMF_MARKER_OBJECT_END:
    case AMF_MARKER_DATE:
    case AMF_MARKER_UNSUPPORTED:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_TYPED_OBJECT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        break;
    case AMF_MARKER_STRICT_ARRAY:
        delete _arr;
        _arr = NULL;
        break;
    case AMF_MARKER_STRING:
    case AMF_MARKER_LONG_STRING:
        if (!_is_shortstr) {
            free(_str);
            _str = NULL;
        }
        _strsize = 0;
        _is_shortstr = false;
        break;
    case AMF_MARKER_OBJECT:
    case AMF_MARKER_ECMA_ARRAY:
        delete _obj;
        _obj = NULL;
        break;
    }
    _type = AMF_MARKER_UNDEFINED;
}

const AMFField* AMFObject::Find(const char* name) const {
    std::map<std::string, AMFField>::const_iterator it =
        butil::find_cstr(_fields, name);
    if (it != _fields.end()) {
        return &it->second;
    }
    return NULL;
}

void AMFField::SetString(const butil::StringPiece& str) {
    // TODO: Try to reuse the space.
    Clear();
    if (str.size() < SSO_LIMIT) {
        _type = AMF_MARKER_STRING;
        _is_shortstr = true;
        _strsize = str.size();
        memcpy(_shortstr, str.data(), str.size());
        _shortstr[str.size()] = '\0';
    } else {
        _type = (str.size() < 65536u ?
                 AMF_MARKER_STRING : AMF_MARKER_LONG_STRING);
        char* buf = (char*)malloc(str.size() + 1);
        memcpy(buf, str.data(), str.size());
        buf[str.size()] = '\0';
        _is_shortstr = false;
        _strsize = str.size();
        _str = buf;
    }
}

void AMFField::SetBool(bool val) {
    if (_type != AMF_MARKER_BOOLEAN) {
        Clear();
        _type = AMF_MARKER_BOOLEAN;
    }
    _b = val;
}

void AMFField::SetNumber(double val) {
    if (_type != AMF_MARKER_NUMBER) {
        Clear();
        _type = AMF_MARKER_NUMBER;
    }
    _num = val;
}

void AMFField::SetNull() {
    if (_type != AMF_MARKER_NULL) {
        Clear();
        _type = AMF_MARKER_NULL;
    }
}

void AMFField::SetUndefined() {
    if (_type != AMF_MARKER_UNDEFINED) {
        Clear();
        _type = AMF_MARKER_UNDEFINED;
    }
}

void AMFField::SetUnsupported() {
    if (_type != AMF_MARKER_UNSUPPORTED) {
        Clear();
        _type = AMF_MARKER_UNSUPPORTED;
    }
}

AMFObject* AMFField::MutableObject() {
    if (!IsObject()) {
        Clear();
        _type = AMF_MARKER_OBJECT;
        _obj = new AMFObject;
    }
    return _obj;
}

AMFArray* AMFField::MutableArray() {
    if (!IsArray()) {
        Clear();
        _type = AMF_MARKER_STRICT_ARRAY;
        _arr = new AMFArray;
    }
    return _arr;
}

// ============= AMFObject =============

void AMFObject::SetString(const std::string& name, const butil::StringPiece& str) {
    _fields[name].SetString(str);
}

void AMFObject::SetBool(const std::string& name, bool val) {
    _fields[name].SetBool(val);
}

void AMFObject::SetNumber(const std::string& name, double val) {
    _fields[name].SetNumber(val);
}

void AMFObject::SetNull(const std::string& name) {
    _fields[name].SetNull();
}

void AMFObject::SetUndefined(const std::string& name) {
    _fields[name].SetUndefined();
}

void AMFObject::SetUnsupported(const std::string& name) {
    _fields[name].SetUnsupported();
}

AMFObject* AMFObject::MutableObject(const std::string& name) {
    return _fields[name].MutableObject();
}

AMFArray* AMFObject::MutableArray(const std::string& name) {
    return _fields[name].MutableArray();
}

static bool ReadAMFShortStringBody(std::string* str, AMFInputStream* stream) {
    uint16_t len = 0;
    if (stream->cut_u16(&len) != 2u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    str->resize(len);
    if (len != 0 && stream->cutn(&(*str)[0], len) != len) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    return true;
}

static bool ReadAMFLongStringBody(std::string* str, AMFInputStream* stream) {
    uint32_t len = 0;
    if (stream->cut_u32(&len) != 4u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    str->resize(len);
    if (len != 0 && stream->cutn(&(*str)[0], len) != len) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    return true;
}

bool ReadAMFString(std::string* str, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_STRING) {
        return ReadAMFShortStringBody(str, stream);
    } else if ((AMFMarker)marker == AMF_MARKER_LONG_STRING) {
        return ReadAMFLongStringBody(str, stream);
    }
    LOG(ERROR) << "Expected string, actually " << marker2str(marker);
    return false;
}

bool ReadAMFBool(bool* val, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_BOOLEAN) {
        uint8_t tmp;
        if (stream->cut_u8(&tmp) != 1u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        *val = tmp;
        return true;
    }
    LOG(ERROR) << "Expected boolean, actually " << marker2str(marker);
    return false;
}

bool ReadAMFNumber(double* val, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_NUMBER) {
        if (stream->cut_u64((uint64_t*)val) != 8u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        return true;
    }
    LOG(ERROR) << "Expected number, actually " << marker2str(marker);
    return false;
}

bool ReadAMFUint32(uint32_t* val, AMFInputStream* stream) {
    double d;
    if (!ReadAMFNumber(&d, stream)) {
        return false;
    }
    *val = (uint32_t)d;
    return true;
}

bool ReadAMFNull(AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_NULL) {
        return true;
    }
    LOG(ERROR) << "Expected null, actually " << marker2str(marker);
    return false;
}

bool ReadAMFUndefined(AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_UNDEFINED) {
        return true;
    }
    LOG(ERROR) << "Expected undefined, actually " << marker2str(marker);
    return false;
}

bool ReadAMFUnsupported(AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_UNSUPPORTED) {
        return true;
    }
    LOG(ERROR) << "Expected unsupported, actually " << marker2str(marker);
    return false;
}

static bool ReadAMFObjectBody(google::protobuf::Message* message,
                              AMFInputStream* stream);
static bool SkipAMFObjectBody(AMFInputStream* stream);

static bool ReadAMFObjectField(AMFInputStream* stream,
                               google::protobuf::Message* message,
                               const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = NULL;
    if (field) {
        reflection = message->GetReflection();
    }
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    switch ((AMFMarker)marker) {
    case AMF_MARKER_NUMBER: {
        uint64_t val = 0;
        if (stream->cut_u64(&val) != 8u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        if (field) {
            if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
                LOG(WARNING) << "Can't set double=" << val << " to "
                             << field->full_name();
            } else {
                double* dptr = (double*)&val;
                reflection->SetDouble(message, field, *dptr);
            }
        }
    } break;
    case AMF_MARKER_BOOLEAN: {
        uint8_t val = 0;
        if (stream->cut_u8(&val) != 1u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        if (field) {
            if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
                LOG(WARNING) << "Can't set bool to " << field->full_name();
            } else {
                reflection->SetBool(message, field, !!val);
            }
        }
    } break;
    case AMF_MARKER_STRING: {
        std::string val;
        if (!ReadAMFShortStringBody(&val, stream)) {
            return false;
        }
        if (field) {
            if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
                LOG(WARNING) << "Can't set string=`" << val << "' to "
                           << field->full_name();
            } else {
                reflection->SetString(message, field, val);
            }
        }
    } break;
    case AMF_MARKER_TYPED_OBJECT: {
        std::string class_name;
        if (!ReadAMFShortStringBody(&class_name, stream)) {
            LOG(ERROR) << "Fail to read class_name";
        }
    }
    // fall through
    case AMF_MARKER_OBJECT: {
        if (field) {
            if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
                LOG(WARNING) << "Can't set object to " << field->full_name();
            } else {
                google::protobuf::Message* m = reflection->MutableMessage(message, field);
                if (!ReadAMFObjectBody(m, stream)) {
                    return false;
                }
            }
        } else {
            if (!SkipAMFObjectBody(stream)) {
                return false;
            }
        }
    } break;
    case AMF_MARKER_NULL:
    case AMF_MARKER_UNDEFINED:
    case AMF_MARKER_UNSUPPORTED:
        // Nothing to do
        break;
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_REFERENCE:
    case AMF_MARKER_ECMA_ARRAY:
    case AMF_MARKER_STRICT_ARRAY:
    case AMF_MARKER_DATE:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        LOG(ERROR) << marker2str(marker) << " is not supported yet";
        return false;
    case AMF_MARKER_OBJECT_END:
        CHECK(false) << "object-end shouldn't be present here";
        return false;
    case AMF_MARKER_LONG_STRING: {
        std::string val;
        if (!ReadAMFLongStringBody(&val, stream)) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        if (field) {
            if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
                LOG(WARNING) << "Can't set string=`" << val << "' to "
                             << field->full_name();
            } else {
                reflection->SetString(message, field, val);
            }
        }
    } break;
    } // switch
    return true;
}

static bool ReadAMFObjectBody(google::protobuf::Message* message,
                              AMFInputStream* stream) {
    const google::protobuf::Descriptor* desc = message->GetDescriptor();
    std::string name;
    while (ReadAMFShortStringBody(&name, stream)) {
        if (name.empty()) {
            uint8_t marker;
            if (stream->cut_u8(&marker) != 1u) {
                LOG(ERROR) << "stream is not long enough";
                return false;
            }
            if ((AMFMarker)marker != AMF_MARKER_OBJECT_END) {
                LOG(ERROR) << "marker=" << marker
                           << " after empty name is not object end";
                return false;
            }
            break;
        }
        const google::protobuf::FieldDescriptor* field = desc->FindFieldByName(name);
        RPC_VLOG_IF(field == NULL) << "Unknown field=" << desc->full_name()
                                   << "." << name;
        if (!ReadAMFObjectField(stream, message, field)) {
            return false;
        }
    }
    return true;
}

static bool SkipAMFObjectBody(AMFInputStream* stream) {
    std::string name;
    while (ReadAMFShortStringBody(&name, stream)) {
        if (name.empty()) {
            uint8_t marker;
            if (stream->cut_u8(&marker) != 1u) {
                LOG(ERROR) << "stream is not long enough";
                return false;
            }
            if ((AMFMarker)marker != AMF_MARKER_OBJECT_END) {
                LOG(ERROR) << "marker=" << marker
                           << " after empty name is not object end";
                return false;
            }
            break;
        }
        if (!ReadAMFObjectField(stream, NULL, NULL)) {
            return false;
        }
    }
    return true;
}

static bool ReadAMFEcmaArrayBody(google::protobuf::Message* message,
                                 AMFInputStream* stream) {
    uint32_t count = 0;
    if (stream->cut_u32(&count) != 4u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    const google::protobuf::Descriptor* desc = message->GetDescriptor();
    std::string name;
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadAMFShortStringBody(&name, stream)) {
            LOG(ERROR) << "Fail to read name from the stream";
            return false;
        }
        const google::protobuf::FieldDescriptor* field = desc->FindFieldByName(name);
        RPC_VLOG_IF(field == NULL) << "Unknown field=" << desc->full_name()
                                   << "." << name;
        if (!ReadAMFObjectField(stream, message, field)) {
            return false;
        }
    }
    return true;
}

bool ReadAMFObject(google::protobuf::Message* msg, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_OBJECT) {
        if (!ReadAMFObjectBody(msg, stream)) {
            return false;
        }
    } else if ((AMFMarker)marker == AMF_MARKER_ECMA_ARRAY) {
        if (!ReadAMFEcmaArrayBody(msg, stream)) {
            return false;
        }
    } else if ((AMFMarker)marker != AMF_MARKER_NULL) {
        // Notice that NULL is treated as an object w/o any fields.
        LOG(ERROR) << "Expected object/null, actually " << marker2str(marker);
        return false;
    }
    if (!msg->IsInitialized()) {
        LOG(ERROR) << "Missing required fields: "
                   << msg->InitializationErrorString();
        return false;
    }
    return true;
}

// [Reading AMFObject]

static bool ReadAMFObjectBody(AMFObject* obj, AMFInputStream* stream);
static bool ReadAMFEcmaArrayBody(AMFObject* obj, AMFInputStream* stream);
static bool ReadAMFArrayBody(AMFArray* arr, AMFInputStream* stream);

static bool ReadAMFObjectField(AMFInputStream* stream,
                               AMFObject* obj,
                               const std::string& name) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    switch ((AMFMarker)marker) {
    case AMF_MARKER_NUMBER: {
        uint64_t val = 0;
        if (stream->cut_u64(&val) != 8u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        double* dptr = (double*)&val;
        obj->SetNumber(name, *dptr);
    } break;
    case AMF_MARKER_BOOLEAN: {
        uint8_t val = 0;
        if (stream->cut_u8(&val) != 1u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        obj->SetBool(name, val);
    } break;
    case AMF_MARKER_STRING: {
        std::string val;
        if (!ReadAMFShortStringBody(&val, stream)) {
            return false;
        }
        obj->SetString(name, val);
    } break;
    case AMF_MARKER_TYPED_OBJECT: {
        std::string class_name;
        if (!ReadAMFShortStringBody(&class_name, stream)) {
            LOG(ERROR) << "Fail to read class_name";
        }
    }
    // fall through
    case AMF_MARKER_OBJECT: {
        if (!ReadAMFObjectBody(obj->MutableObject(name), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_ECMA_ARRAY: {
        if (!ReadAMFEcmaArrayBody(obj->MutableObject(name), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_STRICT_ARRAY: {
        if (!ReadAMFArrayBody(obj->MutableArray(name), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_NULL:
        obj->SetNull(name);
        break;
    case AMF_MARKER_UNDEFINED:
        obj->SetUndefined(name);
        break;
    case AMF_MARKER_UNSUPPORTED:
        obj->SetUnsupported(name);
        break;
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_REFERENCE:
    case AMF_MARKER_DATE:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        LOG(ERROR) << marker2str(marker) << " is not supported yet";
        return false;
    case AMF_MARKER_OBJECT_END:
        CHECK(false) << "object-end shouldn't be present here";
        break;
    case AMF_MARKER_LONG_STRING: {
        std::string val;
        if (!ReadAMFLongStringBody(&val, stream)) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        obj->SetString(name, val);
    } break;
    } // switch
    return true;
}

static bool ReadAMFObjectBody(AMFObject* obj, AMFInputStream* stream) {
    std::string name;
    while (ReadAMFShortStringBody(&name, stream)) {
        if (name.empty()) {
            uint8_t marker;
            if (stream->cut_u8(&marker) != 1u) {
                LOG(ERROR) << "stream is not long enough";
                return false;
            }
            if ((AMFMarker)marker != AMF_MARKER_OBJECT_END) {
                LOG(ERROR) << "marker=" << marker
                           << " after empty name is not object end";
                return false;
            }
            break;
        }
        if (!ReadAMFObjectField(stream, obj, name)) {
            return false;
        }
    }
    return true;
}

static bool ReadAMFEcmaArrayBody(AMFObject* obj, AMFInputStream* stream) {
    uint32_t count = 0;
    if (stream->cut_u32(&count) != 4u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    std::string name;
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadAMFShortStringBody(&name, stream)) {
            LOG(ERROR) << "Fail to read name from the stream";
            return false;
        }
        if (!ReadAMFObjectField(stream, obj, name)) {
            return false;
        }
    }
    return true;
}

bool ReadAMFObject(AMFObject* obj, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_OBJECT) {
        if (!ReadAMFObjectBody(obj, stream)) {
            return false;
        }
    } else if ((AMFMarker)marker == AMF_MARKER_ECMA_ARRAY) {
        if (!ReadAMFEcmaArrayBody(obj, stream)) {
            return false;
        }
    } else if ((AMFMarker)marker != AMF_MARKER_NULL) {
        // NOTE: NULL is treated as an object w/o any fields.
        LOG(ERROR) << "Expected object/null, actually " << marker2str(marker);
        return false;
    }
    return true;
}

static bool ReadAMFArrayItem(AMFInputStream* stream, AMFArray* arr) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    switch ((AMFMarker)marker) {
    case AMF_MARKER_NUMBER: {
        uint64_t val = 0;
        if (stream->cut_u64(&val) != 8u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        double* dptr = (double*)&val;
        arr->AddNumber(*dptr);
    } break;
    case AMF_MARKER_BOOLEAN: {
        uint8_t val = 0;
        if (stream->cut_u8(&val) != 1u) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        arr->AddBool(val);
    } break;
    case AMF_MARKER_STRING: {
        std::string val;
        if (!ReadAMFShortStringBody(&val, stream)) {
            return false;
        }
        arr->AddString(val);
    } break;
    case AMF_MARKER_TYPED_OBJECT: {
        std::string class_name;
        if (!ReadAMFShortStringBody(&class_name, stream)) {
            LOG(ERROR) << "Fail to read class_name";
        }
    }
    // fall through
    case AMF_MARKER_OBJECT: {
        if (!ReadAMFObjectBody(arr->AddObject(), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_ECMA_ARRAY: {
        if (!ReadAMFEcmaArrayBody(arr->AddObject(), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_STRICT_ARRAY: {
        if (!ReadAMFArrayBody(arr->AddArray(), stream)) {
            return false;
        }
    } break;
    case AMF_MARKER_NULL:
        arr->AddNull();
        break;
    case AMF_MARKER_UNDEFINED:
        arr->AddUndefined();
        break;
    case AMF_MARKER_UNSUPPORTED:
        arr->AddUnsupported();
        break;
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_REFERENCE:
    case AMF_MARKER_DATE:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        LOG(ERROR) << marker2str(marker) << " is not supported yet";
        return false;
    case AMF_MARKER_OBJECT_END:
        CHECK(false) << "object-end shouldn't be present here";
        break;
    case AMF_MARKER_LONG_STRING: {
        std::string val;
        if (!ReadAMFLongStringBody(&val, stream)) {
            LOG(ERROR) << "stream is not long enough";
            return false;
        }
        arr->AddString(val);
    } break;
    } // switch
    return true;
}

static bool ReadAMFArrayBody(AMFArray* arr, AMFInputStream* stream) {
    uint32_t count = 0;
    if (stream->cut_u32(&count) != 4u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!ReadAMFArrayItem(stream, arr)) {
            return false;
        }
    }
    return true;
}

bool ReadAMFArray(AMFArray* arr, AMFInputStream* stream) {
    uint8_t marker;
    if (stream->cut_u8(&marker) != 1u) {
        LOG(ERROR) << "stream is not long enough";
        return false;
    }
    if ((AMFMarker)marker == AMF_MARKER_STRICT_ARRAY) {
        if (!ReadAMFArrayBody(arr, stream)) {
            return false;
        }
    } else if ((AMFMarker)marker != AMF_MARKER_NULL) {
        // NOTE: NULL is treated as an array w/o any items.
        LOG(ERROR) << "Expected array/null, actually " << marker2str(marker);
        return false;
    }
    return true;
}

// ======== AMFArray =========

AMFArray::AMFArray() : _size(0) {
}

AMFArray::AMFArray(const AMFArray& rhs) 
    : _size(rhs._size) {
    const size_t inline_size = std::min((size_t)_size, arraysize(_fields));
    for (size_t i = 0; i < inline_size; ++i) {
        _fields[i] = rhs._fields[i];
    }
    if (_size > arraysize(_fields)) {
        _morefields = rhs._morefields;
    }
}

AMFArray& AMFArray::operator=(const AMFArray& rhs) {
    if (_size < rhs._size) {
        this->~AMFArray();
        return *new (this) AMFArray(rhs);
    }
    for (size_t i = 0; i < rhs._size; ++i) {
        (*this)[i] = rhs[i];
    }
    for (size_t i = rhs._size; i < _size; ++i) {
        RemoveLastField();
    }
    return *this;
}

void AMFArray::Clear() {
    const size_t inline_size = std::min((size_t)_size, arraysize(_fields));
    for (size_t i = 0; i < inline_size; ++i) {
        _fields[i].Clear();
    }
    _size = 0;
    _morefields.clear();
}

AMFField* AMFArray::AddField() {
    if (_size < arraysize(_fields)) {
        return &_fields[_size++];
    }
    size_t more_size = _size - arraysize(_fields);
    if (more_size < _morefields.size()) {
        ++_size;
        return &_morefields[more_size];
    }
    _morefields.resize(_morefields.size() + 1);
    ++_size;
    return &_morefields.back();
}

void AMFArray::RemoveLastField() {
    if (_size == 0) {
        return;
    }
    if (_size <= arraysize(_fields)) {
        _fields[--_size].Clear();
        return;
    }
    _morefields.pop_back();
    --_size;
}

// [ Write ]

void WriteAMFString(const butil::StringPiece& str, AMFOutputStream* stream) {
    if (str.size() < 65536u) {
        stream->put_u8(AMF_MARKER_STRING);
        stream->put_u16(str.size());
        stream->putn(str.data(), str.size());
    } else {
        stream->put_u8(AMF_MARKER_LONG_STRING);
        stream->put_u32(str.size());
        stream->putn(str.data(), str.size());
    }
}

void WriteAMFBool(bool val, AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_BOOLEAN);
    stream->put_u8(val);
}

void WriteAMFNumber(double val, AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_NUMBER);
    uint64_t* uptr = (uint64_t*)&val;
    stream->put_u64(*uptr);
}

void WriteAMFUint32(uint32_t val, AMFOutputStream* stream) {
    return WriteAMFNumber((double)val, stream);
}

void WriteAMFNull(AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_NULL);
}

void WriteAMFUndefined(AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_UNDEFINED);
}

void WriteAMFUnsupported(AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_UNSUPPORTED);
}

void WriteAMFObject(const google::protobuf::Message& message,
                    AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_OBJECT);
    const google::protobuf::Descriptor* desc = message.GetDescriptor();
    const google::protobuf::Reflection* reflection = message.GetReflection();
    for (int i = 0; i < desc->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = desc->field(i);
        if (field->is_repeated()) {
            LOG(ERROR) << "Repeated fields are not supported yet";
            return stream->set_bad();
        }
        const bool has_field = reflection->HasField(message, field);
        if (!has_field) {
            if (field->is_required()) {
                LOG(ERROR) << "Missing required field=" << field->full_name();
                return stream->set_bad();
            } else {
                continue;
            }
        }
        const std::string& name = field->name();
        if (name.size() >= 65536u) {
            LOG(ERROR) << "name is too long!";
            return stream->set_bad();
        }
        stream->put_u16(name.size());
        stream->putn(name.data(), name.size());
        switch (field->cpp_type()) {
        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            LOG(ERROR) << "AMF does not have integers";
            return stream->set_bad();
        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: {
            stream->put_u8(AMF_MARKER_NUMBER);
            const double val = reflection->GetDouble(message, field);
            uint64_t* uptr = (uint64_t*)&val;
            stream->put_u64(*uptr);
        } break;
        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            LOG(ERROR) << "AMF does not have float, use double instead";
            return stream->set_bad();
        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL: {
            stream->put_u8(AMF_MARKER_BOOLEAN);
            const bool val = reflection->GetBool(message, field);
            stream->put_u8(val);
        } break;
        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            LOG(ERROR) << "AMF does not have enum";
            return stream->set_bad();
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
            const std::string val = reflection->GetString(message, field);
            if (val.size() < 65536u) {
                stream->put_u8(AMF_MARKER_STRING);
                stream->put_u16(val.size());
                stream->putn(val.data(), val.size());
            } else {
                stream->put_u8(AMF_MARKER_LONG_STRING);
                stream->put_u32(val.size());
                stream->putn(val.data(), val.size());
            }
        } break;
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            WriteAMFObject(reflection->GetMessage(message, field), stream);
            break;
        } // switch
        if (!stream->good()) {
            LOG(ERROR) << "Fail to serialize field=" << field->full_name();
            return;
        }
    }
    stream->put_u16(0);
    stream->put_u8(AMF_MARKER_OBJECT_END);
}

static void WriteAMFField(const AMFField& field, AMFOutputStream* stream) {
    switch (field.type()) {
    case AMF_MARKER_NUMBER: {
        stream->put_u8(AMF_MARKER_NUMBER);
        const double val = field.AsNumber();
        uint64_t* uptr = (uint64_t*)&val;
        stream->put_u64(*uptr);
    } break;
    case AMF_MARKER_BOOLEAN:
        stream->put_u8(AMF_MARKER_BOOLEAN);
        stream->put_u8(field.AsBool());
        break;
    case AMF_MARKER_STRING: {
        stream->put_u8(AMF_MARKER_STRING);
        const butil::StringPiece str = field.AsString();
        stream->put_u16(str.size());
        stream->putn(str.data(), str.size());
    } break;
    case AMF_MARKER_LONG_STRING: {
        stream->put_u8(AMF_MARKER_LONG_STRING);
        const butil::StringPiece str = field.AsString();
        stream->put_u32(str.size());
        stream->putn(str.data(), str.size());
    } break;
    case AMF_MARKER_OBJECT:
    case AMF_MARKER_ECMA_ARRAY:
        WriteAMFObject(field.AsObject(), stream);
        break;
    case AMF_MARKER_STRICT_ARRAY:
        WriteAMFArray(field.AsArray(), stream);
        break;
    case AMF_MARKER_NULL:
        stream->put_u8(AMF_MARKER_NULL);
        break;
    case AMF_MARKER_UNDEFINED:
        stream->put_u8(AMF_MARKER_UNDEFINED);
        break;
    case AMF_MARKER_UNSUPPORTED:
        stream->put_u8(AMF_MARKER_UNSUPPORTED);
        break;
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_REFERENCE:        
    case AMF_MARKER_DATE:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_TYPED_OBJECT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        LOG(ERROR) << marker2str(field.type()) << " is not supported yet";
        break;
    case AMF_MARKER_OBJECT_END:
        CHECK(false) << "object-end shouldn't be present here";
        break;
    } // switch
}

void WriteAMFObject(const AMFObject& obj, AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_OBJECT);
    for (AMFObject::const_iterator it = obj.begin(); it != obj.end(); ++it) {
        const std::string& name = it->first;
        if (name.size() >= 65536u) {
            LOG(ERROR) << "name is too long!";
            return stream->set_bad();
        }
        stream->put_u16(name.size());
        stream->putn(name.data(), name.size());
        WriteAMFField(it->second, stream);
        if (!stream->good()) {
            LOG(ERROR) << "Fail to serialize field=" << name;
            return;
        }
    }
    stream->put_u16(0);
    stream->put_u8(AMF_MARKER_OBJECT_END);
}

void WriteAMFArray(const AMFArray& arr, AMFOutputStream* stream) {
    stream->put_u8(AMF_MARKER_STRICT_ARRAY);
    stream->put_u32(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        WriteAMFField(arr[i], stream);
        if (!stream->good()) {
            LOG(ERROR) << "Fail to serialize item[" << i << ']';
            return;
        }
    }
}

std::ostream& operator<<(std::ostream& os, const AMFField& field) {
    switch (field.type()) {
    case AMF_MARKER_NUMBER:
        return os << field.AsNumber();
    case AMF_MARKER_BOOLEAN:
        return os << (field.AsBool() ? "true" : "false");
    case AMF_MARKER_NULL:
        return os << "null";
    case AMF_MARKER_UNDEFINED:
        return os << "undefined";
    case AMF_MARKER_UNSUPPORTED:
        return os << "unsupported";
    case AMF_MARKER_MOVIECLIP:
    case AMF_MARKER_REFERENCE:
    case AMF_MARKER_OBJECT_END:
    case AMF_MARKER_DATE:
    case AMF_MARKER_RECORDSET:
    case AMF_MARKER_XML_DOCUMENT:
    case AMF_MARKER_TYPED_OBJECT:
    case AMF_MARKER_AVMPLUS_OBJECT:
        return os << marker2str(field.type());
    case AMF_MARKER_STRING:
    case AMF_MARKER_LONG_STRING:
        return os << '"' << field.AsString() << '"';
    case AMF_MARKER_OBJECT:
    case AMF_MARKER_ECMA_ARRAY:
        return os << field.AsObject();
    case AMF_MARKER_STRICT_ARRAY:
        return os << field.AsArray();
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const AMFObject& obj) {
    bool first = true;
    os << "AMFObject{";
    for (AMFObject::const_iterator it = obj.begin(); it != obj.end(); ++it) {
        if (!first) {
            os << ' ';
        } else {
            first = false;
        }        
        os << it->first << '=' << it->second;
    }
    return os << '}';
}

std::ostream& operator<<(std::ostream& os, const AMFArray& arr) {
    bool first = true;
    os << "AMFArray[";
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i >= 512) {
            os << "...<skip " << arr.size() - i << " items>";
            break;
        }
        if (!first) {
            os << ' ';
        } else {
            first = false;
        }        
        os << arr[i];
    }
    return os << ']';
}

} // namespace brpc
