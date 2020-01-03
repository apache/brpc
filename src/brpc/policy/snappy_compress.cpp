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
#include "butil/third_party/snappy/snappy.h"
#include "brpc/policy/snappy_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool SnappyCompress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (res.SerializeToZeroCopyStream(&wrapper)) {
        butil::IOBufAsSnappySource source(serialized_pb);
        butil::IOBufAsSnappySink sink(*buf);
        return butil::snappy::Compress(&source, &sink);
    }
    LOG(WARNING) << "Fail to serialize input pb=" << &res;
    return false;
}

bool SnappyDecompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    butil::IOBufAsSnappySource source(data);
    butil::IOBuf binary_pb;
    butil::IOBufAsSnappySink sink(binary_pb);
    if (butil::snappy::Uncompress(&source, &sink)) {
        return ParsePbFromIOBuf(req, binary_pb);
    }
    LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
    return false;
}

bool SnappyCompress(const butil::IOBuf& in, butil::IOBuf* out) {
    butil::IOBufAsSnappySource source(in);
    butil::IOBufAsSnappySink sink(*out);
    return butil::snappy::Compress(&source, &sink);
}

bool SnappyDecompress(const butil::IOBuf& in, butil::IOBuf* out) {
    butil::IOBufAsSnappySource source(in);
    butil::IOBufAsSnappySink sink(*out);
    return butil::snappy::Uncompress(&source, &sink);
}

}  // namespace policy
} // namespace brpc
