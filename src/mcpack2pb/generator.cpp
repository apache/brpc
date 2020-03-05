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

#include <set>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include "butil/string_printf.h"
#include "butil/file_util.h"
#include "mcpack2pb/mcpack2pb.h"
#include "idl_options.pb.h"

namespace mcpack2pb {

const std::string& get_idl_name(const google::protobuf::FieldDescriptor* f) {
    const std::string& real_name = f->options().GetExtension(idl_name);
    return real_name.empty() ? f->name() : real_name;
}

bool is_integral_type(ConvertibleIdlType type) {
    switch (type) {
    case IDL_INT8:
    case IDL_INT16:
    case IDL_INT32:
    case IDL_INT64:
    case IDL_UINT8:
    case IDL_UINT16:
    case IDL_UINT32:
    case IDL_UINT64:
        return true;
    default:
        return false;
    }
}

const char* field_to_string(const google::protobuf::FieldDescriptor* f) {
    switch (f->type()) {
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:   return "double";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:    return "float";
    case google::protobuf::FieldDescriptor::TYPE_INT64:    return "int64";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:   return "uint64";
    case google::protobuf::FieldDescriptor::TYPE_INT32:    return "int32";
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:  return "fixed64";
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:  return "fixed32";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:     return "bool";
    case google::protobuf::FieldDescriptor::TYPE_STRING:   return "string";
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
        return f->message_type()->name().c_str();
    case google::protobuf::FieldDescriptor::TYPE_BYTES:    return "bytes";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:   return "uint32";
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
        return f->enum_type()->name().c_str();
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32: return "sfixed32";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64: return "sfixed64";
    case google::protobuf::FieldDescriptor::TYPE_SINT32:   return "sint32";
    case google::protobuf::FieldDescriptor::TYPE_SINT64:   return "sint64";
    }
    return "unknown_protobuf_type";
}

const char* to_mcpack_typestr(const google::protobuf::FieldDescriptor* f) {
    switch (f->type()) {
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:   return "double";
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:    return "float";
    case google::protobuf::FieldDescriptor::TYPE_INT64:    return "int64";
    case google::protobuf::FieldDescriptor::TYPE_UINT64:   return "uint64";
    case google::protobuf::FieldDescriptor::TYPE_INT32:    return "int32";
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:  return "uint64";
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:  return "uint32";
    case google::protobuf::FieldDescriptor::TYPE_BOOL:     return "bool";
    case google::protobuf::FieldDescriptor::TYPE_STRING:   return "string";
    case google::protobuf::FieldDescriptor::TYPE_GROUP:    return "object";
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:  return "object";
    case google::protobuf::FieldDescriptor::TYPE_BYTES:    return "binary";
    case google::protobuf::FieldDescriptor::TYPE_UINT32:   return "uint32";
    case google::protobuf::FieldDescriptor::TYPE_ENUM:     return "int32";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32: return "int32";
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64: return "int64";
    case google::protobuf::FieldDescriptor::TYPE_SINT32:   return "int32";
    case google::protobuf::FieldDescriptor::TYPE_SINT64:   return "int64";
    }
    return "unknown_protobuf_type";
}

const char* to_mcpack_typestr(ConvertibleIdlType type,
                              const google::protobuf::FieldDescriptor* f) {
    switch (type) {
    case IDL_AUTO:   return to_mcpack_typestr(f);
    case IDL_INT8:   return "int8";
    case IDL_INT16:  return "int16";
    case IDL_INT32:  return "int32";
    case IDL_INT64:  return "int64";
    case IDL_UINT8:  return "uint8";
    case IDL_UINT16: return "uint16";
    case IDL_UINT32: return "uint32";
    case IDL_UINT64: return "uint64";
    case IDL_BOOL:   return "bool";
    case IDL_FLOAT:  return "float";
    case IDL_DOUBLE: return "double";
    case IDL_BINARY: return "binary";
    case IDL_STRING: return "string";
    }
    return "unknown";
}

const char* to_mcpack_typestr_uppercase(
    ConvertibleIdlType type,
    const google::protobuf::FieldDescriptor* f) {
    const char* s = to_mcpack_typestr(type, f);
    static char tempbuf[32];
    char* p = tempbuf;
    for (; *s; ++s, ++p) {
        *p = ::toupper(*s);
    }
    *p = 0;
    return tempbuf;
}

const char* describe_idl_type(ConvertibleIdlType type) {
    switch (type) {
    case IDL_AUTO:   return "IDL_AUTO";
    case IDL_INT8:   return "IDL_INT8";
    case IDL_INT16:  return "IDL_INT16";
    case IDL_INT32:  return "IDL_INT32";
    case IDL_INT64:  return "IDL_INT64";
    case IDL_UINT8:  return "IDL_UINT8";
    case IDL_UINT16: return "IDL_UINT16";
    case IDL_UINT32: return "IDL_UINT32";
    case IDL_UINT64: return "IDL_UINT64";
    case IDL_BOOL:   return "IDL_BOOL";
    case IDL_FLOAT:  return "IDL_FLOAT";
    case IDL_DOUBLE: return "IDL_DOUBLE";
    case IDL_BINARY: return "IDL_BINARY";
    case IDL_STRING: return "IDL_STRING";
    }
    return "Bad ConvertibleIdlType";
}

static std::string to_var_name(const std::string& name) {
    std::string result = name;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == '.') {
            result[i] = '_';
        }
    }
    return result;
}

static std::string to_cpp_name(const std::string& full_name) {
    std::string cname;
    cname.reserve(full_name.size() + 8);
    for (size_t i = 0; i < full_name.size(); ++i) {
        if (full_name[i] == '.') {
            cname.append("::", 2);
        } else {
            cname.push_back(full_name[i]);
        }
    }
    return cname;
}

bool generate_declarations(const std::set<std::string>& ref_msgs,
                           const std::set<std::string>& ref_maps,
                           google::protobuf::io::Printer& decl) {
    for (std::set<std::string>::const_iterator
             it = ref_msgs.begin(); it != ref_msgs.end(); ++it) {
        decl.Print(
            "extern bool parse_$vmsg$_body_internal(\n"
            "    ::google::protobuf::Message* msg,\n"
            "    ::mcpack2pb::UnparsedValue& value);\n"
            "extern void serialize_$vmsg$_body(\n"
            "    const ::google::protobuf::Message& msg,\n"
            "    ::mcpack2pb::Serializer& serializer,\n"
            "    ::mcpack2pb::SerializationFormat format);\n"
            "extern ::mcpack2pb::FieldMap* g_$vmsg$_fields;\n"
            , "vmsg", *it);
    }
    for (std::set<std::string>::const_iterator
             it = ref_maps.begin(); it != ref_maps.end(); ++it) {
        decl.Print(
            "extern bool set_$vmsg$_value(::google::protobuf::Message* msg,\n"
            "                                 ::mcpack2pb::UnparsedValue& value);\n"
            , "vmsg", *it);
    }

    return !decl.failed();
}

