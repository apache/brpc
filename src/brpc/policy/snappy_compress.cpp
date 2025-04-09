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


#include <google/protobuf/text_format.h>
#include "butil/logging.h"
#include "butil/third_party/snappy/snappy.h"
#include "brpc/policy/snappy_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool SnappyCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (msg.SerializeToZeroCopyStream(&wrapper)) {
        return SnappyCompress(serialized_pb, buf);
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &msg;
    return false;
}

bool SnappyCompress2Json(const google::protobuf::Message& msg, butil::IOBuf* buf,
                         const json2pb::Pb2JsonOptions& options) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    std::string error;
    if (json2pb::ProtoMessageToJson(msg, &wrapper, options, &error)) {
        return SnappyCompress(serialized_pb, buf);
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &msg << " : " << error;
    return false;

}

bool SnappyCompress2ProtoJson(const google::protobuf::Message& msg, butil::IOBuf* buf,
                              const json2pb::Pb2ProtoJsonOptions& options) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    std::string error;
    if (json2pb::ProtoMessageToProtoJson(msg, &wrapper, options, &error)) {
        return SnappyCompress(serialized_pb, buf);
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &msg << " : " << error;
    return false;
}

bool SnappyCompress2ProtoText(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (google::protobuf::TextFormat::Print(msg, &wrapper)) {
        return SnappyCompress(serialized_pb, buf);
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &msg;
    return false;
}

bool SnappyDecompress(const butil::IOBuf& data, google::protobuf::Message* msg) {
    butil::IOBuf binary_pb;
    if (SnappyDecompress(data, &binary_pb)) {
        return ParsePbFromIOBuf(msg, binary_pb);
    }
    LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
    return false;
}

bool SnappyDecompressFromJson(const butil::IOBuf& data, google::protobuf::Message* msg,
                              const json2pb::Json2PbOptions& options) {
    butil::IOBuf json;
    if (!SnappyDecompress(data, &json)) {
        LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
        return false;
    }

    butil::IOBufAsZeroCopyInputStream wrapper(json);
    std::string error;
    if (json2pb::JsonToProtoMessage(&wrapper, msg, options, &error)) {
        return true;
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &msg << " : " << error;
    return false;
}
bool SnappyDecompressFromProtoJson(const butil::IOBuf& data, google::protobuf::Message* msg,
                                   const json2pb::ProtoJson2PbOptions& options) {
    butil::IOBuf json;
    if (!SnappyDecompress(data, &json)) {
        LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
        return false;
    }

    butil::IOBufAsZeroCopyInputStream wrapper(json);
    std::string error;
    if (json2pb::ProtoJsonToProtoMessage(&wrapper, msg, options, &error)) {
        return true;
    }
    LOG(WARNING) << "Fail to serialize input pb=" << &msg << " : " << error;
    return false;
}

bool SnappyDecompressFromProtoText(const butil::IOBuf& data, google::protobuf::Message* msg) {
    butil::IOBuf json;
    if (!SnappyDecompress(data, &json)) {
        LOG(WARNING) << "Fail to snappy::Uncompress, size=" << data.size();
        return false;
    }

    butil::IOBufAsZeroCopyInputStream wrapper(json);
    if (google::protobuf::TextFormat::Parse(&wrapper, msg)) {
        return true;
    }
    LOG(WARNING) << "Fail to serialize input pb=" << &msg;
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
