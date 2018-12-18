// Copyright (c) 2014 Baidu, Inc.

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <sys/time.h>
#include <time.h>
#include <google/protobuf/descriptor.h>
#include "butil/base64.h"
#include "zero_copy_stream_writer.h"
#include "encode_decode.h"
#include "protobuf_map.h"
#include "rapidjson.h"
#include "pb_to_json.h"

namespace json2pb {
Pb2JsonOptions::Pb2JsonOptions()
    : enum_option(OUTPUT_ENUM_BY_NAME)
    , pretty_json(false)
    , enable_protobuf_map(true)
#ifdef BAIDU_INTERNAL
    , bytes_to_base64(false)
#else
    , bytes_to_base64(true)
#endif
    , jsonify_empty_array(false)
    , always_print_primitive_fields(false) {
}

class PbToJsonConverter {
public:
    explicit PbToJsonConverter(const Pb2JsonOptions& opt) : _option(opt) {}

    template <typename Handler>
    bool Convert(const google::protobuf::Message& message, Handler& handler);

    const std::string& ErrorText() const { return _error; }
    
private:
    template <typename Handler>
    bool _PbFieldToJson(const google::protobuf::Message& message,
                        const google::protobuf::FieldDescriptor* field,
                        Handler& handler);

    std::string _error;
    Pb2JsonOptions _option;
};

template <typename Handler>
bool PbToJsonConverter::Convert(const google::protobuf::Message& message, Handler& handler) {
    handler.StartObject();
    const google::protobuf::Reflection* reflection = message.GetReflection();
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    
    int ext_range_count = descriptor->extension_range_count();
    int field_count = descriptor->field_count();
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    fields.reserve(64);
    for (int i = 0; i < ext_range_count; ++i) {
        const google::protobuf::Descriptor::ExtensionRange*
            ext_range = descriptor->extension_range(i);
        for (int tag_number = ext_range->start;
             tag_number < ext_range->end; ++tag_number) {
            const google::protobuf::FieldDescriptor* field =
                    reflection->FindKnownExtensionByNumber(tag_number);
            if (field) {
                fields.push_back(field);
            }
        }
    }
    std::vector<const google::protobuf::FieldDescriptor*> map_fields;
    for (int i = 0; i < field_count; ++i) {
        const google::protobuf::FieldDescriptor* field = descriptor->field(i);
        if (_option.enable_protobuf_map && json2pb::IsProtobufMap(field)) {
            map_fields.push_back(field);
        } else {
            fields.push_back(field);
        }
    }

    // Fill in non-map fields
    std::string field_name_str;
    for (size_t i = 0; i < fields.size(); ++i) {
        const google::protobuf::FieldDescriptor* field = fields[i];
        if (!field->is_repeated() && !reflection->HasField(message, field)) {
            // Field that has not been set
            if (field->is_required()) {
                _error = "Missing required field: " + field->full_name();
                return false;
            }
            // Whether dumps default fields
            if (!_option.always_print_primitive_fields) {
                continue;
            }
        } else if (field->is_repeated()
                   && reflection->FieldSize(message, field) == 0
                   && !_option.jsonify_empty_array) {
            // Repeated field that has no entry
            continue;
        }

        const std::string& orig_name = field->name();
        bool decoded = decode_name(orig_name, field_name_str); 
        const std::string& name = decoded ? field_name_str : orig_name;
        handler.Key(name.data(), name.size(), false);
        if (!_PbFieldToJson(message, field, handler)) {
            return false;
        }
    }

    // Fill in map fields
    for (size_t i = 0; i < map_fields.size(); ++i) {
        const google::protobuf::FieldDescriptor* map_desc = map_fields[i];
        const google::protobuf::FieldDescriptor* key_desc =
                map_desc->message_type()->field(json2pb::KEY_INDEX);
        const google::protobuf::FieldDescriptor* value_desc =
                map_desc->message_type()->field(json2pb::VALUE_INDEX);

        // Write a json object corresponding to hold protobuf map
        // such as {"key": value, ...}
        const std::string& orig_name = map_desc->name();
        bool decoded = decode_name(orig_name, field_name_str);
        const std::string& name = decoded ? field_name_str : orig_name;
        handler.Key(name.data(), name.size(), false);
        handler.StartObject();
        std::string entry_name;
        for (int j = 0; j < reflection->FieldSize(message, map_desc); ++j) {
            const google::protobuf::Message& entry =
                    reflection->GetRepeatedMessage(message, map_desc, j);
            const google::protobuf::Reflection* entry_reflection = entry.GetReflection();
            entry_name = entry_reflection->GetStringReference(
                entry, key_desc, &entry_name);
            handler.Key(entry_name.data(), entry_name.size(), false);

            // Fill in entries into this json object
            if (!_PbFieldToJson(entry, value_desc, handler)) {
                return false;
            }
        }
        // Hack: Pass 0 as parameter since Writer doesn't care this
        handler.EndObject(0);
    }
    // Hack: Pass 0 as parameter since Writer doesn't care this
    handler.EndObject(0);
    return true;
}

template <typename Handler>
bool PbToJsonConverter::_PbFieldToJson(
    const google::protobuf::Message& message,
    const google::protobuf::FieldDescriptor* field,
    Handler& handler) {
    const google::protobuf::Reflection* reflection = message.GetReflection();
    switch (field->cpp_type()) {
#define CASE_FIELD_TYPE(cpptype, method, valuetype, handle)             \
    case google::protobuf::FieldDescriptor::CPPTYPE_##cpptype: {                          \
        if (field->is_repeated()) {                                     \
            int field_size = reflection->FieldSize(message, field);     \
            handler.StartArray();                                       \
            for (int index = 0; index < field_size; ++index) {          \
                handler.handle(static_cast<valuetype>(                  \
                    reflection->GetRepeated##method(                    \
                        message, field, index)));                       \
            }                                                           \
            handler.EndArray(field_size);                               \
                                                                        \
        } else {                                                        \
            handler.handle(static_cast<valuetype>(                      \
                reflection->Get##method(message, field)));              \
        }                                                               \
        break;                                                          \
    }
        
    CASE_FIELD_TYPE(BOOL,   Bool,   bool,         Bool);
    CASE_FIELD_TYPE(INT32,  Int32,  int,          AddInt);
    CASE_FIELD_TYPE(UINT32, UInt32, unsigned int, AddUint);
    CASE_FIELD_TYPE(INT64,  Int64,  int64_t,      AddInt64);
    CASE_FIELD_TYPE(UINT64, UInt64, uint64_t,     AddUint64);
    CASE_FIELD_TYPE(FLOAT,  Float,  double,       Double);
    CASE_FIELD_TYPE(DOUBLE, Double, double,       Double);
#undef CASE_FIELD_TYPE

    case google::protobuf::FieldDescriptor::CPPTYPE_STRING: {
        std::string value;
        if (field->is_repeated()) {
            int field_size = reflection->FieldSize(message, field);
            handler.StartArray();
            for (int index = 0; index < field_size; ++index) {
                value = reflection->GetRepeatedStringReference(
                    message, field, index, &value);
                if (field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES
                    && _option.bytes_to_base64) {
                    std::string value_decoded;
                    butil::Base64Encode(value, &value_decoded);
                    handler.String(value_decoded.data(), value_decoded.size(), false);
                } else {
                    handler.String(value.data(), value.size(), false);
                }
            }
            handler.EndArray(field_size);
            
        } else {
            value = reflection->GetStringReference(message, field, &value);
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES
                && _option.bytes_to_base64) {
                std::string value_decoded;
                butil::Base64Encode(value, &value_decoded);
                handler.String(value_decoded.data(), value_decoded.size(), false);
            } else {
                handler.String(value.data(), value.size(), false);
            }
        }
        break;
    }

    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
        if (field->is_repeated()) {
            int field_size = reflection->FieldSize(message, field);
            handler.StartArray();
            if (_option.enum_option == OUTPUT_ENUM_BY_NAME) {
                for (int index = 0; index < field_size; ++index) { 
                    const std::string& enum_name = reflection->GetRepeatedEnum(
                        message, field, index)->name();
                    handler.String(enum_name.data(), enum_name.size(), false);
                }
            } else {
                for (int index = 0; index < field_size; ++index) { 
                    handler.AddInt(reflection->GetRepeatedEnum(
                        message, field, index)->number());
                }
            }
            handler.EndArray();
            
        } else {
            if (_option.enum_option == OUTPUT_ENUM_BY_NAME) {
                const std::string& enum_name =
                        reflection->GetEnum(message, field)->name();
                handler.String(enum_name.data(), enum_name.size(), false);
            } else {
                handler.AddInt(reflection->GetEnum(message, field)->number());
            }
        }
        break;
    }

    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
        if (field->is_repeated()) {
            int field_size = reflection->FieldSize(message, field);
            handler.StartArray();
            for (int index = 0; index < field_size; ++index) {
                if (!Convert(reflection->GetRepeatedMessage(
                        message, field, index), handler)) {
                    return false;
                }
            }
            handler.EndArray(field_size);
            
        } else {
            if (!Convert(reflection->GetMessage(message, field), handler)) {
                return false;
            }
        }
        break;
    }
    }
    return true;
}

