// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_POLICY_UBRPC2PB_PROTOCOL_H
#define BRPC_POLICY_UBRPC2PB_PROTOCOL_H

#include "mcpack2pb/mcpack2pb.h"
#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/nshead_protocol.h"


namespace brpc {
namespace policy {

void ProcessUbrpcResponse(InputMessageBase* msg);

void SerializeUbrpcCompackRequest(butil::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request);
void SerializeUbrpcMcpack2Request(butil::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request);

void PackUbrpcRequest(butil::IOBuf* buf,
                      SocketMessage**,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller,
                      const butil::IOBuf& request,
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
