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

#ifndef BRPC_JSON2PB_JSON_PROTOBUF_MAP_H
#define BRPC_JSON2PB_JSON_PROTOBUF_MAP_H

#include <google/protobuf/descriptor.h>

namespace json2pb {

const char* const KEY_NAME = "key";
const char* const VALUE_NAME = "value";
const int KEY_INDEX = 0;
const int VALUE_INDEX = 1;

// Map inside protobuf is officially supported in proto3 using
// statement like: map<string, string> my_map = N;
// However, we can still emmulate this map in proto2 by writing:
// message MapFieldEntry {
//     required string key = 1;           // MUST be the first
//     required string value = 2;         // MUST be the second
// }
// repeated MapFieldEntry my_map = N;
// 
// Natually, when converting this map to json, it should be like:
// { "my_map": {"key1": value1, "key2": value2 } }
// instead of:
// { "my_map": [{"key": "key1", "value": value1},
//              {"key": "key2", "value": value2}] }
// In order to get the former one, the type of `key' field MUST be
// string since JSON only supports string keys

// Check whether `field' is a map type field and is convertable
bool IsProtobufMap(const google::protobuf::FieldDescriptor* field);

} // namespace json2pb

#endif // BRPC_JSON2PB_JSON_PROTOBUF_MAP_H
