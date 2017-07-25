// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Dec 23 22:54:08 CST 2015

#ifndef BRPC_POLICY_UBRPC2PB_PROTOCOL_H
#define BRPC_POLICY_UBRPC2PB_PROTOCOL_H

#include "mcpack2pb/mcpack2pb.h"
#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/nshead_protocol.h"


namespace brpc {
namespace policy {

void ProcessUbrpcResponse(InputMessageBase* msg);

void SerializeUbrpcCompackRequest(base::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request);
void SerializeUbrpcMcpack2Request(base::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request);

void PackUbrpcRequest(base::IOBuf* buf,
                      SocketMessage**,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller,
                      const base::IOBuf& request,
                      const Authenticator* auth);

class UbrpcAdaptor : public NsheadPbServiceAdaptor {
public:
    explicit UbrpcAdaptor(mcpack2pb::SerializationFormat format)
        : _format(format) {}
    
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

private:
    mcpack2pb::SerializationFormat _format;
};

class UbrpcCompackAdaptor : public UbrpcAdaptor {
public:
    UbrpcCompackAdaptor() : UbrpcAdaptor(mcpack2pb::FORMAT_COMPACK) {}
};

class UbrpcMcpack2Adaptor : public UbrpcAdaptor {
public:
    UbrpcMcpack2Adaptor() : UbrpcAdaptor(mcpack2pb::FORMAT_MCPACK_V2) {}
};

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_UBRPC2PB_PROTOCOL_H
