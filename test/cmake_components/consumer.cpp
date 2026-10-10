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

#if defined(TEST_butil)
#if defined(ENABLE_THRIFT_FRAMED_PROTOCOL) || defined(BRPC_WITH_RDMA) || \
    defined(BRPC_WITH_URMA) || defined(BRPC_WITH_UBRING) || \
    defined(BRPC_BTHREAD_TRACER) || defined(BRPC_DEBUG_LOCK) || \
    defined(BRPC_DEBUG_BTHREAD_SCHE_SAFETY) || defined(BTHREAD_USE_FAST_PTHREAD_MUTEX)
#error "butil must not inherit RPC or bthread feature definitions"
#endif
#include "butil/iobuf.h"
#elif defined(TEST_bvar)
#include "bvar/latency_recorder.h"
#elif defined(TEST_bthread)
#ifndef BTHREAD_USE_FAST_PTHREAD_MUTEX
#error "bthread must expose its public mutex configuration"
#endif
#include "bthread/bthread.h"
#include "bthread/mutex.h"
static void* run(void* arg) {
    ++*static_cast<int*>(arg);
    return nullptr;
}
#elif defined(TEST_json2pb)
#include <google/protobuf/descriptor.pb.h>
#include "json2pb/json_to_pb.h"
#include "json2pb/pb_to_json.h"
#elif defined(TEST_mcpack2pb)
#include <string>
#include "idl_options.pb.h"
#include "mcpack.pb.h"
#include "butil/iobuf.h"
#include "mcpack2pb/field_type.h"
#include "mcpack2pb/serializer.h"
#elif defined(TEST_brpc)
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "bthread/bthread.h"
#include "bvar/reducer.h"
#include "echo.pb.h"

class EchoService : public brpc_component_test::EchoService {
public:
    void Echo(google::protobuf::RpcController*,
              const brpc_component_test::EchoRequest* request,
              brpc_component_test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        calls << 1;
        response->set_text(request->text());
    }
    bvar::Adder<int> calls;
};
#endif

int main() {
#if defined(TEST_butil)
    butil::IOBuf buf;
    return buf.append("hello") != 0 || buf.to_string() != "hello";
#elif defined(TEST_bvar)
    bvar::LatencyRecorder recorder;
    recorder << 1 << 2 << 3;
    return recorder.count() != 3;
#elif defined(TEST_bthread)
    bthread::FastPthreadMutex mutex;
    mutex.lock();
    mutex.unlock();
    int count = 0;
    bthread_t tid;
    if (bthread_start_background(&tid, nullptr, run, &count) != 0) {
        return 1;
    }
    return bthread_join(tid, nullptr) != 0 || count != 1;
#elif defined(TEST_json2pb)
    google::protobuf::FileDescriptorProto message;
    message.set_name("component.proto");
    google::protobuf::FileDescriptorProto parsed;
    std::string json;
    return !json2pb::ProtoMessageToJson(message, &json) ||
           !json2pb::JsonToProtoMessage(json, &parsed) ||
           parsed.name() != message.name();
#elif defined(TEST_mcpack2pb)
    brpc_component_test::McpackMessage message;
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    if (!descriptor->file()->options().GetExtension(idl_support) ||
        descriptor->FindFieldByName("text")->options().GetExtension(idl_name) !=
            "payload") {
        return 1;
    }
    butil::IOBuf buf;
    butil::IOBufAsZeroCopyOutputStream stream(&buf);
    mcpack2pb::OutputStream output(&stream);
    mcpack2pb::Serializer serializer(&output);
    serializer.begin_object();
    serializer.add_int32("value", 42);
    serializer.end_object();
    output.done();
    return !serializer.good() || buf.empty() ||
           std::string(mcpack2pb::type2str(mcpack2pb::FIELD_OBJECT)) != "object";
#elif defined(TEST_brpc)
    EchoService service;
    brpc::Server server;
    if (server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0 ||
        server.Start(0, nullptr) != 0) {
        return 1;
    }
    int result = 1;
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 3000;
    options.max_retry = 0;
    if (channel.Init(server.listen_address(), &options) == 0) {
        brpc_component_test::EchoService_Stub stub(&channel);
        brpc_component_test::EchoRequest request;
        brpc_component_test::EchoResponse response;
        brpc::Controller controller;
        request.set_text("component RPC");
        stub.Echo(&controller, &request, &response, nullptr);
        result = controller.Failed() || response.text() != request.text() ||
                 service.calls.get_value() != 1;
    }
    server.Stop(0);
    server.Join();
    return result;
#endif
}
