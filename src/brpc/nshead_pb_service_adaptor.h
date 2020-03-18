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


#ifndef BRPC_NSHEAD_PB_SERVICE_ADAPTOR_H
#define BRPC_NSHEAD_PB_SERVICE_ADAPTOR_H

#include "brpc/nshead_service.h"             // NsheadService
#include "brpc/nshead_meta.pb.h"            // NsheadMeta

namespace brpc {

class NsheadPbServiceAdaptor;
extern const size_t SendNsheadPbResponseSize;

// Adapt nshead requests to use protobuf-based service.
// What RPC does:
//  * Call ParseNsheadMeta() to understand the nshead header, user must
//    tell RPC which pb method to call in the callback.
//  * Call ParseRequestFromIOBuf() to convert the body after nshead header
//    to pb request, then call the pb method.
//  * When user calls server's done to end the RPC, SerializeResponseToIOBuf()
//    is called to convert pb response to binary data that will be appended
//    after nshead header and sent back to client.

class NsheadPbServiceAdaptor : public NsheadService {
public:
    NsheadPbServiceAdaptor() : NsheadService(
        NsheadServiceOptions(false, SendNsheadPbResponseSize)) {}
    virtual ~NsheadPbServiceAdaptor() {}

    // Fetch meta from `nshead_req' into `meta'.
    // Params:
    //   server: where the RPC runs.
    //   nshead_req: the nshead request that server received.
    //   controller: If something goes wrong, call controller->SetFailed()
    //   meta: Set meta information into this structure. `full_method_name'
    //         must be set if controller is not SetFailed()-ed
    // FIXME: server is not needed anymore, controller->server() is same
    virtual void ParseNsheadMeta(const Server& server,
                                 const NsheadMessage& nshead_req,
                                 Controller* controller,
                                 NsheadMeta* meta) const = 0;

    // Transform `nshead_req' to `pb_req'.
    // Params:
    //   meta: was set by ParseNsheadMeta()
    //   nshead_req: the nshead request that server received.
    //   controller: you can set attachment into the controller. If something
    //               goes wrong, call controller->SetFailed()
    //   pb_req: the pb request should be set by your implementation.
    virtual void ParseRequestFromIOBuf(const NsheadMeta& meta,
                                       const NsheadMessage& nshead_req,
                                       Controller* controller,
                                       google::protobuf::Message* pb_req) const = 0;

    // Transform `pb_res' (and controller) to `nshead_res'.
    // Params:
    //   meta: was set by ParseNsheadMeta()
    //   controller: If something goes wrong, call controller->SetFailed()
    //   pb_res: the pb response that returned by pb method. [NOTE] `pb_res'
    //           can be NULL or uninitialized when RPC failed (indicated by
    //           Controller::Failed()), in which case you may put error
    //           information into `nshead_res'.
    //   nshead_res: the nshead response that will be sent back to client.
    virtual void SerializeResponseToIOBuf(const NsheadMeta& meta,
                                          Controller* controller, 
                                          const google::protobuf::Message* pb_res,
                                          NsheadMessage* nshead_res) const = 0;

private:
    void ProcessNsheadRequest(
        const Server& server, Controller* controller,
        const NsheadMessage& request, NsheadMessage* response,
        NsheadClosure* done);
};

} // namespace brpc


#endif // BRPC_NSHEAD_PB_SERVICE_ADAPTOR_H
