// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 27 14:04:44 2014

#include <google/protobuf/io/gzip_stream.h>    // GzipXXXStream
#include "base/logging.h"
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

bool GzipCompress(const google::protobuf::Message& msg, base::IOBuf* buf) {
    base::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    gzip_opt.format = google::protobuf::io::GzipOutputStream::GZIP;
    google::protobuf::io::GzipOutputStream gzip(&wrapper, gzip_opt);
    if (!msg.SerializeToZeroCopyStream(&gzip)) {
        LogError(gzip);
        return false;
    }
    return gzip.Close();
}

bool GzipDecompress(const base::IOBuf& data, google::protobuf::Message* msg) {
    base::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream gzip(
            &wrapper, google::protobuf::io::GzipInputStream::GZIP);
    if (!ParsePbFromZeroCopyStream(msg, &gzip)) {
        LogError(gzip);
        return false;
    }
    return true;
}

bool GzipCompress(const base::IOBuf& msg, base::IOBuf* buf,
                  const GzipCompressOptions* options_in) {
    base::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options gzip_opt;
    if (options_in) {
        gzip_opt = *options_in;
    }
    google::protobuf::io::GzipOutputStream out(&wrapper, gzip_opt);
    base::IOBufAsZeroCopyInputStream in(msg);
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
    const base::IOBuf& data, base::IOBuf* msg,
    google::protobuf::io::GzipInputStream::Format format) {
    base::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream in(&wrapper, format);
    base::IOBufAsZeroCopyOutputStream out(msg);
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

bool ZlibCompress(const google::protobuf::Message& res, base::IOBuf* buf) {
    base::IOBufAsZeroCopyOutputStream wrapper(buf);
    google::protobuf::io::GzipOutputStream::Options zlib_opt;
    zlib_opt.format = google::protobuf::io::GzipOutputStream::ZLIB;
    google::protobuf::io::GzipOutputStream zlib(&wrapper, zlib_opt);
    return res.SerializeToZeroCopyStream(&zlib) && zlib.Close();
}

bool ZlibDecompress(const base::IOBuf& data, google::protobuf::Message* req) {
    base::IOBufAsZeroCopyInputStream wrapper(data);
    google::protobuf::io::GzipInputStream zlib(
        &wrapper, google::protobuf::io::GzipInputStream::ZLIB);
    return ParsePbFromZeroCopyStream(req, &zlib);
}

bool GzipDecompress(const base::IOBuf& data, base::IOBuf* msg) {
    return GzipDecompressBase(
        data, msg, google::protobuf::io::GzipInputStream::GZIP);
}

bool ZlibDecompress(const base::IOBuf& data, base::IOBuf* msg) {
    return GzipDecompressBase(
        data, msg, google::protobuf::io::GzipInputStream::ZLIB);
}

}  // namespace policy
} // namespace brpc

