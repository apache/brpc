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

// protobuf-json: Conversions between protobuf and json.

#ifndef BRPC_JSON2PB_JSON_TO_PB_H
#define BRPC_JSON2PB_JSON_TO_PB_H

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>    // ZeroCopyInputStream

namespace json2pb {

struct Json2PbOptions {
    Json2PbOptions();

    // Decode string in json using base64 decoding if the type of
    // corresponding field is bytes when this option is turned on.
    // Default: false for baidu-interal, true otherwise.
    bool base64_to_bytes;
};

// Convert `json' to protobuf `message'.
// Returns true on success. `error' (if not NULL) will be set with error
// message on failure.
bool JsonToProtoMessage(const std::string& json,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error = NULL);

// send output to ZeroCopyOutputStream instead of std::string.
bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream *json,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error = NULL);

// Using default Json2PbOptions.
bool JsonToProtoMessage(const std::string& json,
                        google::protobuf::Message* message,
                        std::string* error = NULL);

bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream* stream,
                        google::protobuf::Message* message,
                        std::string* error = NULL);
} // namespace json2pb

#endif // BRPC_JSON2PB_JSON_TO_PB_H
