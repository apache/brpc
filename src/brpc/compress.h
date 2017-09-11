// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Rujie Jiang (jiangrujie@baidu.com)
//          Ge,Jun (gejun@baidu.com)

#ifndef BRPC_COMPRESS_H
#define BRPC_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include "butil/iobuf.h"                           // butil::IOBuf
#include "brpc/options.pb.h"                     // CompressType

namespace brpc {

struct CompressHandler {
    // Compress serialized `msg' into `buf'.
    // Returns true on success, false otherwise
    bool (*Compress)(const google::protobuf::Message& msg, butil::IOBuf* buf);

    // Parse decompressed `data' as `msg'.
    // Returns true on success, false otherwise
    bool (*Decompress)(const butil::IOBuf& data, google::protobuf::Message* msg);

    // Name of the compression algorithm, must be string constant.
    const char* name;
};

// [NOT thread-safe] Register `handler' using key=`type'
// Returns 0 on success, -1 otherwise
int RegisterCompressHandler(CompressType type, CompressHandler handler);

// Returns the `name' of the CompressType if registered
const char* CompressTypeToCStr(CompressType type);

// Put all registered handlers into `vec'.
void ListCompressHandler(std::vector<CompressHandler>* vec);

// Parse decompressed `data' as `msg' using registered `compress_type'.
// Returns true on success, false otherwise
bool ParseFromCompressedData(const butil::IOBuf& data,
                             google::protobuf::Message* msg,
                             CompressType compress_type);

// Compress serialized `msg' into `buf' using registered `compress_type'.
// Returns true on success, false otherwise
bool SerializeAsCompressedData(const google::protobuf::Message& msg,
                               butil::IOBuf* buf,
                               CompressType compress_type);

} // namespace brpc


#endif // BRPC_COMPRESS_H