#define TEMPLATE_OF_SET_FUNC_SIGNATURE()                                \
    "bool set_$vmsg$_$lcfield$(::google::protobuf::Message* msg_base,\n" \
    "                                ::mcpack2pb::UnparsedValue& value) "

#define TEMPLATE_OF_SET_FUNC_BODY(fntype)                               \
    "{\n"                                                               \
    "  static_cast<$msg$*>(msg_base)->set_$lcfield$(value.as_"#fntype"(\"$field$\"));\n" \
    "  return value.stream()->good();\n"                                \
    "}\n"                                                               

#define TEMPLATE_OF_ADD_FUNC_SIGNATURE()                                \
    "bool set_$vmsg$_$lcfield$(::google::protobuf::Message* msg_base,\n"     \
    "                                ::mcpack2pb::UnparsedValue& value) "  \

#define TEMPLATE_OF_ADD_FUNC_BODY(fntype, pbtype)                       \
    "{\n"                                                               \
    "  $msg$* const msg = static_cast<$msg$*>(msg_base);\n"             \
    "  if (value.type() == ::mcpack2pb::FIELD_ISOARRAY) {\n"               \
    "    ::mcpack2pb::ISOArrayIterator it(value);\n"                       \
    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n"         \
    "    for (; it != NULL; ++it) {\n"                                  \
    "      msg->add_$lcfield$(it.as_"#fntype "());\n"                   \
    "    }\n"                                                           \
    "    return value.stream()->good();\n"                              \
    "  } else if (value.type() == ::mcpack2pb::FIELD_ARRAY) {\n"           \
    "    ::mcpack2pb::ArrayIterator it(value);\n"                          \
    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n"         \
    "    for (; it != NULL; ++it) {\n"                                  \
    "      msg->add_$lcfield$(it->as_"#fntype "(\"$field$\"));\n" \
    "    }\n"                                                           \
    "    return value.stream()->good();\n"                              \
    "  }\n"                                                             \
    "  LOG(ERROR) << \"Can't set \" << value << \" to $field$ (repeated " #pbtype ")\";\n" \
    "  return false;\n"                                                 \
    "}\n"

static bool is_map_entry(const google::protobuf::Descriptor* d) {
    if (d->field_count() != 2 ||
        d->field(0)->name() != "key" ||
        d->field(0)->number() != 1 ||
        d->field(1)->name() != "value" ||
        d->field(1)->number() != 2) {
        return false;
    }
    if (d->field(0)->is_repeated()) {
        LOG(ERROR) << d->field(0)->full_name() << " must be required or optional";
        return false;
    }
    if (d->field(1)->is_repeated()) {
        LOG(ERROR) << d->field(1)->full_name() << " must be required or optional";
        return false;
    }
    if (d->field(0)->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
        LOG(ERROR) << "key of idl map must be string";
        return false;
    }
    return true;
}

static bool generate_parsing(const google::protobuf::Descriptor* d,
                             std::set<std::string> & ref_msgs,
                             std::set<std::string> & ref_maps,
                             google::protobuf::io::Printer& impl) {
    std::string var_name = mcpack2pb::to_var_name(d->full_name());
    std::string cpp_name = mcpack2pb::to_cpp_name(d->full_name());
    ref_msgs.insert(var_name);

    impl.Print("\n// $msg$ from mcpack\n", "msg", d->full_name());
    for (int i = 0; i < d->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* f = d->field(i);
        if (f->is_repeated()) {
            impl.Print("// repeated $type$ $name$ = $number$;\n"
                       , "type", field_to_string(f)
                       , "name", f->name()
                       , "number", butil::string_printf("%d", f->number()));
            impl.Print(TEMPLATE_OF_ADD_FUNC_SIGNATURE()
                       , "vmsg", var_name
                       , "lcfield", f->lowercase_name());
            switch (f->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(int32, int32)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(int64, int64)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(uint32, uint32)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;                
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(uint64, uint64)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;                
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(bool, bool)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                impl.Print(
                    "{\n"
                    "  $msg$* const msg = static_cast<$msg$*>(msg_base);\n"
                    "  if (value.type() == ::mcpack2pb::FIELD_ISOARRAY) {\n"
                    "    ::mcpack2pb::ISOArrayIterator it(value);\n"
                    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n" 
                    "    for (; it != NULL; ++it) {\n"
                    "      msg->add_$lcfield$(($enum$)it.as_int32());\n"
                    "    }\n"
                    "    return value.stream()->good();\n"
                    "  } else if (value.type() == ::mcpack2pb::FIELD_ARRAY) {\n"            
                    "    ::mcpack2pb::ArrayIterator it(value);\n"
                    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n"
                    "    for (; it != NULL; ++it) {\n"
                    "      msg->add_$lcfield$(($enum$)it->as_int32(\"$enum$\"));\n"
                    "    }\n"
                    "    return value.stream()->good();\n"
                    "  }\n"                                                             
                    "  LOG(ERROR) << \"Can't set \" << value << \" to repeated $enum$\";\n"
                    "  return false;\n"                                                 
                    "}\n"
                    , "msg", cpp_name
                    , "enum", to_cpp_name(f->enum_type()->full_name())
                    , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(float, float)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                impl.Print(TEMPLATE_OF_ADD_FUNC_BODY(double, double)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                impl.Print(
                    "{\n"
                    "  $msg$* const msg = static_cast<$msg$*>(msg_base);\n"
                    "  if (value.type() == ::mcpack2pb::FIELD_ARRAY) {\n"
                    "    ::mcpack2pb::ArrayIterator it(value);\n"
                    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n"
                    "    for (; it != NULL; ++it) {\n"
                    "      if (it->type() == ::mcpack2pb::FIELD_STRING) {\n"
                    "        it->as_string(msg->add_$lcfield$(), \"$field$\");\n"
                    "      } else if (it->type() == ::mcpack2pb::FIELD_BINARY) {\n"
                    "        it->as_binary(msg->add_$lcfield$(), \"$field$\");\n"
                    "      } else {\n"
                    "        LOG(ERROR) << \"Can't add \" << *it << \" to $field$ (repeated string)\";\n"
                    "        return false;\n"    
                    "      }\n"
                    "    }\n"
                    "    return value.stream()->good();\n"
                    "  }\n"                                                             
                    "  LOG(ERROR) << \"Can't set \" << value << \" to $field$ (repeated string)\";\n"
                    "  return false;\n"    
                    "}\n"
                    , "msg", cpp_name
                    , "field", f->full_name()
                    , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                std::string var_name2 = mcpack2pb::to_var_name(f->message_type()->full_name());
                std::string cpp_name2 = mcpack2pb::to_cpp_name(f->message_type()->full_name());
                if (is_map_entry(f->message_type())) {
                    ref_maps.insert(var_name2);
                    impl.Print(
                        "{\n"
                        "  $msg$* const msg = static_cast<$msg$*>(msg_base);\n"
                        "  if (value.type() == ::mcpack2pb::FIELD_OBJECT) {\n"
                        "    ::mcpack2pb::ObjectIterator it(value);\n"
                        "    for (; it != NULL; ++it) {\n"
                        "      $msg2$* sub = msg->add_$lcfield$();\n"
                        "      sub->set_key(it->name.data(), it->name.size());\n"
                        , "msg", cpp_name
                        , "msg2", cpp_name2
                        , "lcfield", f->lowercase_name());
                    impl.Print(
                        "      if (!set_$vmsg2$_value(sub, it->value)) {\n"
                        "        return false;\n"
                        "      }\n"
                        "    }\n"
                        "    return value.stream()->good();\n"
                        "  }\n"
                        "  LOG(ERROR) << \"Can't set \" << value << \" to $field$ (repeated $msg2$)\";\n"
                        "  return false;\n"    
                        "}\n"
                        , "vmsg2", var_name2
                        , "msg2", f->message_type()->full_name()
                        , "field", f->full_name());
                    break;
                }
                ref_msgs.insert(var_name2);
                impl.Print(
                    "{\n"
                    "  $msg$* const msg = static_cast<$msg$*>(msg_base);\n"
                    "  if (value.type() == ::mcpack2pb::FIELD_OBJECTISOARRAY) {\n"
                    "    ::mcpack2pb::ObjectIterator it(value);\n"
                    "    for (; it != NULL; ++it) {\n"
                    "      ::mcpack2pb::SetFieldFn* fn = g_$vmsg2$_fields->seek(it->name);\n"
                    "      if (!fn) {\n"
                    "        if (!FLAGS_mcpack2pb_absent_field_is_error) {\n"
                    "          continue;\n"
                    "        } else {\n"
                    "          LOG(ERROR) << \"No field=\" << it->name << \" (\"\n"
                    "                     << value << \") in $msg$\";\n"
                    "          return false;\n"
                    "        }\n"
                    "      }\n"
                    "      if (it->value.type() == ::mcpack2pb::FIELD_ARRAY) {\n"
                    "        ::mcpack2pb::ArrayIterator it2(it->value);\n"
                    "        int i = 0;\n"
                    "        for (; it2 != NULL; ++it2, ++i) {\n"
                    "          ::google::protobuf::Message* sub_msg = NULL;\n"
                    "          if (i < msg->$lcfield$_size()) {\n"
                    "            sub_msg = msg->mutable_$lcfield$(i);\n"
                    "          } else {\n"
                    "            sub_msg = msg->add_$lcfield$();\n"
                    "          }\n"
                    "          if (it2->type() != ::mcpack2pb::FIELD_NULL) {\n"
                    "            if (!(*fn)(sub_msg, *it2)) {\n"
                    "              LOG(ERROR) << \"Fail to set item of \" << it->name;\n"
                    "              return false;\n"
                    "            }\n"
                    "          }\n"
                    "        }\n"
                    "      } else if (it->value.type() == ::mcpack2pb::FIELD_ISOARRAY) {\n"
                    "         LOG(ERROR) << \"Shouldn't be a iso array, name=\" << it->name;\n"
                    "         return false;\n"
                    "      } else {\n"
                    "         LOG(ERROR) << \"Can't add \" << value << \" to repeated $msg$\";\n"
                    "         return false;\n"
                    "      }\n"
                    "    }\n"
                    "    return value.stream()->good();\n"
                    "  } else if (value.type() == ::mcpack2pb::FIELD_ARRAY) {\n"
                    "    ::mcpack2pb::ArrayIterator it(value);\n"
                    "    msg->mutable_$lcfield$()->Reserve(it.item_count());\n"
                    "    for (; it != NULL; ++it) {\n"
                    "      if (it->type() == ::mcpack2pb::FIELD_OBJECT) {\n"
                    "        if (!parse_$vmsg2$_body_internal(msg->add_$lcfield$(), *it)) {\n"
                    "          return false;\n"
                    "        }\n"
                    "      } else {\n"
                    "        LOG(ERROR) << \"Can't add \" << *it << \" to repeated $msg$\";\n"
                    "        return false;\n"    
                    "      }\n"
                    "    }\n"
                    "    return value.stream()->good();\n"
                    "  }\n"
                    , "vmsg2", var_name2
                    , "msg", cpp_name
                    , "lcfield", f->lowercase_name());
                impl.Print(
                    "  LOG(ERROR) << \"Can't set \" << value << \" to $field$ (repeated $msg2$)\";\n"
                    "  return false;\n"    
                    "}\n"
                    , "msg2", f->message_type()->full_name()
                    , "field", f->full_name());
            } break;
            } // switch
        } else {
            if (f->is_optional()) {
                impl.Print("// optional $type$ $name$ = $number$;\n"
                           , "type", field_to_string(f)
                           , "name", f->name()
                           , "number", butil::string_printf("%d", f->number()));
            } else {
                impl.Print("// required $type$ $name$ = $number$;\n"
                           , "type", field_to_string(f)
                           , "name", f->name()
                           , "number", butil::string_printf("%d", f->number()));
            }
            impl.Print(TEMPLATE_OF_SET_FUNC_SIGNATURE()
                       , "vmsg", var_name
                       , "lcfield", f->lowercase_name());
            switch (f->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(int32)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(int64)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(uint32)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;                
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(uint64)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;                
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(bool)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(float)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                impl.Print(TEMPLATE_OF_SET_FUNC_BODY(double)
                           , "msg", cpp_name
                           , "field", f->full_name()
                           , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                impl.Print(
                    "{\n"
                    "  static_cast<$msg$*>(msg_base)->set_$lcfield$(static_cast<$enum$>(value.as_int32(\"$enum$\")));\n"
                    "  return value.stream()->good();\n"
                    "}\n"
                    , "msg", cpp_name
                    , "enum", to_cpp_name(f->enum_type()->full_name())
                    , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                // TODO: Encoding checking/conversion for pb strings(utf8).
                impl.Print(
                    "{\n"
                    "  if (value.type() == ::mcpack2pb::FIELD_STRING) {\n"
                    "    value.as_string(static_cast<$msg$*>(msg_base)->mutable_$lcfield$(), \"$field$\");\n"
                    "    return value.stream()->good();\n"
                    "  } else if (value.type() == ::mcpack2pb::FIELD_BINARY) {\n"
                    "    value.as_binary(static_cast<$msg$*>(msg_base)->mutable_$lcfield$(), \"$field$\");\n"
                    "    return value.stream()->good();\n"
                    "  }\n"
                    "  LOG(ERROR) << \"Can't set \" << value << \" to $field$ (string)\";\n"
                    "  return false;\n"
                    "}\n"
                    , "msg", cpp_name
                    , "field", f->full_name()
                    , "lcfield", f->lowercase_name());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                std::string var_name2 = mcpack2pb::to_var_name(f->message_type()->full_name());
                ref_msgs.insert(var_name2);
                impl.Print(
                    "{\n"
                    "  if (value.type() == ::mcpack2pb::FIELD_OBJECT) {\n"
                    "    return parse_$vmsg2$_body_internal(static_cast<$msg$*>(msg_base)->mutable_$lcfield$(), value);\n"
                    "  }\n"
                    , "vmsg2", var_name2
                    , "msg", cpp_name
                    , "lcfield", f->lowercase_name());
                impl.Print(
                    // FIXME: describe type.
                    "  LOG(ERROR) << \"Can't set \" << value << \" to $field$\";\n"
                    "  return false;\n"
                    "}\n"
                    , "field", f->full_name());
            } break;
            } // switch
        } // else
    }

    impl.Print(
        "bool parse_$vmsg$_body_internal(\n"
        "    ::google::protobuf::Message* msg,\n"
        "    ::mcpack2pb::UnparsedValue& value) {\n"
        "  ::mcpack2pb::ObjectIterator it(value);\n"
        "  for (; it != NULL; ++it) {\n"
        "    ::mcpack2pb::SetFieldFn* fn = g_$vmsg$_fields->seek(it->name);\n"
        "    if (!fn) {\n"
        "      if (!FLAGS_mcpack2pb_absent_field_is_error) {\n"
        "        continue;\n"
        "      } else {\n"
        "        LOG(ERROR) << \"No field=\" << it->name << \" (\"\n"
        "                   << it->value << \") in $msg$\";\n"
        "        return false;\n"
        "      }\n"
        "    }\n"
        "    if (!(*fn)(msg, it->value)) {\n"
        "      return false;\n"
        "    }\n"
        "  }\n"
        "  return value.stream()->good();\n"
        "}\n"
        "bool parse_$vmsg$_body(\n"
        "    ::google::protobuf::Message* msg,\n"
        "    ::google::protobuf::io::ZeroCopyInputStream* input,\n"
        "     size_t size) {\n"
        "  ::mcpack2pb::InputStream mc_stream(input);\n"
        "  ::mcpack2pb::UnparsedValue value(::mcpack2pb::FIELD_OBJECT, &mc_stream, size);\n"
        "  if (!parse_$vmsg$_body_internal(msg, value)) {\n"
        "    return false;\n"
        "  }\n"
        "  if (!msg->IsInitialized()) {\n"
        "    LOG(ERROR) << \"Missing required fields: \" << msg->InitializationErrorString();\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n"
        "}\n"
        "size_t parse_$vmsg$(\n"
        "    ::google::protobuf::Message* msg,\n"
        "    ::google::protobuf::io::ZeroCopyInputStream* input) {\n"
        "  ::mcpack2pb::InputStream mc_stream(input);\n"
        "  const size_t value_size = ::mcpack2pb::unbox(&mc_stream);\n"
        "  if (!value_size) {\n"
        "    LOG(ERROR) << \"Fail to unbox\";\n"
        "    return 0;\n"
        "  }\n"
        "  ::mcpack2pb::UnparsedValue value(::mcpack2pb::FIELD_OBJECT, &mc_stream, value_size);\n"
        "  if (!parse_$vmsg$_body_internal(msg, value)) {\n"
        "    return 0;\n"
        "  }\n"
        "  if (!msg->IsInitialized()) {\n"
        "    LOG(ERROR) << \"Missing required fields: \" << msg->InitializationErrorString();\n"
        "    return 0;\n"
        "  }\n"
        "  return 6/*sizeof(FieldLongHead)*/ + value_size;\n"
        "}\n"
        , "vmsg", var_name
        , "msg", d->full_name());
    return !impl.failed();
}

#define TEMPLATE_SERIALIZE_REPEATED(cit, printer, field, strict_cond, looser_cond) \
    if (strict_cond) {                                                  \
        (printer).Print(                                                \
            "if (msg.$lcfield$_size()) {\n"                             \
            "  if (format == ::mcpack2pb::FORMAT_COMPACK) {\n"             \
            "    serializer.begin_compack_array(\"$field$\", ::mcpack2pb::FIELD_$TYPE$);\n" \
            "  } else {\n"                                              \
            "    serializer.begin_mcpack_array(\"$field$\", ::mcpack2pb::FIELD_$TYPE$);\n" \
            "  }\n"                                                     \
            , "lcfield", (field)->lowercase_name()                      \
            , "field", get_idl_name(field)                              \
            , "TYPE", to_mcpack_typestr_uppercase(cit, (field)));       \
        (printer).Print(                                                \
            "  serializer.add_multiple_$type$(msg.$lcfield$().data(), msg.$lcfield$_size());\n" \
            "  serializer.end_array();\n"                               \
            "}"                                                         \
            , "type", to_mcpack_typestr(cit, (field))                   \
            , "field", get_idl_name(field)                              \
            , "lcfield", (field)->lowercase_name());                    \
        if ((field)->options().GetExtension(idl_on)) {                  \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_empty_array(\"$field$\");\n"              \
            "}\n"                                                       \
            , "field", get_idl_name(field));                            \
        } else {                                                        \
          (printer).Print("\n");                                        \
        }                                                               \
    } else if (looser_cond) {                                           \
        (printer).Print(                                                \
            "if (msg.$lcfield$_size()) {\n"                             \
            "  if (format == ::mcpack2pb::FORMAT_COMPACK) {\n"             \
            "    serializer.begin_compack_array(\"$field$\", ::mcpack2pb::FIELD_$TYPE$);\n" \
            "  } else {\n"                                              \
            "    serializer.begin_mcpack_array(\"$field$\", ::mcpack2pb::FIELD_$TYPE$);\n" \
            "  }\n"                                                     \
            , "lcfield", (field)->lowercase_name()                      \
            , "field", get_idl_name(field)                              \
            , "TYPE", to_mcpack_typestr_uppercase(cit, (field)));       \
        (printer).Print(                                                \
            "  for (int i = 0; i < msg.$lcfield$_size(); ++i) {\n"      \
            "    serializer.add_$type$(msg.$lcfield$(i));\n"            \
            "  }\n"                                                     \
            "  serializer.end_array();\n"                               \
            "}"                                                         \
            , "type", to_mcpack_typestr(cit, (field))                   \
            , "field", get_idl_name(field)                              \
            , "lcfield", (field)->lowercase_name());                    \
        if ((field)->options().GetExtension(idl_on)) {                  \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_empty_array(\"$field$\");\n"              \
            "}\n"                                                       \
            , "field", get_idl_name(field));                            \
        } else {                                                        \
          (printer).Print("\n");                                        \
        }                                                               \
    } else {                                                            \
        if ((field)->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) { \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << (field)->enum_type()->full_name() << ") to " \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        } else {                                                        \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << field_to_string(field) << ") to "     \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        }                                                               \
        return false;                                                   \
    }                                                                   \

#define TEMPLATE_SERIALIZE_REPEATED_INTEGRAL(cit, printer, field, strict_cond) \
    TEMPLATE_SERIALIZE_REPEATED(cit, printer, field, ((cit) == IDL_AUTO || (strict_cond)), \
                                is_integral_type(cit))

    
#define TEMPLATE_SERIALIZE(cit, printer, field, cond)                   \
    if (!(cond)) {                                                      \
        if ((field)->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) { \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << (field)->enum_type()->full_name() << ") to " \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        } else {                                                        \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << field_to_string(field) << ") to "     \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        }                                                               \
        return false;                                                   \
    }                                                                   \
    (printer).Print(                                                    \
        "if (msg.has_$lcfield$()) {\n"                                  \
        "  serializer.add_$type$(\"$field$\", msg.$lcfield$());\n"      \
        "}"                                                             \
        , "type", to_mcpack_typestr(cit, (field))                       \
        , "field", get_idl_name(field)                                  \
        , "lcfield", (field)->lowercase_name());                        \
        if ((field)->options().GetExtension(idl_on)) {                  \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_empty_array(\"$field$\");\n"              \
            "}\n"                                                       \
            , "field", get_idl_name(field));                            \
        } 

#define TEMPLATE_SERIALIZE_INTEGRAL(cit, printer, field)                \
    TEMPLATE_SERIALIZE(cit, printer, field,                             \
                       ((cit) == IDL_AUTO || is_integral_type(cit)))


#define TEMPLATE_INNER_SERIALIZE_REPEATED(msg, cit, printer, field,     \
                                          strict_cond, looser_cond)     \
    if ((strict_cond)) {                                                \
        (printer).Print(                                                \
            "if ($msg$.$lcfield$_size()) {\n"                           \
            "  serializer.begin_compack_array(::mcpack2pb::FIELD_$TYPE$);\n" \
            , "msg", msg                                                \
            , "TYPE", to_mcpack_typestr_uppercase(cit, (field))         \
            , "lcfield", (field)->lowercase_name());                    \
        (printer).Print(                                                \
            "  serializer.add_multiple_$type$($msg$.$lcfield$().data(), $msg$.$lcfield$_size());\n" \
            "  serializer.end_array();\n"                               \
            "}"                                                         \
            , "msg", msg                                                \
            , "type", to_mcpack_typestr(cit, (field))                   \
            , "lcfield", (field)->lowercase_name());                    \
        if ((field)->options().GetExtension(idl_on)) {                  \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_empty_array();\n"                         \
            "}\n");                                                     \
        } else {                                                        \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_null();\n"                                \
            "}\n");                                                     \
        }                                                               \
    } else if (looser_cond) {                                           \
        (printer).Print(                                                \
            "if ($msg$.$lcfield$_size()) {\n"                           \
            "  serializer.begin_compack_array(::mcpack2pb::FIELD_$TYPE$);\n" \
            , "msg", msg                                                \
            , "TYPE", to_mcpack_typestr_uppercase(cit, (field))         \
            , "lcfield", (field)->lowercase_name());                    \
        (printer).Print(                                                \
            "  for (int j = 0; j < $msg$.$lcfield$_size(); ++j) {\n"    \
            "    serializer.add_$type$($msg$.$lcfield$(j));\n"          \
            "  }\n"                                                     \
            "  serializer.end_array();\n"                               \
            "}"                                                         \
            , "msg", msg                                                \
            , "type", to_mcpack_typestr(cit, (field))                   \
            , "lcfield", (field)->lowercase_name());                    \
        if ((field)->options().GetExtension(idl_on)) {                  \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_empty_array();\n"                         \
            "}\n");                                                     \
        } else {                                                        \
          (printer).Print(                                              \
            " else {\n"                                                 \
            "  serializer.add_null();\n"                                \
            "}\n");                                                     \
        }                                                               \
    } else {                                                            \
        if ((field)->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) { \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << (field)->enum_type()->full_name() << ") to " \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        } else {                                                        \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << field_to_string(field) << ") to "     \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        }                                                               \
        return false;                                                   \
    }                                                                   \

