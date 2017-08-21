// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 27 14:02:49 2014

#ifndef BRPC_COMPRESS_H
#define BRPC_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include "base/iobuf.h"                           // base::IOBuf
#include "brpc/options.pb.h"                     // CompressType

namespace brpc {

struct CompressHandler {
    // Compress serialized `msg' into `buf'.
    // Returns true on success, false otherwise
    bool (*Compress)(const google::protobuf::Message& msg, base::IOBuf* buf);

    // Parse decompressed `data' as `msg'.
    // Returns true on success, false otherwise
    bool (*Decompress)(const base::IOBuf& data, google::protobuf::Message* msg);

    // Name of the compression algorithm, must be string constant.
    const char* name;
};

// [NOT thread-safe] Register `handler' using key=`type'
// Returns 0 on success, -1 otherwise
int RegisterCompressHandler(CompressType type, CompressHandler handler);

// Returns the `name' of the CompressType if registered
const char* CompressTypeToCStr(CompressType type);

// Put all registered handlers into `vec'.
void ListCompressHandler(std::vector<CompressHandler>* vec);

// Parse decompressed `data' as `msg' using registered `compress_type'.
// Returns true on success, false otherwise
bool ParseFromCompressedData(const base::IOBuf& data,
                             google::protobuf::Message* msg,
                             CompressType compress_type);

// Compress serialized `msg' into `buf' using registered `compress_type'.
// Returns true on success, false otherwise
bool SerializeAsCompressedData(const google::protobuf::Message& msg,
                               base::IOBuf* buf,
                               CompressType compress_type);

} // namespace brpc


#endif // BRPC_COMPRESS_H
