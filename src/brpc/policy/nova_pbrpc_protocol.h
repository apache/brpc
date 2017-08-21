// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/18 11:13:19

#ifndef BRPC_POLICY_NOVA_PBRPC_PROTOCOL_H
#define BRPC_POLICY_NOVA_PBRPC_PROTOCOL_H

#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/nshead_protocol.h" 


namespace brpc {
namespace policy {

// Actions to a (server) response in nova_pbrpc format.
void ProcessNovaResponse(InputMessageBase* msg);

void SerializeNovaRequest(base::IOBuf* buf, Controller* cntl,
                                 const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackNovaRequest(base::IOBuf* buf,
                     SocketMessage** user_message_out,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* controller,
                     const base::IOBuf& request,
                     const Authenticator* auth);

class NovaServiceAdaptor : public NsheadPbServiceAdaptor {
public:
    void ParseNsheadMeta(const Server& svr,
                        const NsheadMessage& request,
                        Controller*,
                        NsheadMeta* out_meta) const;

    void ParseRequestFromIOBuf(
        const NsheadMeta& meta, const NsheadMessage& ns_req,
        Controller* controller, google::protobuf::Message* pb_req) const;

    void SerializeResponseToIOBuf(
        const NsheadMeta& meta,
        Controller* controller,
        const google::protobuf::Message* pb_res,
        NsheadMessage* ns_res) const;
};

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_NOVA_PBRPC_PROTOCOL_H