#define TEMPLATE_INNER_SERIALIZE_REPEATED_INTEGRAL(msg, cit, printer, field, strict_cond) \
    TEMPLATE_INNER_SERIALIZE_REPEATED(msg, cit, printer, field, ((cit) == IDL_AUTO || (strict_cond)), \
                                      is_integral_type(cit))

    
#define TEMPLATE_INNER_SERIALIZE(msg, cit, printer, field, cond)        \
    if (!(cond)) {                                                      \
        if ((field)->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) { \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << (field)->enum_type()->full_name() << ") to " \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        } else {                                                        \
            LOG(ERROR) << "Disallow converting " << (field)->full_name() \
                       << " (" << field_to_string(field) << ") to "     \
                       << to_mcpack_typestr(cit, (field)) << " (idl)";  \
        }                                                               \
        return false;                                                   \
    }                                                                   \
    (printer).Print("if ($msg$.has_$lcfield$()) {\n"                    \
                    "  serializer.add_$type$($msg$.$lcfield$());\n"     \
                    "} else {\n"                                        \
                    "  serializer.add_null();\n"                        \
                    "}\n"                                               \
                    , "msg", msg                                        \
                    , "type", to_mcpack_typestr(cit, (field))           \
                    , "lcfield", (field)->lowercase_name())            

