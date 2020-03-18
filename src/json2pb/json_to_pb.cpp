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

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <sys/time.h>
#include <time.h>
#include <typeinfo>
#include <limits> 
#include <google/protobuf/descriptor.h>
#include "json_to_pb.h"
#include "zero_copy_stream_reader.h"       // ZeroCopyStreamReader
#include "encode_decode.h"
#include "butil/base64.h"
#include "butil/string_printf.h"
#include "protobuf_map.h"
#include "rapidjson.h"

#define J2PERROR(perr, fmt, ...)                                        \
    if (perr) {                                                         \
        if (!perr->empty()) {                                           \
            perr->append(", ", 2);                                      \
        }                                                               \
        butil::string_appendf(perr, fmt, ##__VA_ARGS__);                 \
    } else { }

namespace json2pb {

Json2PbOptions::Json2PbOptions()
#ifdef BAIDU_INTERNAL
    : base64_to_bytes(false) {
#else
    : base64_to_bytes(true) {
#endif
}

enum MatchType { 
    TYPE_MATCH = 0x00, 
    REQUIRED_OR_REPEATED_TYPE_MISMATCH = 0x01, 
    OPTIONAL_TYPE_MISMATCH = 0x02 
};
 
static void string_append_value(const BUTIL_RAPIDJSON_NAMESPACE::Value& value,
                                std::string* output) {
    if (value.IsNull()) {
        output->append("null");
    } else if (value.IsBool()) {
        output->append(value.GetBool() ? "true" : "false");
    } else if (value.IsInt()) {
        butil::string_appendf(output, "%d", value.GetInt());
    } else if (value.IsUint()) {
        butil::string_appendf(output, "%u", value.GetUint());
    } else if (value.IsInt64()) {
        butil::string_appendf(output, "%" PRId64, value.GetInt64());
    } else if (value.IsUint64()) {
        butil::string_appendf(output, "%" PRIu64, value.GetUint64());
    } else if (value.IsDouble()) {
        butil::string_appendf(output, "%f", value.GetDouble());
    } else if (value.IsString()) {
        output->push_back('"');
        output->append(value.GetString(), value.GetStringLength());
        output->push_back('"');
    } else if (value.IsArray()) {
        output->append("array");
    } else if (value.IsObject()) {
        output->append("object");
    }
}

//It will be called when type mismatch occurs, fg: convert string to uint, 
//and will also be called when invalid value appears, fg: invalid enum name,
//invalid enum number, invalid string content to convert to double or float.
//for optional field error will just append error into error message
//and ends with ',' and return true.
//otherwise will append error into error message and return false.
inline bool value_invalid(const google::protobuf::FieldDescriptor* field, const char* type,
                          const BUTIL_RAPIDJSON_NAMESPACE::Value& value, std::string* err) {
    bool optional = field->is_optional();
    if (err) {
        if (!err->empty()) {
            err->append(", ");
        }
        err->append("Invalid value `");
        string_append_value(value, err);
        butil::string_appendf(err, "' for %sfield `%s' which SHOULD be %s",
                       optional ? "optional " : "",
                       field->full_name().c_str(), type);
    }
    if (!optional) {
        return false;                                           
    } 
    return true;
}

template<typename T>
inline bool convert_string_to_double_float_type(
    void (google::protobuf::Reflection::*func)(
        google::protobuf::Message* message,
        const google::protobuf::FieldDescriptor* field, T value) const,
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field, 
    const google::protobuf::Reflection* reflection,
    const BUTIL_RAPIDJSON_NAMESPACE::Value& item,
    std::string* err) {
    const char* limit_type = item.GetString();  // MUST be string here 
    if (std::numeric_limits<T>::has_quiet_NaN &&
        strcasecmp(limit_type, "NaN") == 0) { 
        (reflection->*func)(message, field, std::numeric_limits<T>::quiet_NaN());
        return true;
    } 
    if (std::numeric_limits<T>::has_infinity &&
        strcasecmp(limit_type, "Infinity") == 0) {
        (reflection->*func)(message, field, std::numeric_limits<T>::infinity());
        return true;
    } 
    if (std::numeric_limits<T>::has_infinity &&
        strcasecmp(limit_type, "-Infinity") == 0) { 
        (reflection->*func)(message, field, -std::numeric_limits<T>::infinity());
        return true;
    }
    return value_invalid(field, typeid(T).name(), item, err);
}

inline bool convert_float_type(const BUTIL_RAPIDJSON_NAMESPACE::Value& item, bool repeated,
                               google::protobuf::Message* message,
                               const google::protobuf::FieldDescriptor* field, 
                               const google::protobuf::Reflection* reflection,
                               std::string* err) { 
    if (item.IsNumber()) {
        if (repeated) {
            reflection->AddFloat(message, field, item.GetDouble());
        } else {
            reflection->SetFloat(message, field, item.GetDouble());
        }
    } else if (item.IsString()) {
        if (!convert_string_to_double_float_type(
                (repeated ? &google::protobuf::Reflection::AddFloat
                 : &google::protobuf::Reflection::SetFloat),
                message, field, reflection, item, err)) { 
            return false;
        }                                                                               
    } else {                                         
        return value_invalid(field, "float", item, err);
    } 
    return true;
}

inline bool convert_double_type(const BUTIL_RAPIDJSON_NAMESPACE::Value& item, bool repeated,
                                google::protobuf::Message* message,
                                const google::protobuf::FieldDescriptor* field, 
                                const google::protobuf::Reflection* reflection,
                                std::string* err) {  
    if (item.IsNumber()) {
        if (repeated) {
            reflection->AddDouble(message, field, item.GetDouble());
        } else {
            reflection->SetDouble(message, field, item.GetDouble());
        }
    } else if (item.IsString()) {
        if (!convert_string_to_double_float_type(
                (repeated ? &google::protobuf::Reflection::AddDouble
                 : &google::protobuf::Reflection::SetDouble),
                message, field, reflection, item, err)) { 
            return false; 
        }
    } else {
        return value_invalid(field, "double", item, err); 
    }
    return true;
}

inline bool convert_enum_type(const BUTIL_RAPIDJSON_NAMESPACE::Value&item, bool repeated,
                              google::protobuf::Message* message,
                              const google::protobuf::FieldDescriptor* field,
                              const google::protobuf::Reflection* reflection,
                              std::string* err) {
    const google::protobuf::EnumValueDescriptor * enum_value_descriptor = NULL; 
    if (item.IsInt()) {
        enum_value_descriptor = field->enum_type()->FindValueByNumber(item.GetInt()); 
    } else if (item.IsString()) {                                          
        enum_value_descriptor = field->enum_type()->FindValueByName(item.GetString()); 
    }                                                                      
    if (!enum_value_descriptor) {                                      
        return value_invalid(field, "enum", item, err); 
    }                                                                  
    if (repeated) {
        reflection->AddEnum(message, field, enum_value_descriptor);
    } else {
        reflection->SetEnum(message, field, enum_value_descriptor);
    }
    return true;
}

bool JsonValueToProtoMessage(const BUTIL_RAPIDJSON_NAMESPACE::Value& json_value,
                             google::protobuf::Message* message,
                             const Json2PbOptions& options,
                             std::string* err);

//Json value to protobuf convert rules for type:
//Json value type                 Protobuf type                convert rules
//int                             int uint int64 uint64        valid convert is available
//uint                            int uint int64 uint64        valid convert is available
//int64                           int uint int64 uint64        valid convert is available
//uint64                          int uint int64 uint64        valid convert is available
//int uint int64 uint64           float double                 available
//"NaN" "Infinity" "-Infinity"    float double                 only "NaN" "Infinity" "-Infinity" is available    
//int                             enum                         valid enum number value is available
//string                          enum                         valid enum name value is available         
//other mismatch type convertion will be regarded as error.
#define J2PCHECKTYPE(value, cpptype, jsontype) ({                   \
            MatchType match_type = TYPE_MATCH;                      \
            if (!value.Is##jsontype()) {                            \
                match_type = OPTIONAL_TYPE_MISMATCH;                \
                if (!value_invalid(field, #cpptype, value, err)) {  \
                    return false;                                   \
                }                                                   \
            }                                                       \
            match_type;                                             \
        })

static bool JsonValueToProtoField(const BUTIL_RAPIDJSON_NAMESPACE::Value& value,
                                  const google::protobuf::FieldDescriptor* field,
                                  google::protobuf::Message* message,
                                  const Json2PbOptions& options,
                                  std::string* err) {
    if (value.IsNull()) {
        if (field->is_required()) {
            J2PERROR(err, "Missing required field: %s", field->full_name().c_str());
            return false;
        }
        return true;
    }
        
    if (field->is_repeated()) {
        if (!value.IsArray()) {
            J2PERROR(err, "Invalid value for repeated field: %s",
                     field->full_name().c_str());
            return false;
        }
    } 

    const google::protobuf::Reflection* reflection = message->GetReflection();
    switch (field->cpp_type()) {
#define CASE_FIELD_TYPE(cpptype, method, jsontype)                      \
        case google::protobuf::FieldDescriptor::CPPTYPE_##cpptype: {                      \
            if (field->is_repeated()) {                                 \
                const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();          \
                for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) { \
                    const BUTIL_RAPIDJSON_NAMESPACE::Value & item = value[index];       \
                    if (TYPE_MATCH == J2PCHECKTYPE(item, cpptype, jsontype)) { \
                        reflection->Add##method(message, field, item.Get##jsontype()); \
                    }                                                   \
                }                                                       \
            } else if (TYPE_MATCH == J2PCHECKTYPE(value, cpptype, jsontype)) { \
                reflection->Set##method(message, field, value.Get##jsontype()); \
            }                                                           \
            break;                                                      \
        }                                                           
        CASE_FIELD_TYPE(INT32,  Int32,  Int);
        CASE_FIELD_TYPE(UINT32, UInt32, Uint);
        CASE_FIELD_TYPE(BOOL,   Bool,   Bool);
        CASE_FIELD_TYPE(INT64,  Int64,  Int64);
        CASE_FIELD_TYPE(UINT64, UInt64, Uint64);
#undef CASE_FIELD_TYPE

    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:  
        if (field->is_repeated()) {
            const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();
            for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) {
                const BUTIL_RAPIDJSON_NAMESPACE::Value & item = value[index];
                if (!convert_float_type(item, true, message, field,
                                        reflection, err)) {
                    return false;
                }
            }
        } else if (!convert_float_type(value, false, message, field,
                                       reflection, err)) {
            return false;
        }
        break;

    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE: 
        if (field->is_repeated()) {
            const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();
            for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) {
                const BUTIL_RAPIDJSON_NAMESPACE::Value & item = value[index];
                if (!convert_double_type(item, true, message, field,
                                         reflection, err)) {
                    return false;
                }
            }
        } else if (!convert_double_type(value, false, message, field,
                                        reflection, err)) {
            return false;
        }
        break;
        
    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        if (field->is_repeated()) {
            const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();
            for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) {
                const BUTIL_RAPIDJSON_NAMESPACE::Value & item = value[index];
                if (TYPE_MATCH == J2PCHECKTYPE(item, string, String)) { 
                    std::string str(item.GetString(), item.GetStringLength());
                    if (field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES &&
                        options.base64_to_bytes) {
                        std::string str_decoded;
                        if (!butil::Base64Decode(str, &str_decoded)) {
                            J2PERROR(err, "Fail to decode base64 string=%s", str.c_str());
                            return false;
                        }
                        str = str_decoded;
                    }
                    reflection->AddString(message, field, str);
                }  
            }
        } else if (TYPE_MATCH == J2PCHECKTYPE(value, string, String)) {
            std::string str(value.GetString(), value.GetStringLength());
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES &&
                options.base64_to_bytes) {
                std::string str_decoded;
                if (!butil::Base64Decode(str, &str_decoded)) {
                    J2PERROR(err, "Fail to decode base64 string=%s", str.c_str());
                    return false;
                }
                str = str_decoded;
            }
            reflection->SetString(message, field, str);
        }
        break;

    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        if (field->is_repeated()) {
            const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();
            for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) {
                const BUTIL_RAPIDJSON_NAMESPACE::Value & item = value[index];
                if (!convert_enum_type(item, true, message, field,
                                       reflection, err)) {
                    return false;
                }
            }
        } else if (!convert_enum_type(value, false, message, field,
                                      reflection, err)) {
            return false;
        }
        break;
        
    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
        if (field->is_repeated()) {
            const BUTIL_RAPIDJSON_NAMESPACE::SizeType size = value.Size();
            for (BUTIL_RAPIDJSON_NAMESPACE::SizeType index = 0; index < size; ++index) {
                const BUTIL_RAPIDJSON_NAMESPACE::Value& item = value[index];
                if (TYPE_MATCH == J2PCHECKTYPE(item, message, Object)) { 
                    if (!JsonValueToProtoMessage(
                            item, reflection->AddMessage(message, field), options, err)) {
                        return false;
                    }
                } 
            }
        } else if (!JsonValueToProtoMessage(
            value, reflection->MutableMessage(message, field), options, err)) {
            return false;
        }
        break;
    }
    return true;
}

