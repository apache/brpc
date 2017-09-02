// Copyright (c) 2015 baidu-rpc authors.
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

#ifndef BRPC_POLICY_SNAPPY_COMPRESS_H
#define BRPC_POLICY_SNAPPY_COMPRESS_H

#include <google/protobuf/message.h>          // Message
#include "base/iobuf.h"                       // IOBuf


namespace brpc {
namespace policy {

// Compress serialized `msg' into `buf'.
bool SnappyCompress(const google::protobuf::Message& msg, base::IOBuf* buf);

// Parse `msg' from decompressed `buf'
bool SnappyDecompress(const base::IOBuf& data, google::protobuf::Message* msg);

// Put compressed `in' into `out'.
bool SnappyCompress(const base::IOBuf& in, base::IOBuf* out);

// Put decompressed `in' into `out'.
bool SnappyDecompress(const base::IOBuf& in, base::IOBuf* out);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_SNAPPY_COMPRESS_H
