// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/01/20 14:42:14

#ifndef BRPC_POLICY_SNAPPY_COMPRESS_H
#define BRPC_POLICY_SNAPPY_COMPRESS_H

#include <google/protobuf/message.h>          // Message
#include "base/iobuf.h"                       // IOBuf


namespace brpc {
namespace policy {

// Compress serialized `msg' into `buf'.
bool SnappyCompress(const google::protobuf::Message& msg, base::IOBuf* buf);

// Parse `msg' from decompressed `buf'
bool SnappyDecompress(const base::IOBuf& data, google::protobuf::Message* msg);

// Put compressed `in' into `out'.
bool SnappyCompress(const base::IOBuf& in, base::IOBuf* out);

// Put decompressed `in' into `out'.
bool SnappyDecompress(const base::IOBuf& in, base::IOBuf* out);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_SNAPPY_COMPRESS_H
