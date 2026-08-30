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


#include <limits>
#include <gflags/gflags.h>
#include "butil/logging.h"
#include "json2pb/json_to_pb.h"
#include "brpc/compress.h"
#include "brpc/protocol.h"
#include "brpc/proto_base.pb.h"

namespace brpc {

DEFINE_uint64(max_decompressed_body_size, 0,
              "Maximum size (in bytes) that a single compressed message body"
              " may decompress to, guarding against decompression bombs."
              " 0 (the default) means 32 times -max_body_size. Raise this"
              " flag explicitly if larger decompressed messages are expected");

uint64_t MaxDecompressedBodySize() {
    const uint64_t limit = FLAGS_max_decompressed_body_size;
    if (limit > 0) {
        return limit;
    }
    const uint64_t base = FLAGS_max_body_size;
    if (base > std::numeric_limits<uint64_t>::max() / 32) {
        return std::numeric_limits<uint64_t>::max();
    }
    return base * 32;
}

static const int MAX_HANDLER_SIZE = 1024;
static CompressHandler s_handler_map[MAX_HANDLER_SIZE] = { { nullptr, nullptr, nullptr } };

int RegisterCompressHandler(CompressType type, 
                            CompressHandler handler) {
    if (nullptr == handler.Compress || nullptr == handler.Decompress) {
        LOG(FATAL) << "Invalid parameter: handler function is NULL";
        return -1;
    }
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(FATAL) << "CompressType=" << type << " is out of range";
        return -1;
    }
    if (s_handler_map[index].Compress != nullptr) {
        LOG(FATAL) << "CompressType=" << type << " was registered";
        return -1;
    }
    s_handler_map[index] = handler;
    return 0;
}

// Find CompressHandler by type.
// Returns nullptr if not found
const CompressHandler* FindCompressHandler(CompressType type) {
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(ERROR) << "CompressType=" << type << " is out of range";
        return nullptr;
    }
    if (nullptr == s_handler_map[index].Compress) {
        return nullptr;
    }
    return &s_handler_map[index];
}

const char* CompressTypeToCStr(CompressType type) {
    if (type == COMPRESS_TYPE_NONE) {
        return "none";
    }
    const CompressHandler* handler = FindCompressHandler(type);
    return (handler != nullptr ? handler->name : "unknown");
}

void ListCompressHandler(std::vector<CompressHandler>* vec) {
    vec->clear();
    for (int i = 0; i < MAX_HANDLER_SIZE; ++i) {
        if (s_handler_map[i].Compress != nullptr) {
            vec->push_back(s_handler_map[i]);
        }
    }
}

bool ParseFromCompressedData(const butil::IOBuf& data, 
                             google::protobuf::Message* msg,
                             CompressType compress_type) {
    if (compress_type == COMPRESS_TYPE_NONE) {
        return ParsePbFromIOBuf(msg, data);
    }
    const CompressHandler* handler = FindCompressHandler(compress_type);
    if (nullptr == handler) {
        return false;
    }

    Deserializer deserializer([msg](google::protobuf::io::ZeroCopyInputStream* input) {
        return msg->ParseFromZeroCopyStream(input);
    });
    return handler->Decompress(data, &deserializer);
}

bool SerializeAsCompressedData(const google::protobuf::Message& msg,
                               butil::IOBuf* buf, CompressType compress_type) {
    if (compress_type == COMPRESS_TYPE_NONE) {
        butil::IOBufAsZeroCopyOutputStream wrapper(buf);
        return msg.SerializeToZeroCopyStream(&wrapper);
    }
    const CompressHandler* handler = FindCompressHandler(compress_type);
    if (nullptr == handler) {
        return false;
    }

    Serializer serializer([&msg](google::protobuf::io::ZeroCopyOutputStream* output) {
        return msg.SerializeToZeroCopyStream(output);
    });
    return handler->Compress(serializer, buf);
}

::google::protobuf::Metadata Serializer::GetMetadata() const {
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = SerializerBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

::google::protobuf::Metadata Deserializer::GetMetadata() const {
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = DeserializerBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

} // namespace brpc
