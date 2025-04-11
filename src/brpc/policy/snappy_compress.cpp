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
#include "brpc/compress.h"

namespace brpc {
namespace policy {

bool SnappyCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    bool ok;
    if (msg.GetDescriptor() == Serializer::descriptor()) {
        ok = ((const Serializer&)msg).SerializeTo(&wrapper);
    } else {
        ok = msg.SerializeToZeroCopyStream(&wrapper);
    }
    if (!ok) {
        LOG(WARNING) << "Fail to serialize input pb="
                     << msg.GetDescriptor()->full_name();
        return false;
    }

    ok = SnappyCompress(serialized_pb, buf);
    if (!ok) {
        LOG(WARNING) << "Fail to snappy::Compress, size="
                     << serialized_pb.size();
    }
    return ok;
}

bool SnappyDecompress(const butil::IOBuf& data, google::protobuf::Message* msg) {
    butil::IOBuf binary_pb;
    if (!SnappyDecompress(data, &binary_pb)) {
        LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
        return false;
    }

    bool ok;
    butil::IOBufAsZeroCopyInputStream stream(binary_pb);
    if (msg->GetDescriptor() == Deserializer::descriptor()) {
        ok = ((Deserializer*)msg)->DeserializeFrom(&stream);
    } else {
        ok = msg->ParseFromZeroCopyStream(&stream);
    }
    if (!ok) {
        LOG(WARNING) << "Fail to eserialize input message="
                     << msg->GetDescriptor()->full_name();
    }
    return ok;
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