bool JsonMapToProtoMap(const BUTIL_RAPIDJSON_NAMESPACE::Value& value,
                       const google::protobuf::FieldDescriptor* map_desc,
                       google::protobuf::Message* message,
                       const Json2PbOptions& options,
                       std::string* err) {
    if (!value.IsObject()) {
        J2PERROR(err, "Non-object value for map field: %s",
                 map_desc->full_name().c_str());
        return false;
    }

    const google::protobuf::Reflection* reflection = message->GetReflection();
    const google::protobuf::FieldDescriptor* key_desc =
            map_desc->message_type()->FindFieldByName(KEY_NAME);
    const google::protobuf::FieldDescriptor* value_desc =
            map_desc->message_type()->FindFieldByName(VALUE_NAME);

    for (BUTIL_RAPIDJSON_NAMESPACE::Value::ConstMemberIterator it =
                 value.MemberBegin(); it != value.MemberEnd(); ++it) {
        google::protobuf::Message* entry = reflection->AddMessage(message, map_desc);
        const google::protobuf::Reflection* entry_reflection = entry->GetReflection();
        entry_reflection->SetString(
            entry, key_desc, std::string(it->name.GetString(),
                                         it->name.GetStringLength()));
        if (!JsonValueToProtoField(it->value, value_desc, entry, options, err)) {
            return false;
        }
    }
    return true;
}

