// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/01/19 19:38:59

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

