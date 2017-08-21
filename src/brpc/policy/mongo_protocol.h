// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Oct 08 13:11:01 2015

#ifndef BRPC_POLICY_MONGO_PROTOCOL_H
#define BRPC_POLICY_MONGO_PROTOCOL_H

#include "brpc/protocol.h"
#include "brpc/input_messenger.h"


namespace brpc {
namespace policy {

// Parse binary format of mongo
ParseResult ParseMongoMessage(base::IOBuf* source, Socket* socket, bool read_eof, const void *arg);

// Actions to a (client) request in mongo format
void ProcessMongoRequest(InputMessageBase* msg);

} // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_MONGO_PROTOCOL_H

