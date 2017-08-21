// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 27 14:03:53 2014

#include "base/logging.h"
#include "brpc/compress.h"
#include "brpc/protocol.h"


namespace brpc {

static const int MAX_HANDLER_SIZE = 1024;
static CompressHandler s_handler_map[MAX_HANDLER_SIZE] = { { NULL, NULL, NULL } };

int RegisterCompressHandler(CompressType type, 
                            CompressHandler handler) {
    if (NULL == handler.Compress || NULL == handler.Decompress) {
        LOG(FATAL) << "Invalid parameter: handler function is NULL";
        return -1;
    }
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(FATAL) << "CompressType=" << type << " is out of range";
        return -1;
    }
    if (s_handler_map[index].Compress != NULL) {
        LOG(FATAL) << "CompressType=" << type << " was registered";
        return -1;
    }
    s_handler_map[index] = handler;
    return 0;
}

// Find CompressHandler by type.
// Returns NULL if not found
inline const CompressHandler* FindCompressHandler(CompressType type) {
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(FATAL) << "CompressType=" << type << " is out of range";
        return NULL;
    }
    if (NULL == s_handler_map[index].Compress) {
        return NULL;
    }
    return &s_handler_map[index];
}

const char* CompressTypeToCStr(CompressType type) {
    if (type == COMPRESS_TYPE_NONE) {
        return "none";
    }
    const CompressHandler* handler = FindCompressHandler(type);
    return (handler != NULL ? handler->name : "unknown");
}

void ListCompressHandler(std::vector<CompressHandler>* vec) {
    vec->clear();
    for (int i = 0; i < MAX_HANDLER_SIZE; ++i) {
        if (s_handler_map[i].Compress != NULL) {
            vec->push_back(s_handler_map[i]);
        }
    }
}

bool ParseFromCompressedData(const base::IOBuf& data, 
                             google::protobuf::Message* msg,
                             CompressType compress_type) {
    if (compress_type == COMPRESS_TYPE_NONE) {
        return ParsePbFromIOBuf(msg, data);
    }
    const CompressHandler* handler = FindCompressHandler(compress_type);
    if (NULL != handler) {
        return handler->Decompress(data, msg);
    }
    return false;
}

bool SerializeAsCompressedData(const google::protobuf::Message& msg,
                               base::IOBuf* buf, CompressType compress_type) {
    if (compress_type == COMPRESS_TYPE_NONE) {
        base::IOBufAsZeroCopyOutputStream wrapper(buf);
        return msg.SerializeToZeroCopyStream(&wrapper);
    }
    const CompressHandler* handler = FindCompressHandler(compress_type);
    if (NULL != handler) {
        return handler->Compress(msg, buf);
    }
    return false;
}

} // namespace brpc