bool JsonValueToProtoMessage(const BUTIL_RAPIDJSON_NAMESPACE::Value& json_value,
                             google::protobuf::Message* message,
                             const Json2PbOptions& options,
                             std::string* err) {
    const google::protobuf::Descriptor* descriptor = message->GetDescriptor();
    if (!json_value.IsObject()) {
        J2PERROR(err, "`json_value' is not a json object. %s", descriptor->name().c_str());
        return false;
    }

    const google::protobuf::Reflection* reflection = message->GetReflection();
    
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    fields.reserve(64);
    for (int i = 0; i < descriptor->extension_range_count(); ++i) {
        const google::protobuf::Descriptor::ExtensionRange*
            ext_range = descriptor->extension_range(i);
        for (int tag_number = ext_range->start; tag_number < ext_range->end;
             ++tag_number) {
            const google::protobuf::FieldDescriptor* field =
                reflection->FindKnownExtensionByNumber(tag_number);
            if (field) {
                fields.push_back(field);
            }
        }
    }
    for (int i = 0; i < descriptor->field_count(); ++i) {
        fields.push_back(descriptor->field(i));
    }

    std::string field_name_str_temp; 
    const BUTIL_RAPIDJSON_NAMESPACE::Value* value_ptr = NULL;
    for (size_t i = 0; i < fields.size(); ++i) {
        const google::protobuf::FieldDescriptor* field = fields[i];
        
        const std::string& orig_name = field->name();
        bool res = decode_name(orig_name, field_name_str_temp); 
        const std::string& field_name_str = (res ? field_name_str_temp : orig_name);

#ifndef RAPIDJSON_VERSION_0_1
        BUTIL_RAPIDJSON_NAMESPACE::Value::ConstMemberIterator member =
                json_value.FindMember(field_name_str.data());
        if (member == json_value.MemberEnd()) {
            if (field->is_required()) {
                J2PERROR(err, "Missing required field: %s", field->full_name().c_str());
                return false;
            }
            continue; 
        }
        value_ptr = &(member->value);
#else 
        const BUTIL_RAPIDJSON_NAMESPACE::Value::Member* member =
                json_value.FindMember(field_name_str.data());
        if (member == NULL) {
            if (field->is_required()) {
                J2PERROR(err, "Missing required field: %s", field->full_name().c_str());
                return false;
            }
            continue; 
        }
        value_ptr = &(member->value);
#endif

        if (IsProtobufMap(field) && value_ptr->IsObject()) {
            // Try to parse json like {"key":value, ...} into protobuf map
            if (!JsonMapToProtoMap(*value_ptr, field, message, options, err)) {
                return false;
            }
        } else {
            if (!JsonValueToProtoField(*value_ptr, field, message, options, err)) {
                return false;
            }
        }
    }
    return true;
}

