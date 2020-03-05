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


#ifndef BRPC_POLICY_GZIP_COMPRESS_H
#define BRPC_POLICY_GZIP_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include <google/protobuf/io/gzip_stream.h>
#include "butil/iobuf.h"                           // butil::IOBuf


namespace brpc {
namespace policy {

typedef google::protobuf::io::GzipOutputStream::Options GzipCompressOptions;

// Compress serialized `msg' into `buf'.
bool GzipCompress(const google::protobuf::Message& msg, butil::IOBuf* buf);
bool ZlibCompress(const google::protobuf::Message& msg, butil::IOBuf* buf);

// Parse `msg' from decompressed `buf'.
bool GzipDecompress(const butil::IOBuf& buf, google::protobuf::Message* msg);
bool ZlibDecompress(const butil::IOBuf& buf, google::protobuf::Message* msg);

// Put compressed `in' into `out'.
bool GzipCompress(const butil::IOBuf& in, butil::IOBuf* out,
                  const GzipCompressOptions*);

// Put decompressed `in' into `out'.
bool GzipDecompress(const butil::IOBuf& in, butil::IOBuf* out);
bool ZlibDecompress(const butil::IOBuf& in, butil::IOBuf* out);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_GZIP_COMPRESS_H