template <typename OutputStream>
bool ProtoMessageToJsonStream(const google::protobuf::Message& message,
                              const Pb2JsonOptions& options,
                              OutputStream& os, std::string* error) {
    PbToJsonConverter converter(options);
    bool succ = false;
    if (options.pretty_json) {    
        BUTIL_RAPIDJSON_NAMESPACE::PrettyWriter<OutputStream> writer(os);
        succ = converter.Convert(message, writer); 
    } else {
        BUTIL_RAPIDJSON_NAMESPACE::OptimizedWriter<OutputStream> writer(os);
        succ = converter.Convert(message, writer); 
    }
    if (!succ && error) {
        error->clear();
        error->append(converter.ErrorText());
    }
    return succ;
}

bool ProtoMessageToJson(const google::protobuf::Message& message,
                        std::string* json,
                        const Pb2JsonOptions& options,
                        std::string* error) {
    // TODO(gejun): We could further wrap a std::string as a buffer to reduce
    // a copying.
    BUTIL_RAPIDJSON_NAMESPACE::StringBuffer buffer;
    if (json2pb::ProtoMessageToJsonStream(message, options, buffer, error)) {
        json->append(buffer.GetString(), buffer.GetSize());
        return true;
    }
    return false;
}

bool ProtoMessageToJson(const google::protobuf::Message& message,
                        std::string* json, std::string* error) {
    return ProtoMessageToJson(message, json, Pb2JsonOptions(), error);
}

bool ProtoMessageToJson(const google::protobuf::Message& message,
                        google::protobuf::io::ZeroCopyOutputStream *stream,
                        const Pb2JsonOptions& options, std::string* error) {
    json2pb::ZeroCopyStreamWriter wrapper(stream);
    return json2pb::ProtoMessageToJsonStream(message, options, wrapper, error);
}

bool ProtoMessageToJson(const google::protobuf::Message& message,
                        google::protobuf::io::ZeroCopyOutputStream *stream,
                        std::string* error) {
    return ProtoMessageToJson(message, stream, Pb2JsonOptions(), error);
}
} // namespace json2pb
