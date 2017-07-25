// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/10/13 11:49:20

#ifndef  BRPC_STREAMING_RPC_PROTOCOL_H
#define  BRPC_STREAMING_RPC_PROTOCOL_H

#include "brpc/protocol.h"
#include "brpc/streaming_rpc_meta.pb.h"


namespace brpc {
namespace policy {

void PackStreamMessage(base::IOBuf* out,
                       const StreamFrameMeta &fm,
                       const base::IOBuf *data);

ParseResult ParseStreamingMessage(base::IOBuf* source, Socket* socket,
                                  bool read_eof, const void* arg);

void ProcessStreamingMessage(InputMessageBase* msg);

void SendStreamRst(Socket* sock, int64_t remote_stream_id);

void SendStreamClose(Socket *sock, int64_t remote_stream_id,
                     int64_t source_stream_id);

int SendStreamData(Socket* sock, const base::IOBuf* data,
                   int64_t remote_stream_id, int64_t source_stream_id);

}  // namespace policy
} // namespace brpc


#endif  //BRPC_STREAMING_RPC_PROTOCOL_H
