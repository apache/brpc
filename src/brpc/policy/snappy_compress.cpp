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

#include "base/logging.h"
#include "base/third_party/snappy/snappy.h"
#include "brpc/policy/snappy_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool SnappyCompress(const google::protobuf::Message& res, base::IOBuf* buf) {
    base::IOBuf serialized_pb;
    base::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (res.SerializeToZeroCopyStream(&wrapper)) {
        base::IOBufAsSnappySource source(serialized_pb);
        base::IOBufAsSnappySink sink(*buf);
        return base::snappy::Compress(&source, &sink);
    }
    LOG(WARNING) << "Fail to serialize input pb=" << &res;
    return false;
}

bool SnappyDecompress(const base::IOBuf& data, google::protobuf::Message* req) {
    base::IOBufAsSnappySource source(data);
    base::IOBuf binary_pb;
    base::IOBufAsSnappySink sink(binary_pb);
    if (base::snappy::Uncompress(&source, &sink)) {
        return ParsePbFromIOBuf(req, binary_pb);
    }
    LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
    return false;
}

bool SnappyCompress(const base::IOBuf& in, base::IOBuf* out) {
    base::IOBufAsSnappySource source(in);
    base::IOBufAsSnappySink sink(*out);
    return base::snappy::Compress(&source, &sink);
}

bool SnappyDecompress(const base::IOBuf& in, base::IOBuf* out) {
    base::IOBufAsSnappySource source(in);
    base::IOBufAsSnappySink sink(*out);
    return base::snappy::Uncompress(&source, &sink);
}

}  // namespace policy
} // namespace brpc

