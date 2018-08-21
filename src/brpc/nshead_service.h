// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_NSHEAD_SERVICE_H
#define BRPC_NSHEAD_SERVICE_H

#include "brpc/controller.h"                 // Controller
#include "brpc/nshead_message.h"             // NsheadMessage
#include "brpc/describable.h"


namespace brpc {

class Server;
class MethodStatus;
class StatusService;
namespace policy {
void ProcessNsheadRequest(InputMessageBase* msg_base);
}

// The continuation of request processing. Namely send response back to client.
// NOTE: you DON'T need to inherit this class or create instance of this class.
class NsheadClosure : public google::protobuf::Closure {
public:
    explicit NsheadClosure(void* additional_space);

    // [Required] Call this to send response back to the client.
    void Run();

    // [Optional] Set the full method name. If unset, use name of the service.
    void SetMethodName(const std::string& full_method_name);
    
    // The space required by subclass at NsheadServiceOptions. subclass may
    // utilizes this feature to save the cost of allocating closure separately.
    // If subclass does not require space, this return value is NULL.
    void* additional_space() { return _additional_space; }

    int64_t received_us() const { return _received_us; }

    // Don't send response back, used by MIMO.
    void DoNotRespond();

private:
friend void policy::ProcessNsheadRequest(InputMessageBase* msg_base);
friend class DeleteNsheadClosure;
    // Only callable by Run().
    ~NsheadClosure();

    const Server* _server;
    int64_t _received_us;
    NsheadMessage _request;
    NsheadMessage _response;
    bool _do_respond;
    void* _additional_space;
    Controller _controller;
};

struct NsheadServiceOptions {
    NsheadServiceOptions() : generate_status(true), additional_space(0) {}
    NsheadServiceOptions(bool generate_status2, size_t additional_space2)
        : generate_status(generate_status2)
        , additional_space(additional_space2) {}

    bool generate_status;
    size_t additional_space;
};

// Inherit this class to let brpc server understands nshead requests.
class NsheadService : public Describable {
public:
    NsheadService();
    NsheadService(const NsheadServiceOptions&);
    virtual ~NsheadService();

    // Implement this method to handle nshead requests. Notice that this
    // method can be called with a failed Controller(something wrong with the
    // request before calling this method), in which case the implemenetation
    // shall send specific response with error information back to client.
    // Parameters:
    //   server      The server receiving the request.
    //   controller  per-rpc settings.
    //   request     The nshead request received.
    //   response    The nshead response that you should fill in.
    //   done        You must call done->Run() to end the processing.
    virtual void ProcessNsheadRequest(const Server& server,
                                      Controller* controller,
                                      const NsheadMessage& request,
                                      NsheadMessage* response,
                                      NsheadClosure* done) = 0;

    // Put descriptions into the stream.
    void Describe(std::ostream &os, const DescribeOptions&) const;

private:
DISALLOW_COPY_AND_ASSIGN(NsheadService);
friend class NsheadClosure;
friend void policy::ProcessNsheadRequest(InputMessageBase* msg_base);
friend class StatusService;
friend class Server;

private:
    void Expose(const butil::StringPiece& prefix);
    
    // Tracking status of non NsheadPbService
    MethodStatus* _status;
    size_t _additional_space;
    std::string _cached_name;
};

} // namespace brpc


#endif // BRPC_NSHEAD_SERVICE_H
