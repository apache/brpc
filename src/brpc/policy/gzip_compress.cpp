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
#include "butil/logging.h"
#include "brpc/policy/gzip_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

static void LogError(const google::protobuf::io::GzipOutputStream& gzip) {
    if (gzip.ZlibErrorMessage()) {
        LOG(WARNING) << "Fail to decompress: " << gzip.ZlibErrorMessage();
    } else {
        LOG(WARNING) << "Fail to decompress.";
    }
}

static void LogError(const google::protobuf::io::GzipInputStream& gzip) {
    if (gzip.ZlibErrorMessage()) {
        LOG(WARNING) << "Fail to decompress: " << gzip.ZlibErrorMessage();
    } else {
        LOG(WARNING) << "Fail to decompress.";
    }
}

bool GzipCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    gzip_opt.format = google::protobuf::io::GzipOutputStream::GZIP;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, gzip_opt);
    if (!msg.SerializeToZeroCopyStream(&gzip)) {
        LogError(gzip);
        return false;
    }
    return gzip.Close();
}

bool GzipDecompress(const butil::IOBuf& data, google::protobuf::Message* msg) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(
            &wrapper, google::protobuf::io::GzipInputStream::GZIP);
    if (!ParsePbFromZeroCopyStream(msg, &gzip)) {
        LogError(gzip);
        return false;
    }
    return true;
}

bool GzipCompress(const butil::IOBuf& msg, butil::IOBuf* buf,
                  const GzipCompressOptions* options_in) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
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
        LogError(out);
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
        LogError(in);
        return false;
    }
    if (size_out != 0) {
        out.BackUp(size_out);
    }
    return true;
}

bool ZlibCompress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options zlib_opt;
    zlib_opt.format = google::protobuf::io::GzipOutputStream::ZLIB;
    google::protobuf::io::GzipOutputStream zlib(&wrapper, zlib_opt);
    return res.SerializeToZeroCopyStream(&zlib) && zlib.Close();
}

bool ZlibDecompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream zlib(
        &wrapper, google::protobuf::io::GzipInputStream::ZLIB);
    return ParsePbFromZeroCopyStream(req, &zlib);
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