bool ZeroCopyStreamToJson(BUTIL_RAPIDJSON_NAMESPACE::Document *dest, 
                          google::protobuf::io::ZeroCopyInputStream *stream) {
    ZeroCopyStreamReader stream_reader(stream);
    dest->ParseStream<0, BUTIL_RAPIDJSON_NAMESPACE::UTF8<> >(stream_reader);
    return !dest->HasParseError();
}

inline bool JsonToProtoMessageInline(const std::string& json_string, 
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error) {
    if (error) {
        error->clear();
    }
    BUTIL_RAPIDJSON_NAMESPACE::Document d;
    d.Parse<0>(json_string.c_str());
    if (d.HasParseError()) {
        J2PERROR(error, "Invalid json format");
        return false;
    }
    return json2pb::JsonValueToProtoMessage(d, message, options, error);
}

bool JsonToProtoMessage(const std::string& json_string,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error) {
    return JsonToProtoMessageInline(json_string, message, options, error);
}

bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream* stream,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error) {
    if (error) {
        error->clear();
    }
    BUTIL_RAPIDJSON_NAMESPACE::Document d;
    if (!json2pb::ZeroCopyStreamToJson(&d, stream)) {
        J2PERROR(error, "Invalid json format");
        return false;
    }
    return json2pb::JsonValueToProtoMessage(d, message, options, error);
}

bool JsonToProtoMessage(const std::string& json_string, 
                        google::protobuf::Message* message,
                        std::string* error) {
    return JsonToProtoMessageInline(json_string, message, Json2PbOptions(), error);
}

// For ABI compatibility with 1.0.0.0
// (https://svn.baidu.com/public/tags/protobuf-json/protobuf-json_1-0-0-0_PD_BL)
// This method should not be exposed in header, otherwise calls to
// JsonToProtoMessage will be ambiguous.
bool JsonToProtoMessage(std::string json_string, 
                        google::protobuf::Message* message,
                        std::string* error) {
    return JsonToProtoMessageInline(json_string, message, Json2PbOptions(), error);
}

bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream *stream,
                        google::protobuf::Message* message,
                        std::string* error) {
    if (error) {
        error->clear();
    }
    BUTIL_RAPIDJSON_NAMESPACE::Document d;
    if (!json2pb::ZeroCopyStreamToJson(&d, stream)) {
        J2PERROR(error, "Invalid json format");
        return false;
    }
    return json2pb::JsonValueToProtoMessage(d, message, Json2PbOptions(), error);
}
} //namespace json2pb

#undef J2PERROR
#undef J2PCHECKTYPE
