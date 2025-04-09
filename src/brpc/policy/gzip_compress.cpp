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


namespace brpc {
namespace policy {

template <bool IsCompress>
void LogError(const char* error_message1 = NULL,
              const char* error_message2 = NULL) {
    if (IsCompress) {
        LOG(WARNING) << "Fail to compress. "
                     << (NULL == error_message1 ? "" : error_message1) << " "
                     << (NULL == error_message2 ? "" : error_message2);
    } else {
        LOG(WARNING) << "Fail to decompress."
                     << error_message1 << " "
                     << error_message2;
    }
}

static bool Compress(const google::protobuf::Message& msg, butil::IOBuf* buf,
                     google::protobuf::io::GzipOutputStream::Format format) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options options;
    options.format = format;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, options);
    if (!msg.SerializeToZeroCopyStream(&gzip)) {
        LogError<true>(gzip.ZlibErrorMessage());
        return false;
    }
    return gzip.Close();
}

static bool Compress2Json(const google::protobuf::Message& msg, butil::IOBuf* buf,
                          const json2pb::Pb2JsonOptions& options,
                          google::protobuf::io::GzipOutputStream::Format format) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    gzip_opt.format = format;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, gzip_opt);
    std::string error;
    if (!json2pb::ProtoMessageToJson(msg, &gzip, options, &error)) {
        LogError<true>(error.c_str(), gzip.ZlibErrorMessage());
        return false;
    }
    return gzip.Close();
}

static bool Compress2ProtoJson(const google::protobuf::Message& msg, butil::IOBuf* buf,
                               const json2pb::Pb2ProtoJsonOptions& options,
                               google::protobuf::io::GzipOutputStream::Format format) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    gzip_opt.format = format;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, gzip_opt);
    std::string error;
    if (!json2pb::ProtoMessageToProtoJson(msg, &gzip, options, &error)) {
        LogError<true>(error.c_str(), gzip.ZlibErrorMessage());
        return false;
    }
    return gzip.Close();
}

static bool Compress2ProtoText(const google::protobuf::Message& msg, butil::IOBuf* buf,
                               google::protobuf::io::GzipOutputStream::Format format) {
    butil::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    gzip_opt.format = format;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, gzip_opt);
    if (!google::protobuf::TextFormat::Print(msg, &gzip)) {
        LogError<true>(gzip.ZlibErrorMessage());
        return false;
    }
    return gzip.Close();
}

static bool Decompress(const butil::IOBuf& data, google::protobuf::Message* msg,
                       google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(&wrapper, format);
    if (!ParsePbFromZeroCopyStream(msg, &gzip)) {
        LogError<false>(gzip.ZlibErrorMessage(), gzip.ZlibErrorMessage());
        return false;
    }
    return true;
}

static bool DecompressFromJson(const butil::IOBuf& data,
                               google::protobuf::Message* msg,
                               const json2pb::Json2PbOptions& options,
                               google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(&wrapper, format);
    std::string error;
    if (!json2pb::JsonToProtoMessage(&gzip, msg, options, &error)) {
        LogError<false>(error.c_str(), gzip.ZlibErrorMessage());
        return false;
    }
    return true;
}

static bool DecompressFromProtoJson(const butil::IOBuf& data,
                                    google::protobuf::Message* msg,
                                    const json2pb::ProtoJson2PbOptions& options,
                                    google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(&wrapper, format);
    std::string error;
    if (!json2pb::ProtoJsonToProtoMessage(&gzip, msg, options, &error)) {
        LogError<false>(error.c_str(), gzip.ZlibErrorMessage());
        return false;
    }
    return true;
}

static bool DecompressFromProtoText(const butil::IOBuf& data,
                                    google::protobuf::Message* msg,
                                    google::protobuf::io::GzipInputStream::Format format) {
    butil::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(&wrapper, format);
    std::string error;
    if (!google::protobuf::TextFormat::Parse(&gzip, msg)) {
        LogError<false>(error.c_str(), gzip.ZlibErrorMessage());
        return false;
    }
    return true;
}