#define TEMPLATE_INNER_SERIALIZE_INTEGRAL(msg, cit, printer, field)     \
    TEMPLATE_INNER_SERIALIZE(msg, cit, printer, field,                  \
                             ((cit) == IDL_AUTO || is_integral_type(cit)))

static bool generate_serializing(const google::protobuf::Descriptor* d,
                                 std::set<std::string> & ref_msgs,
                                 std::set<std::string> & ref_maps,
                                 google::protobuf::io::Printer & impl) {
    std::string var_name = mcpack2pb::to_var_name(d->full_name());
    std::string cpp_name = mcpack2pb::to_cpp_name(d->full_name());
    ref_msgs.insert(var_name);
    impl.Print(
        "void serialize_$vmsg$_body(\n"
        "    const ::google::protobuf::Message& msg_base,\n"
        "    ::mcpack2pb::Serializer& serializer,\n"
        "    ::mcpack2pb::SerializationFormat format) {\n"
        "  (void)format;     // suppress compiler warning when it's not used\n"
        , "vmsg", var_name);
    if (d->field_count()) {
        impl.Print(
            "  const $msg$& msg = static_cast<const $msg$&>(msg_base);\n"
            , "msg", cpp_name);
    } else {
        impl.Print("  (void)msg_base;   // ^\n"
                   "  (void)serializer; // ^\n");
    }
    impl.Indent();
    for (int i = 0; i < d->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* f = d->field(i);
        ConvertibleIdlType cit = f->options().GetExtension(idl_type);
        // Print the field as comment.
        std::string comment_template;
        if (cit == IDL_AUTO) {
            butil::string_printf(&comment_template,
                                "// %s $type$ $name$ = $number$;\n",
                                (f->is_repeated() ? "repeated" :
                                 (f->is_optional() ? "optional" : "required")));
        } else {
            butil::string_printf(&comment_template,
                                "// %s $type$ $name$ = $number$ [(idl_type)=%s];\n",
                                (f->is_repeated() ? "repeated" :
                                 (f->is_optional() ? "optional" : "required")),
                                describe_idl_type(cit));
        }
        impl.Print(comment_template.c_str()
                   , "type", field_to_string(f)
                   , "name", f->name()
                   , "number", butil::string_printf("%d", f->number()));
        if (f->is_repeated()) {
            switch (f->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                TEMPLATE_SERIALIZE_REPEATED_INTEGRAL(
                    cit, impl, f, (cit == IDL_INT32 || cit == IDL_UINT32));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                TEMPLATE_SERIALIZE_REPEATED_INTEGRAL(
                    cit, impl, f, (cit == IDL_INT64 || cit == IDL_UINT64));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                TEMPLATE_SERIALIZE_REPEATED(
                    cit, impl, f, (cit == IDL_AUTO || cit == IDL_BOOL), false);
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                TEMPLATE_SERIALIZE_REPEATED(
                    cit, impl, f,
                    (cit == IDL_AUTO || cit == IDL_FLOAT), (cit == IDL_DOUBLE));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                TEMPLATE_SERIALIZE_REPEATED(
                    cit, impl, f,
                    (cit == IDL_AUTO || cit == IDL_DOUBLE), (cit == IDL_FLOAT));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                if (f->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                    TEMPLATE_SERIALIZE_REPEATED(
                        cit, impl, f, false, (cit == IDL_AUTO || cit == IDL_STRING));
                } else if (f->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
                    TEMPLATE_SERIALIZE_REPEATED(
                        cit, impl, f, false,
                        (cit == IDL_AUTO || cit == IDL_BINARY || cit == IDL_STRING));
                } else {
                    LOG(ERROR) << "Unknown pb type=" << f->type();
                    return false;
                }
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                if (cit != IDL_AUTO) {
                    LOG(ERROR) << "Disallow converting " << f->full_name()
                               << " (" << f->message_type()->full_name() << ") to "
                               << to_mcpack_typestr(cit, f) << " (idl)";
                    return false;
                }
                const google::protobuf::Descriptor* msg2 = f->message_type();
                std::string var_name2 = mcpack2pb::to_var_name(msg2->full_name());
                std::string cpp_name2 = mcpack2pb::to_cpp_name(msg2->full_name());
                if (is_map_entry(msg2)) {
                    ref_maps.insert(var_name2);
                    impl.Print(
                        "serializer.begin_object(\"$field$\");\n"
                        "for (int i = 0; i < msg.$lcfield$_size(); ++i) {\n"
                        "  const $msg2$& pair = msg.$lcfield$(i);\n"
                        , "field", get_idl_name(f)
                        , "lcfield", f->lowercase_name()
                        , "msg2", cpp_name2);
                    const google::protobuf::FieldDescriptor* value_desc = msg2->field(1);
                    switch (value_desc->cpp_type()) {
                    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                        impl.Print("  serializer.add_int32(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                        impl.Print("  serializer.add_uint32(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                        impl.Print("  serializer.add_int32(pair.key(), (int32_t)pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                        impl.Print("  serializer.add_int64(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                        impl.Print("  serializer.add_uint64(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                        impl.Print("  serializer.add_bool(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                        impl.Print("  serializer.add_float(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                        impl.Print("  serializer.add_double(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                        impl.Print("  serializer.add_string(pair.key(), pair.value());\n");
                        break;
                    case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                        std::string var_name3 = mcpack2pb::to_var_name(
                            value_desc->message_type()->full_name());
                        ref_msgs.insert(var_name3);
                        impl.Print(
                            "  serializer.begin_object(pair.key());\n"
                            "  serialize_$vmsg3$_body(pair.value(), serializer, format);\n"
                            "  serializer.end_object();\n"
                            , "vmsg3", var_name3);
                    } break;
                    } // switch
                    impl.Print(
                        "}\n"
                        "serializer.end_object();\n");
                    break;
                }

                ref_msgs.insert(var_name2);
                impl.Print(
                    "if (format == ::mcpack2pb::FORMAT_MCPACK_V2) {\n"
                    "  if (msg.$lcfield$_size()) {\n"
                    "    serializer.begin_mcpack_array(\"$field$\", ::mcpack2pb::FIELD_OBJECT);\n"
                    "    for (int i = 0; i < msg.$lcfield$_size(); ++i) {\n"
                    "      serializer.begin_object();\n"
                    "      serialize_$vmsg2$_body(msg.$lcfield$(i), serializer, format);\n"
                    "      serializer.end_object();\n"
                    "    }\n"
                    "    serializer.end_array();\n"
                    "  }"
                    , "field", get_idl_name(f)
                    , "lcfield", f->lowercase_name()
                    , "vmsg2", var_name2);
                if (f->options().GetExtension(idl_on)) {
                    impl.Print(
                       " else {\n"
                       "    serializer.add_empty_array(\"$field$\");\n"
                       "  }\n", "field", get_idl_name(f));
                } else {
                    impl.Print("\n");
                }
                impl.Print("} else if (msg.$lcfield$_size()) {\n"
                    , "lcfield", f->lowercase_name());
                impl.Indent();
                impl.Print("serializer.begin_object(\"$field$\");\n"
                           , "field", get_idl_name(f));
                for (int j = 0; j < msg2->field_count(); ++j) {
                    const google::protobuf::FieldDescriptor* f2 = msg2->field(j);
                    ConvertibleIdlType cit2 = f2->options().GetExtension(idl_type);
                    impl.Print(
                        "serializer.begin_mcpack_array(\"$f2$\", ::mcpack2pb::FIELD_$TYPE$);\n"
                        "for (int i = 0; i < msg.$lcfield$_size(); ++i) {\n"
                        , "f2", get_idl_name(f2)
                        , "TYPE", (f2->is_repeated() ? "ARRAY" : to_mcpack_typestr_uppercase(cit2, (f2)))
                        , "lcfield", f->lowercase_name());
                    impl.Indent();
                    if (f2->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
                        if (cit2 != IDL_AUTO) {
                            LOG(ERROR) << "Disallow converting " << f2->full_name()
                                       << " (" << f2->message_type()->full_name() << ") to "
                                       << to_mcpack_typestr(cit2, f2) << " (idl)";
                            return false;
                        }
                        std::string var_name3 = mcpack2pb::to_var_name(f2->message_type()->full_name());
                        ref_msgs.insert(var_name3);
                        if (f2->is_repeated()) {
                            impl.Print(
                                "const int $lcfield2$_size = msg.$lcfield$(i).$lcfield2$_size();\n"
                                "if ($lcfield2$_size) {\n"
                                "  serializer.begin_mcpack_array(::mcpack2pb::FIELD_OBJECT);\n"
                                "  for (int j = 0; j < $lcfield2$_size; ++j) {\n"
                                "    serializer.begin_object();\n"
                                "    serialize_$vmsg3$_body(msg.$lcfield$(i).$lcfield2$(j), serializer, format);\n"
                                "    serializer.end_object();\n"
                                "  }\n"
                                "  serializer.end_array();\n"
                                "}"
                                , "vmsg3", var_name3
                                , "lcfield", f->lowercase_name()
                                , "lcfield2", f2->lowercase_name());
                            if (f2->options().GetExtension(idl_on)) {
                                impl.Print(
                                " else {\n"
                                "  serializer.add_empty_array();\n"
                                "}\n");
                            } else {
                                impl.Print(
                                " else {\n"
                                "  serializer.add_null();\n"
                                "}\n");
                            }
                        } else {
                            impl.Print(
                                "if (msg.$lcfield$(i).has_$lcfield2$()) {\n"
                                "  serializer.begin_object();\n"
                                "  serialize_$vmsg3$_body(msg.$lcfield$(i).$lcfield2$(), serializer, format);\n"
                                "  serializer.end_object();\n"
                                "} else {\n"
                                "  serializer.add_null();\n"
                                "}\n"
                                , "vmsg3", var_name3
                                , "lcfield", f->lowercase_name()
                                , "lcfield2", f2->lowercase_name());
                        }
                    } else if (f2->is_repeated()) {
                        const std::string msgstr = butil::string_printf(
                            "msg.%s(i)", f->lowercase_name().c_str());
                        switch (f2->cpp_type()) {
                        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                            TEMPLATE_INNER_SERIALIZE_REPEATED_INTEGRAL(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_INT32 || cit2 == IDL_UINT32));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                            TEMPLATE_INNER_SERIALIZE_REPEATED_INTEGRAL(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_INT64 || cit2 == IDL_UINT64));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                            TEMPLATE_INNER_SERIALIZE_REPEATED(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_BOOL), false);
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                            TEMPLATE_INNER_SERIALIZE_REPEATED(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_FLOAT), (cit2 == IDL_DOUBLE));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                            TEMPLATE_INNER_SERIALIZE_REPEATED(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_DOUBLE), (cit2 == IDL_FLOAT));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                            if (f2->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                                TEMPLATE_INNER_SERIALIZE_REPEATED(
                                    msgstr.c_str(), cit2, impl, f2, false,
                                    (cit2 == IDL_AUTO || cit2 == IDL_STRING));
                            } else if (f2->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
                                TEMPLATE_INNER_SERIALIZE_REPEATED(
                                    msgstr.c_str(), cit2, impl, f2, false,
                                    (cit2 == IDL_AUTO || cit2 == IDL_BINARY || cit2 == IDL_STRING));
                            } else {
                                LOG(ERROR) << "Unknown pb type=" << f2->type();
                                return false;
                            }
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                            CHECK(false) << "Impossible";
                            return false;
                        }
                    } else {
                        const std::string msgstr = butil::string_printf(
                            "msg.%s(i)", f->lowercase_name().c_str());
                        switch (f2->cpp_type()) {
                        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                            TEMPLATE_INNER_SERIALIZE_INTEGRAL(msgstr.c_str(), cit2, impl, f2);
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                            TEMPLATE_INNER_SERIALIZE(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_BOOL));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                            TEMPLATE_INNER_SERIALIZE(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_FLOAT));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                            TEMPLATE_INNER_SERIALIZE(
                                msgstr.c_str(), cit2, impl, f2,
                                (cit2 == IDL_AUTO || cit2 == IDL_FLOAT || cit2 == IDL_DOUBLE));
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                            if (f2->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                                TEMPLATE_INNER_SERIALIZE(
                                    msgstr.c_str(), cit2, impl, f2,
                                    (cit2 == IDL_AUTO || cit2 == IDL_STRING));
                            } else if (f2->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
                                TEMPLATE_INNER_SERIALIZE(
                                    msgstr.c_str(), cit2, impl, f2,
                                    (cit2 == IDL_AUTO || cit2 == IDL_BINARY || cit2 == IDL_STRING));
                            } else {
                                LOG(ERROR) << "Unknown pb type=" << f2->type();
                                return false;
                            }
                            break;
                        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                            LOG(ERROR) << "Impossible";
                            return false;
                        }
                    }
                    impl.Outdent();
                    impl.Print("}\n"
                               "serializer.end_array();\n");
                }
                impl.Print("serializer.end_object_iso();\n");
                impl.Outdent();
                impl.Print("} else {\n"
                           "  serializer.begin_object(\"$field$\");\n"
                           "  serializer.end_object_iso();\n"
                           "}\n"
                           , "field", get_idl_name(f));
            } break;
            } // switch
        } else {
            switch (f->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                TEMPLATE_SERIALIZE_INTEGRAL(cit, impl, f);
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                TEMPLATE_SERIALIZE(cit, impl, f,
                                   (cit == IDL_AUTO || cit == IDL_BOOL));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                TEMPLATE_SERIALIZE(cit, impl, f,
                                   (cit == IDL_AUTO || cit == IDL_FLOAT));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                TEMPLATE_SERIALIZE(
                    cit, impl, f,
                    (cit == IDL_AUTO || cit == IDL_FLOAT || cit == IDL_DOUBLE));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                if (f->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                    TEMPLATE_SERIALIZE(
                        cit, impl, f, (cit == IDL_AUTO || cit == IDL_STRING));
                } else if (f->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
                    TEMPLATE_SERIALIZE(
                        cit, impl, f,
                        (cit == IDL_AUTO || cit == IDL_BINARY || cit == IDL_STRING));
                } else {
                    LOG(ERROR) << "Unknown pb type=" << f->type();
                    return false;
                }
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                if (cit != IDL_AUTO) {
                    LOG(ERROR) << "Disallow converting " << f->full_name()
                               << " (" << f->message_type()->full_name() << ") to "
                               << to_mcpack_typestr(cit, f) << " (idl)";
                    return false;
                }
                std::string var_name2 = mcpack2pb::to_var_name(f->message_type()->full_name());
                ref_msgs.insert(var_name2);
                impl.Print("if (msg.has_$lcfield$()) {\n"
                           "  serializer.begin_object(\"$field$\");\n"
                           "  serialize_$vmsg$_body(msg.$lcfield$(), serializer, format);\n"
                           "  serializer.end_object();\n"
                           "}"
                           , "field", get_idl_name(f)
                           , "lcfield", f->lowercase_name()
                           , "vmsg", var_name2);
            } break;
            } // switch
            if (f->is_required()) {
                impl.Print(" else {\n"
                           "  LOG(ERROR) << \"Missing required $field$\";\n"
                           "  return serializer.set_bad();\n"
                           "}\n", "field", f->full_name());
            } else {
                impl.Print("\n");
            }
        } // else
    }
    impl.Outdent();
    impl.Print(
        "}\n"
        "bool serialize_$vmsg$(\n"
        "    const ::google::protobuf::Message& msg_base,\n"
        "    ::google::protobuf::io::ZeroCopyOutputStream* output,\n"
        "    ::mcpack2pb::SerializationFormat format) {\n"
        "  ::mcpack2pb::OutputStream ostream(output);\n"
        "  ::mcpack2pb::Serializer serializer(&ostream);\n"
        "  serializer.begin_object();\n"
        "  serialize_$vmsg$_body(msg_base, serializer, format);\n"
        "  serializer.end_object();\n"
        "  ostream.done();\n"
        "  return serializer.good();\n"
        "}\n"
        , "vmsg", var_name);
    return !impl.failed();
}

