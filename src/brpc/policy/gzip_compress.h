// Copyright (c) 2014 baidu-rpc authors.
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

#ifndef BRPC_POLICY_GZIP_COMPRESS_H
#define BRPC_POLICY_GZIP_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include <google/protobuf/io/gzip_stream.h>
#include "base/iobuf.h"                           // base::IOBuf


namespace brpc {
namespace policy {

typedef google::protobuf::io::GzipOutputStream::Options GzipCompressOptions;

// Compress serialized `msg' into `buf'.
bool GzipCompress(const google::protobuf::Message& msg, base::IOBuf* buf);
bool ZlibCompress(const google::protobuf::Message& msg, base::IOBuf* buf);

// Parse `msg' from decompressed `buf'.
bool GzipDecompress(const base::IOBuf& buf, google::protobuf::Message* msg);
bool ZlibDecompress(const base::IOBuf& buf, google::protobuf::Message* msg);

// Put compressed `in' into `out'.
bool GzipCompress(const base::IOBuf& in, base::IOBuf* out,
                  const GzipCompressOptions*);

// Put decompressed `in' into `out'.
bool GzipDecompress(const base::IOBuf& in, base::IOBuf* out);
bool ZlibDecompress(const base::IOBuf& in, base::IOBuf* out);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_GZIP_COMPRESS_H
