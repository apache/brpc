// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Apr 19 18:17:26 CST 2016

#ifndef BRPC_POLICY_NSHEAD_MCPACK_PROTOCOL_H
#define BRPC_POLICY_NSHEAD_MCPACK_PROTOCOL_H

#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/nshead_protocol.h" 


namespace brpc {
namespace policy {

// Actions to a (server) response in nshead+mcpack format.
void ProcessNsheadMcpackResponse(InputMessageBase* msg);

void SerializeNsheadMcpackRequest(base::IOBuf* buf, Controller* cntl,
                                 const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackNsheadMcpackRequest(base::IOBuf* buf,
                             SocketMessage**,
                             uint64_t correlation_id,
                             const google::protobuf::MethodDescriptor* method,
                             Controller* controller,
                             const base::IOBuf& request,
                             const Authenticator* auth);

class NsheadMcpackAdaptor : public NsheadPbServiceAdaptor {
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


#endif // BRPC_POLICY_NSHEAD_MCPACK_PROTOCOL_H
