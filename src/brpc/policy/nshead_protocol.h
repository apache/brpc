// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Mar 30 13:11:01 2015

#ifndef BRPC_POLICY_NSHEAD_PROTOCOL_H
#define BRPC_POLICY_NSHEAD_PROTOCOL_H

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

// Parse binary format of nshead
ParseResult ParseNsheadMessage(base::IOBuf* source, Socket* socket, bool read_eof, const void *arg);

// Actions to a (client) request in nshead format
void ProcessNsheadRequest(InputMessageBase* msg);

// Actions to a (server) response in nshead format
void ProcessNsheadResponse(InputMessageBase* msg);

void SerializeNsheadRequest(base::IOBuf* request_buf, Controller* controller,
                            const google::protobuf::Message* request);

void PackNsheadRequest(
    base::IOBuf* packet_buf,
    SocketMessage**,
    uint64_t correlation_id,
    const google::protobuf::MethodDescriptor*,
    Controller* controller,
    const base::IOBuf&,
    const Authenticator*);

// Verify authentication information in nshead format
bool VerifyNsheadRequest(const InputMessageBase *msg);

} // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_NSHEAD_PROTOCOL_H

