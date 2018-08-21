// Copyright (c) 2018 Baidu, Inc.
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

// Authors: HaoPeng,Li (happenlee@hotmail.com)

#ifndef BRPC_LZ4_COMPRESS_H
#define BRPC_LZ4_COMPRESS_H

#include <google/protobuf/message.h>          // Message
#include "butil/iobuf.h"                       // IOBuf

const uint32_t BLOCK_BYTES = 1024 * 8;

namespace brpc {
namespace policy {

// Compress serialized `msg' into `buf'.
bool LZ4Compress(const google::protobuf::Message& msg, butil::IOBuf* buf);

// Parse `msg' from decompressed `buf'
bool LZ4Decompress(const butil::IOBuf& data, google::protobuf::Message* msg);

// Put compressed `data' into `out'.
bool LZ4Compress(const butil::IOBuf& data, butil::IOBuf* out);

// Put decompressed `data' into `out'.
bool LZ4Decompress(const butil::IOBuf& data, butil::IOBuf* out);

}  // namespace policy
} // namespace brpc



#endif //BRPC_LZ4_COMPRESS_H
