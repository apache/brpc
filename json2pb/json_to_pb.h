// protobuf-json: Conversions between protobuf and json.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date:  2014-10-29 18:30:33

#ifndef BRPC_JSON2PB_JSON_TO_PB_H
#define BRPC_JSON2PB_JSON_TO_PB_H

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>    // ZeroCopyInputStream

namespace json2pb {
// Rules: http://wiki.baidu.com/display/RPC/Json+%3C%3D%3E+Protobuf

// Convert `json' to protobuf `message'.
// Returns true on success. `error' (if not NULL) will be set with error
// message on failure.
bool JsonToProtoMessage(const std::string& json,
                        google::protobuf::Message* message,
                        std::string* error = NULL);

// send output to ZeroCopyOutputStream instead of std::string.
bool JsonToProtoMessage(google::protobuf::io::ZeroCopyInputStream *json,
                        google::protobuf::Message* message,
                        std::string* error = NULL);
}

#endif // BRPC_JSON2PB_JSON_TO_PB_H