bool GzipCompress(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    return Compress(msg, buf, google::protobuf::io::GzipOutputStream::GZIP);
}

bool GzipCompress2Json(const google::protobuf::Message& msg, butil::IOBuf* buf,
                       const json2pb::Pb2JsonOptions& options) {
    return Compress2Json(
        msg, buf, options, google::protobuf::io::GzipOutputStream::GZIP);
}

bool GzipCompress2ProtoJson(const google::protobuf::Message& msg, butil::IOBuf* buf,
                            const json2pb::Pb2ProtoJsonOptions& options) {
    return Compress2ProtoJson(
        msg, buf, options, google::protobuf::io::GzipOutputStream::GZIP);
}

bool GzipCompress2ProtoText(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    return Compress2ProtoText(msg, buf, google::protobuf::io::GzipOutputStream::GZIP);
}

bool GzipDecompress(const butil::IOBuf& data, google::protobuf::Message* msg) {
    return Decompress(data, msg, google::protobuf::io::GzipInputStream::GZIP);
}

bool GzipDecompressFromJson(const butil::IOBuf& data, google::protobuf::Message* msg,
                            const json2pb::Json2PbOptions& options) {
    return DecompressFromJson(
        data, msg, options, google::protobuf::io::GzipInputStream::GZIP);
}

bool GzipDecompressFromProtoJson(const butil::IOBuf& data,
                                 google::protobuf::Message* msg,
                                 const json2pb::ProtoJson2PbOptions& options) {
    return DecompressFromProtoJson(
        data, msg, options, google::protobuf::io::GzipInputStream::GZIP);
}

bool GzipDecompressFromProtoText(const butil::IOBuf& data,
                                 google::protobuf::Message* msg) {
    return DecompressFromProtoText(
        data, msg, google::protobuf::io::GzipInputStream::GZIP);
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
        LogError<true>(out.ZlibErrorMessage());
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
        LogError<false>(in.ZlibErrorMessage());
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

bool ZlibCompress2Json(const google::protobuf::Message& msg, butil::IOBuf* buf,
                       const json2pb::Pb2JsonOptions& options) {
    return Compress2Json(
        msg, buf, options, google::protobuf::io::GzipOutputStream::ZLIB);
}

bool ZlibCompress2ProtoJson(const google::protobuf::Message& msg, butil::IOBuf* buf,
                            const json2pb::Pb2ProtoJsonOptions& options) {
    return Compress2ProtoJson(
        msg, buf, options, google::protobuf::io::GzipOutputStream::ZLIB);
}

bool ZlibCompress2ProtoText(const google::protobuf::Message& msg, butil::IOBuf* buf) {
    return Compress2ProtoText(msg, buf, google::protobuf::io::GzipOutputStream::ZLIB);
}

bool ZlibDecompress(const butil::IOBuf& data,
                    google::protobuf::Message* msg) {
    return Decompress(data, msg, google::protobuf::io::GzipInputStream::ZLIB);
}

bool ZlibDecompressFromJson(const butil::IOBuf& data,
                            google::protobuf::Message* msg,
                            const json2pb::Json2PbOptions& options) {
    return DecompressFromJson(
        data, msg, options, google::protobuf::io::GzipInputStream::ZLIB);
}

bool ZlibDecompressFromProtoJson(const butil::IOBuf& data,
                                 google::protobuf::Message* msg,
                                 const json2pb::ProtoJson2PbOptions& options) {
    return DecompressFromProtoJson(
        data, msg, options, google::protobuf::io::GzipInputStream::ZLIB);
}

bool ZlibDecompressFromProtoText(const butil::IOBuf& data,
                                 google::protobuf::Message* msg) {
    return DecompressFromProtoText(data, msg, google::protobuf::io::GzipInputStream::ZLIB);
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
