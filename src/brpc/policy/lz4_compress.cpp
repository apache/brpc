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


#include "butil/logging.h"
#include "butil/third_party/lz4/lz4.h"
#include "brpc/policy/lz4_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool Lz4Compress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    return false;
}

bool Lz4Decompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    return true;
}

bool Lz4Compress(const butil::IOBuf& in, butil::IOBuf* out) {
    return true;
}

bool Lz4Decompress(const butil::IOBuf& in, butil::IOBuf* out) {
    return true;
}

}  // namespace policy
} // namespace brpc
