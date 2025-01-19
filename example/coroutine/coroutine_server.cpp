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

// A server to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/channel.h>
#include <brpc/coroutine.h>
#include "echo.pb.h"

DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_int32(sleep_us, 1000000, "Server sleep us");
DEFINE_bool(enable_coroutine, true, "Enable coroutine");

using brpc::experimental::Awaitable;
using brpc::experimental::AwaitableDone;
using brpc::experimental::Coroutine;

namespace example {
class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {
        brpc::ChannelOptions options;
        options.timeout_ms = FLAGS_sleep_us / 1000 * 2 + 100;
        options.max_retry = 0;
        CHECK(_channel.Init(butil::EndPoint(butil::IP_ANY, FLAGS_port), &options) == 0);
    }

    virtual ~EchoServiceImpl() {}

    void Echo(google::protobuf::RpcController* cntl_base,
              const EchoRequest* request,
              EchoResponse* response,
              google::protobuf::Closure* done) override {
        // brpc::Controller* cntl =
        //     static_cast<brpc::Controller*>(cntl_base);

        if (FLAGS_enable_coroutine) {
            Coroutine(EchoAsync(request, response, done), true);
        } else {
            brpc::ClosureGuard done_guard(done);
            bthread_usleep(FLAGS_sleep_us);
            response->set_message(request->message());
        }
    }

    Awaitable<void> EchoAsync(const EchoRequest* request,
               EchoResponse* response,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        co_await Coroutine::usleep(FLAGS_sleep_us);
        response->set_message(request->message());
    }

    void Proxy(google::protobuf::RpcController* cntl_base,
               const EchoRequest* request,
               EchoResponse* response,
               google::protobuf::Closure* done) override {
        // brpc::Controller* cntl =
        //     static_cast<brpc::Controller*>(cntl_base);

        if (FLAGS_enable_coroutine) {
            Coroutine(ProxyAsync(request, response, done), true);
        } else {
            brpc::ClosureGuard done_guard(done);
            EchoService_Stub stub(&_channel);
            brpc::Controller cntl;
            stub.Echo(&cntl, request, response, NULL);
            if (cntl.Failed()) {
                response->set_message(cntl.ErrorText());
            }
        }
    }

    Awaitable<void> ProxyAsync(const EchoRequest* request,
                    EchoResponse* response,
                    google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        EchoService_Stub stub(&_channel);
        brpc::Controller cntl;
        AwaitableDone done2;
        stub.Echo(&cntl, request, response, &done2);
        co_await done2.awaitable();
        if (cntl.Failed()) {
            response->set_message(cntl.ErrorText());
        }
    }    

private:
    brpc::Channel _channel;
};
}  // namespace example

int main(int argc, char* argv[]) {
    bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY);

    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_enable_coroutine) {
        GFLAGS_NAMESPACE::SetCommandLineOption("usercode_in_coroutine", "true");
    }

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    example::EchoServiceImpl echo_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server.
    brpc::ServerOptions options;
    options.num_threads = BTHREAD_MIN_CONCURRENCY;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}