static std::string protobuf_style_normalize_filename(const std::string & fname) {
    std::string norm_fname;
    norm_fname.reserve(fname.size() + 10);
    for (size_t i = 0; i < fname.size(); ++i) {
        if (fname[i] == '_' || isdigit(fname[i]) || isalpha(fname[i])) {
            norm_fname.push_back(fname[i]);
        } else {
            char symbol[4];
            snprintf(symbol, sizeof(symbol), "_%02x", (int)fname[i]);
            norm_fname.append(symbol, 3);
        }
    }
    return norm_fname;
}

static bool generate_registration(
    const google::protobuf::FileDescriptor* file,
    google::protobuf::io::Printer & impl) {
    const std::string cpp_ns = to_cpp_name(file->package());
    std::string norm_fname = protobuf_style_normalize_filename(file->name());
    impl.Print(
        "\n// register all message handlers\n"
        "struct RegisterMcpackFunctions_$norm_fname$ {\n"
        "  RegisterMcpackFunctions_$norm_fname$() {\n"
        , "norm_fname", norm_fname);
    impl.Indent();
    impl.Indent();
    for (int i = 0; i < file->message_type_count(); ++i) {
        const google::protobuf::Descriptor* d = file->message_type(i);
        std::string var_name = mcpack2pb::to_var_name(d->full_name());

        impl.Print(
            "\n"
            "g_$vmsg$_fields = new ::mcpack2pb::FieldMap;\n"
            "CHECK_EQ(0, g_$vmsg$_fields->init(std::max($field_count$, 1), 30));\n"
            , "vmsg", var_name
            , "field_count", ::butil::string_printf("%d", d->field_count())); 
        for (int i = 0; i < d->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* f = d->field(i);
            impl.Print("(*g_$vmsg$_fields)[\"$field$\"] = ::set_$vmsg$_$lcfield$;\n"
                       , "vmsg", var_name
                       , "field", get_idl_name(f)
                       , "lcfield", f->lowercase_name());
        }
        impl.Print(
            "::mcpack2pb::MessageHandler $vmsg$_handler = {\n"
            "  parse_$vmsg$,\n"
            "  parse_$vmsg$_body,\n"
            "  serialize_$vmsg$,\n"
            "  serialize_$vmsg$_body\n"
            "};\n"
            "::mcpack2pb::register_message_handler_or_die(\"$fmsg$\", $vmsg$_handler);\n"
            , "fmsg", d->full_name()
            , "vmsg", var_name);
    }
    impl.Outdent();
    impl.Outdent();
    impl.Print("  }\n"
               "} static_init_mcpack_$suffix$;\n"
               , "suffix", norm_fname);
    return !impl.failed();
}

