// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Oct 28 17:11:18 2014

#ifndef BRPC_POLICY_GZIP_COMPRESS_H
#define BRPC_POLICY_GZIP_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include <google/protobuf/io/gzip_stream.h>
#include "base/iobuf.h"                           // base::IOBuf


namespace brpc {
namespace policy {

typedef google::protobuf::io::GzipOutputStream::Options GzipCompressOptions;

// Compress serialized `msg' into `buf'.
bool GzipCompress(const google::protobuf::Message& msg, base::IOBuf* buf);
bool ZlibCompress(const google::protobuf::Message& msg, base::IOBuf* buf);

// Parse `msg' from decompressed `buf'.
bool GzipDecompress(const base::IOBuf& buf, google::protobuf::Message* msg);
bool ZlibDecompress(const base::IOBuf& buf, google::protobuf::Message* msg);

// Put compressed `in' into `out'.
bool GzipCompress(const base::IOBuf& in, base::IOBuf* out,
                  const GzipCompressOptions*);

// Put decompressed `in' into `out'.
bool GzipDecompress(const base::IOBuf& in, base::IOBuf* out);
bool ZlibDecompress(const base::IOBuf& in, base::IOBuf* out);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_GZIP_COMPRESS_H
