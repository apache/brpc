// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Apr 20 11:21:40 2015

#ifndef BRPC_POLICY_PUBLIC_PBRPC_PROTOCOL_H
#define BRPC_POLICY_PUBLIC_PBRPC_PROTOCOL_H

#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/nshead_protocol.h"             


namespace brpc {
namespace policy {

// Actions to a (server) response in public-pbrpc format.
void ProcessPublicPbrpcResponse(InputMessageBase* msg);

void SerializePublicPbrpcRequest(base::IOBuf* buf, Controller* cntl,
                                 const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackPublicPbrpcRequest(base::IOBuf* buf,
                            SocketMessage**,
                            uint64_t correlation_id,
                            const google::protobuf::MethodDescriptor* method,
                            Controller* controller,
                            const base::IOBuf& request,
                            const Authenticator* auth);

class PublicPbrpcServiceAdaptor : public NsheadPbServiceAdaptor {
public:
    void ParseNsheadMeta(
        const Server& svr, const NsheadMessage& request, Controller*,
        NsheadMeta* out_meta) const;

    void ParseRequestFromIOBuf(
        const NsheadMeta& meta, const NsheadMessage& raw_req,
        Controller* controller, google::protobuf::Message* pb_req) const;

    void SerializeResponseToIOBuf(
        const NsheadMeta& meta, Controller* controller,
        const google::protobuf::Message* pb_res,
        NsheadMessage* raw_res) const;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_PUBLIC_PBRPC_PROTOCOL_H
