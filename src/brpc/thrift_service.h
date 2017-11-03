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

// Authors: wangxuefeng (wangxuefeng@didichuxing.com)

#ifndef BRPC_THRIFT_SERVICE_H
#define BRPC_THRIFT_SERVICE_H

#include "brpc/controller.h"                        // Controller
#include "brpc/thrift_binary_message.h"             // ThriftBinaryMessage
#include "brpc/describable.h"


namespace brpc {

class Socket;
class Server;
class MethodStatus;
class StatusService;
namespace policy {
void ProcessThriftBinaryRequest(InputMessageBase* msg_base);
}

// The continuation of request processing. Namely send response back to client.
// NOTE: you DON'T need to inherit this class or create instance of this class.
class ThriftFramedClosure : public google::protobuf::Closure {
public:
    explicit ThriftFramedClosure(void* additional_space);

    // [Required] Call this to send response back to the client.
    void Run();

    // [Optional] Set the full method name. If unset, use name of the service.
    void SetMethodName(const std::string& full_method_name);
    
    // The space required by subclass at ThriftFramedServiceOptions. subclass may
    // utilizes this feature to save the cost of allocating closure separately.
    // If subclass does not require space, this return value is NULL.
    void* additional_space() { return _additional_space; }

    // The starting time of the RPC, got from butil::cpuwide_time_us().
    int64_t cpuwide_start_us() const { return _start_parse_us; }

    // Don't send response back, used by MIMO.
    void DoNotRespond();

private:
friend void policy::ProcessThriftBinaryRequest(InputMessageBase* msg_base);
friend class DeleteThriftFramedClosure;
    // Only callable by Run().
    ~ThriftFramedClosure();

    Socket* _socket_ptr;
    const Server* _server;
    int64_t _start_parse_us;
    ThriftBinaryMessage _request;
    ThriftBinaryMessage _response;
    bool _do_respond;
    void* _additional_space;
    Controller _controller;
};

struct ThriftFramedServiceOptions {
    ThriftFramedServiceOptions() : generate_status(true), additional_space(0) {}
    ThriftFramedServiceOptions(bool generate_status2, size_t additional_space2)
        : generate_status(generate_status2)
        , additional_space(additional_space2) {}

    bool generate_status;
    size_t additional_space;
};

// Inherit this class to let brpc server understands thrift_binary requests.
class ThriftFramedService : public Describable {
public:
    ThriftFramedService();
    ThriftFramedService(const ThriftFramedServiceOptions&);
    virtual ~ThriftFramedService();

    // Implement this method to handle thrift_binary requests. Notice that this
    // method can be called with a failed Controller(something wrong with the
    // request before calling this method), in which case the implemenetation
    // shall send specific response with error information back to client.
    // Parameters:
    //   server      The server receiving the request.
    //   controller  per-rpc settings.
    //   request     The thrift_binary request received.
    //   response    The thrift_binary response that you should fill in.
    //   done        You must call done->Run() to end the processing.
    virtual void ProcessThriftBinaryRequest(const Server& server,
                                      Controller* controller,
                                      const ThriftBinaryMessage& request,
                                      ThriftBinaryMessage* response,
                                      ThriftFramedClosure* done) = 0;

    // Put descriptions into the stream.
    void Describe(std::ostream &os, const DescribeOptions&) const;

private:
DISALLOW_COPY_AND_ASSIGN(ThriftFramedService);
friend class ThriftFramedClosure;
friend void policy::ProcessThriftBinaryRequest(InputMessageBase* msg_base);
friend class StatusService;
friend class Server;

private:
    void Expose(const butil::StringPiece& prefix);
    
    // Tracking status of non ThriftBinaryPbService
    MethodStatus* _status;
    size_t _additional_space;
    std::string _cached_name;
};

} // namespace brpc


#endif // BRPC_THRIFT_SERVICE_H
