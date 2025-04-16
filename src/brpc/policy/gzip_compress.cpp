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


#include <google/protobuf/io/gzip_stream.h>    // GzipXXXStream
#include <google/protobuf/text_format.h>
#include "butil/logging.h"
#include "brpc/policy/gzip_compress.h"
#include "brpc/protocol.h"
#include "brpc/compress.h"

namespace brpc {
namespace policy {

const char* Format2CStr(google::protobuf::io::GzipOutputStream::Format format) {
    switch (format) {
    case google::protobuf::io::GzipOutputStream::GZIP:
        return "gzip";
    case google::protobuf::io::GzipOutputStream::ZLIB:
        return "zlib";
    default:
        return "unknown";
    }
}

const char* Format2CStr(google::protobuf::io::GzipInputStream::Format format) {
    switch (format) {
    case google::protobuf::io::GzipInputStream::GZIP:
        return "gzip";
    case google::protobuf::io::GzipInputStream::ZLIB:
        return "zlib";
    default:
        return "unknown";
    }
}

static bool Compress(const google::protobuf::Message& msg, butil::IOBuf* buf,
                     google::protobuf::io::GzipOutputStream::Format format) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    GzipCompressOptions options;
    options.format = format;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, options);
    bool ok;
    if (msg.GetDescriptor() == Serializer::descriptor()) {
        ok = ((const Serializer&)msg).SerializeTo(&gzip);
    } else {
        ok = msg.SerializeToZeroCopyStream(&gzip);
    }
    if (!ok) {
        LOG(WARNING) << "Fail to serialize input message="
                     << msg.GetDescriptor()->full_name()
                     << ", format=" << Format2CStr(format) << " : "
                     << (NULL == gzip.ZlibErrorMessage() ? "" : gzip.ZlibErrorMessage());
    }
    return ok && gzip.Close();
}

static bool Decompress(const butil::IOBuf& data, google::protobuf::Message* msg,
                       google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(&wrapper, format);
    bool ok;
    if (msg->GetDescriptor() == Deserializer::descriptor()) {
        ok = ((Deserializer*)msg)->DeserializeFrom(&gzip);
    } else {
        ok = msg->ParseFromZeroCopyStream(&gzip);
    }
    if (!ok) {
        LOG(WARNING) << "Fail to deserialize input message="
                     << msg->GetDescriptor()->full_name()
                     << ", format=" << Format2CStr(format) << " : "
                     << (NULL == gzip.ZlibErrorMessage() ? "" : gzip.ZlibErrorMessage());
    }
    return ok;
}

bool GzipCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    return Compress(msg, buf, google::protobuf::io::GzipOutputStream::GZIP);
}

bool GzipDecompress(const butil::IOBuf& data, google::protobuf::Message* msg) {
    return Decompress(data, msg, google::protobuf::io::GzipInputStream::GZIP);
}

bool GzipCompress(const butil::IOBuf& msg, butil::IOBuf* buf,
                  const GzipCompressOptions* options_in) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    GzipCompressOptions gzip_opt;
    if (options_in) {
        gzip_opt = *options_in;
    }
    google::protobuf::io::GzipOutputStream out(&wrapper, gzip_opt);
    butil::IOBufAsZeroCopyInputStream in(msg);
    const void* data_in = NULL;
    int size_in = 0;
    void* data_out = NULL;
    int size_out = 0;
    while (1) {
        if (size_out == 0 && !out.Next(&data_out, &size_out)) {
            break;
        }
        if (size_in == 0 && !in.Next(&data_in, &size_in)) {
            break;
        }
        const int size_cp = std::min(size_in, size_out);
        memcpy(data_out, data_in, size_cp);
        size_in -= size_cp;
        data_in = (char*)data_in + size_cp;
        size_out -= size_cp;
        data_out = (char*)data_out + size_cp;
    }
    if (size_in != 0 || (size_t)in.ByteCount() != msg.size()) {
        // If any stage is not fully consumed, something went wrong.
        LOG(WARNING) << "Fail to compress, format=" << Format2CStr(gzip_opt.format)
                     << " : " << out.ZlibErrorMessage();
        return false;
    }
    if (size_out != 0) {
        out.BackUp(size_out);
    }
    return out.Close();
}

inline bool GzipDecompressBase(
    const butil::IOBuf& data, butil::IOBuf* msg,
    google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream in(&wrapper, format);
    butil::IOBufAsZeroCopyOutputStream out(msg);
    const void* data_in = NULL;
    int size_in = 0;
    void* data_out = NULL;
    int size_out = 0;
    while (1) {
        if (size_out == 0 && !out.Next(&data_out, &size_out)) {
            break;
        }
        if (size_in == 0 && !in.Next(&data_in, &size_in)) {
            break;
        }
        const int size_cp = std::min(size_in, size_out);
        memcpy(data_out, data_in, size_cp);
        size_in -= size_cp;
        data_in = (char*)data_in + size_cp;
        size_out -= size_cp;
        data_out = (char*)data_out + size_cp;
    }
    if (size_in != 0 ||
        (size_t)wrapper.ByteCount() != data.size() ||
        in.Next(&data_in, &size_in)) {
        // If any stage is not fully consumed, something went wrong.
        // Here we call in.Next addtitionally to make sure that the gzip
        // "blackbox" does not have buffer left.
        LOG(WARNING) << "Fail to decompress, format=" << Format2CStr(format)
                     << " : " << in.ZlibErrorMessage();
        return false;
    }
    if (size_out != 0) {
        out.BackUp(size_out);
    }
    return true;
}

bool ZlibCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    return Compress(msg, buf, google::protobuf::io::GzipOutputStream::ZLIB);
}

bool ZlibDecompress(const butil::IOBuf& data,
                    google::protobuf::Message* msg) {
    return Decompress(data, msg, google::protobuf::io::GzipInputStream::ZLIB);
}

bool GzipDecompress(const butil::IOBuf& data, butil::IOBuf* msg) {
    return GzipDecompressBase(
        data, msg, google::protobuf::io::GzipInputStream::GZIP);
}

bool ZlibDecompress(const butil::IOBuf& data, butil::IOBuf* msg) {
    return GzipDecompressBase(
        data, msg, google::protobuf::io::GzipInputStream::ZLIB);
}

}  // namespace policy
} // namespace brpc
