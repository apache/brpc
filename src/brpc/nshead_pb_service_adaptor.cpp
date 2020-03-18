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


#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message

#include "butil/time.h" 
#include "butil/iobuf.h"                         // butil::IOBuf

#include "brpc/controller.h"               // Controller
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/span.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/nshead_pb_service_adaptor.h"
#include "brpc/policy/most_common_message.h"


namespace brpc {

struct SendNsheadPbResponse : public google::protobuf::Closure {
    SendNsheadPbResponse(const NsheadPbServiceAdaptor* adaptor,
                         Controller* cntl,
                         NsheadMessage* ns_res,
                         NsheadClosure* done);
    ~SendNsheadPbResponse() {}

    void Run();

    NsheadMeta meta;
    const NsheadPbServiceAdaptor* adaptor;
    Controller* cntl;
    std::unique_ptr<google::protobuf::Message> pbreq;
    std::unique_ptr<google::protobuf::Message> pbres;
    NsheadMessage* ns_res;
    NsheadClosure* done;
    MethodStatus* status;
};

extern const size_t SendNsheadPbResponseSize = sizeof(SendNsheadPbResponse);

SendNsheadPbResponse::SendNsheadPbResponse(
    const NsheadPbServiceAdaptor* adaptor2,
    Controller* cntl2,
    NsheadMessage* ns_res2,
    NsheadClosure* done2)
    : adaptor(adaptor2)
    , cntl(cntl2)
    , ns_res(ns_res2)
    , done(done2)
    , status(NULL) {
}

void SendNsheadPbResponse::Run() {
    MethodStatus* saved_status = status;
    const int64_t received_us = done->received_us();
    if (!cntl->IsCloseConnection()) {
        adaptor->SerializeResponseToIOBuf(meta, cntl, pbres.get(), ns_res);
    }

    const bool saved_failed = cntl->Failed();
    NsheadClosure* saved_done = done;
    // The space is allocated by NsheadClosure, don't delete.
    this->~SendNsheadPbResponse();

    // NOTE: *this was destructed, don't touch anything.
    
    // FIXME(gejun): We can't put this after saved_done->Run() where the
    // service containing saved_status may be destructed (upon server's
    // quiting). Thus the latency does not include the time of sending
    // back response.
    if (saved_status) {
        saved_status->OnResponded(
            !saved_failed, butil::cpuwide_time_us() - received_us);
    }
    saved_done->Run();
}

void NsheadPbServiceAdaptor::ProcessNsheadRequest(
    const Server& server, Controller* controller,
    const NsheadMessage& request, NsheadMessage* response,
    NsheadClosure* done) {
    SendNsheadPbResponse* pbdone = new (done->additional_space())
        SendNsheadPbResponse(this, controller, response, done);
    NsheadMeta* meta = &pbdone->meta;
    do {
        if (controller->Failed()) {
            break;
        }
        ParseNsheadMeta(server, request, controller, meta);
        if (controller->Failed()) {
            break;
        }
        if (meta->has_log_id()) {
            controller->set_log_id(meta->log_id());
        }

        ServerPrivateAccessor server_accessor(&server);
        const Server::MethodProperty *sp = server_accessor
            .FindMethodPropertyByFullName(meta->full_method_name());
        if (NULL == sp ||
            sp->service->GetDescriptor() == BadMethodService::descriptor()) {
            controller->SetFailed(ENOMETHOD, "Fail to find method=%s", 
                                  meta->full_method_name().c_str());
            break;
        }
        pbdone->status = sp->status;
        sp->status->OnRequested();

        google::protobuf::Service* svc = sp->service;
        const google::protobuf::MethodDescriptor* method = sp->method;
        ControllerPrivateAccessor(controller).set_method(method);
        done->SetMethodName(method->full_name());
        pbdone->pbreq.reset(svc->GetRequestPrototype(method).New());
        pbdone->pbres.reset(svc->GetResponsePrototype(method).New());

        ParseRequestFromIOBuf(*meta, request, controller, pbdone->pbreq.get());
        if (controller->Failed()) {
            break;
        }

        // `meta', `req' and `res' will be deleted inside `pbdone'
        return svc->CallMethod(method, controller, pbdone->pbreq.get(),
                               pbdone->pbres.get(), pbdone);
    } while (false);

    pbdone->Run();
} 

} // namespace brpc
