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


#ifndef BRPC_BAIDU_MASTER_SERVICE_H
#define BRPC_BAIDU_MASTER_SERVICE_H

#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include "brpc/details/method_status.h"
#include "brpc/proto_base.pb.h"
#include "brpc/controller.h"
#include "brpc/serialized_request.h"
#include "brpc/serialized_response.h"

namespace brpc {

namespace policy {
void ProcessRpcRequest(InputMessageBase* msg_base);
}

class BaiduMasterService : public ::google::protobuf::Service
                         , public Describable {
public:
    BaiduMasterService();
    ~BaiduMasterService() override;

    DISALLOW_COPY_AND_ASSIGN(BaiduMasterService);

    AdaptiveMaxConcurrency& MaxConcurrency() {
        return _max_concurrency;
    }

    bool ignore_eovercrowded() {
        return _ignore_eovercrowded;
    }

    void set_ignore_eovercrowded(bool ignore_eovercrowded) {
        _ignore_eovercrowded = ignore_eovercrowded;
    }

    virtual void ProcessRpcRequest(Controller* cntl,
                                   const SerializedRequest* request,
                                   SerializedResponse* response,
                                   ::google::protobuf::Closure* done) = 0;


    // Put descriptions into the stream.
    void Describe(std::ostream &os, const DescribeOptions&) const override;

    // implements Service ----------------------------------------------

    void CallMethod(const ::google::protobuf::MethodDescriptor*,
                    ::google::protobuf::RpcController* controller,
                    const ::google::protobuf::Message* request,
                    ::google::protobuf::Message* response,
                    ::google::protobuf::Closure* done) override {
        ProcessRpcRequest((Controller*)controller,
                          (const SerializedRequest*)request,
                          (SerializedResponse*)response, done);
    }

    const ::google::protobuf::ServiceDescriptor* GetDescriptor() override {
        return BaiduMasterServiceBase::descriptor();
    }

    const ::google::protobuf::Message& GetRequestPrototype(
        const ::google::protobuf::MethodDescriptor*) const override {
        return _default_request;
    }

    const ::google::protobuf::Message& GetResponsePrototype(
        const ::google::protobuf::MethodDescriptor*) const override {
        return _default_response;
    }

private:
friend void policy::ProcessRpcRequest(InputMessageBase* msg_base);
friend class StatusService;
friend class Server;

    void Expose(const butil::StringPiece& prefix);

    SerializedRequest _default_request;
    SerializedResponse _default_response;

    MethodStatus* _status;
    AdaptiveMaxConcurrency _max_concurrency;
    bool _ignore_eovercrowded;
};

}

#endif //BRPC_BAIDU_MASTER_SERVICE_H
