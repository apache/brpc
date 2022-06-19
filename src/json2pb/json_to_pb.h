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

#include "json2pb/zero_copy_stream_reader.h"
#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>    // ZeroCopyInputStream

namespace json2pb {

struct Json2PbOptions {
    Json2PbOptions();

    // Decode string in json using base64 decoding if the type of
    // corresponding field is bytes when this option is turned on.
    // Default: false for baidu-interal, true otherwise.
    bool base64_to_bytes;

    // Allow decoding json array iff there is only one repeated field.
    // Default: false.
    bool array_to_single_repeated;

    // Allow more bytes remaining in the input after parsing the first json
    // object. Useful when the input contains more than one json object.
    bool allow_remaining_bytes_after_parsing;
};

// Convert `json' to protobuf `message'.
// Returns true on success. `error' (if not NULL) will be set with error
// message on failure.
//
// [When options.allow_remaining_bytes_after_parsing is true]
// * `parse_offset' will be set with #bytes parsed
// * the function still returns false on empty document but the `error' is set
//   to empty string instead of `The document is empty'.
bool JsonToProtoMessage(const std::string& json,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error = nullptr,
                        size_t* parsed_offset = nullptr);

// Use ZeroCopyInputStream as input instead of std::string.
bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream *json,
                        google::protobuf::Message *message,
                        const Json2PbOptions &options,
                        std::string *error = nullptr,
                        size_t *parsed_offset = nullptr);

// Use ZeroCopyStreamReader as input instead of std::string.
// If you need to parse multiple jsons from IOBuf, you should use this
// overload instead of the ZeroCopyInputStream one which bases on this
// and recreates a ZeroCopyStreamReader internally that can't be reused
// between continuous calls.
bool JsonToProtoMessage(ZeroCopyStreamReader *json,
                        google::protobuf::Message* message,
                        const Json2PbOptions& options,
                        std::string* error = nullptr,
                        size_t* parsed_offset = nullptr);

// Using default Json2PbOptions.
bool JsonToProtoMessage(const std::string& json,
                        google::protobuf::Message* message,
                        std::string* error = nullptr);

bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream* stream,
                        google::protobuf::Message* message,
                        std::string* error = nullptr);
} // namespace json2pb

#endif // BRPC_JSON2PB_JSON_TO_PB_H