class McpackToProtobuf : public google::protobuf::compiler::CodeGenerator {
public:
    bool Generate(const google::protobuf::FileDescriptor* file,
                  const std::string& parameter,
                  google::protobuf::compiler::GeneratorContext*,
                  std::string* error) const override;
};

bool McpackToProtobuf::Generate(const google::protobuf::FileDescriptor* file,
                                const std::string& /*parameter*/,
                                google::protobuf::compiler::GeneratorContext* ctx,
                                std::string* error) const {
    if (!file->options().GetExtension(idl_support)) {
        // skip the file.
        return true;
    }
    
    std::string cpp_name = file->name();
    const size_t pos = cpp_name.find_last_of('.');
    if (pos == std::string::npos) {
        ::butil::string_printf(error, "Bad filename=%s", cpp_name.c_str());
        return false;
    }
    cpp_name.resize(pos);
    cpp_name.append(".pb.cc");
    
    google::protobuf::io::Printer inc_printer(
        ctx->OpenForInsert(cpp_name, "includes"), '$');
    inc_printer.Print("#include <butil/logging.h>\n"
                      "#include <mcpack2pb/mcpack2pb.h>\n"
                      "#include <gflags/gflags.h>\n");

    google::protobuf::io::Printer gdecl_printer(
        ctx->OpenForInsert(cpp_name, "includes"), '$');
    google::protobuf::io::Printer gimpl_printer(
        ctx->OpenForInsert(cpp_name, "global_scope"), '$');

    gdecl_printer.Print(
        "\n// ==== declarations generated by brpc/mcpack2pb/protoc-gen-mcpack ====\n"
        "DECLARE_bool(mcpack2pb_absent_field_is_error);\n");

    std::set<std::string> ref_msgs;
    std::set<std::string> ref_maps;
    for (int i = 0; i < file->message_type_count(); ++i) {
        const google::protobuf::Descriptor* d = file->message_type(i);
        if (!generate_parsing(d, ref_msgs, ref_maps, gimpl_printer)) {
            ::butil::string_printf(
                error, "Fail to generate parsing code for %s",
                d->full_name().c_str());
            return false;
        }
        if (!generate_serializing(d, ref_msgs, ref_maps, gimpl_printer)) {
            ::butil::string_printf(
                error, "Fail to generate serializing code for %s",
                d->full_name().c_str());
            return false;
        }
        std::string var_name = mcpack2pb::to_var_name(d->full_name());
        gdecl_printer.Print(
            "::mcpack2pb::FieldMap* g_$vmsg$_fields = NULL;\n"
            , "vmsg", var_name);
    }
    if (!generate_declarations(ref_msgs, ref_maps, gdecl_printer)) {
        ::butil::string_printf(
            error, "Fail to generate declarations for %s",
            cpp_name.c_str());
        return false;        
    }
    if (!generate_registration(file, gimpl_printer)) {
        ::butil::string_printf(
            error, "Fail to generate registration code for %s",
            cpp_name.c_str());
        return false;
    }
    return true;
}
} // namespace mcpack2pb

int main(int argc, char* argv[]) {
    ::mcpack2pb::McpackToProtobuf generator;
    return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
