// protobuf-json: Conversions between protobuf and json.
// Copyright (c) 2014 Baidu, Inc.
// Date:  2014-10-29 18:30:33

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
