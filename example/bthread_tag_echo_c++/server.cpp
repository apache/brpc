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

#include <brpc/server.h>
#include <bthread/unstable.h>
#include <butil/logging.h>
#include <gflags/gflags.h>
#include "echo.pb.h"

DEFINE_bool(echo_attachment, true, "Echo attachment as well");
DEFINE_int32(port1, 8002, "TCP Port of this server");
DEFINE_int32(port2, 8003, "TCP Port of this server");
DEFINE_int32(tag1, 0, "Server1 tag");
DEFINE_int32(tag2, 1, "Server2 tag");
DEFINE_int32(tag3, 2, "Background task tag");
DEFINE_int32(num_threads1, 6, "Thread number of server1");
DEFINE_int32(num_threads2, 16, "Thread number of server2");
DEFINE_int32(idle_timeout_s, -1,
             "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");
DEFINE_int32(internal_port1, -1, "Only allow builtin services at this port");
DEFINE_int32(internal_port2, -1, "Only allow builtin services at this port");

namespace example {
// Your implementation of EchoService
class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    ~EchoServiceImpl() {}
    void Echo(google::protobuf::RpcController* cntl_base, const EchoRequest* request,
              EchoResponse* response, google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        // Echo request and its attachment
        response->set_message(request->message());
        if (FLAGS_echo_attachment) {
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
};
}  // namespace example

DEFINE_bool(h, false, "print help information");

static void my_tagged_worker_start_fn(bthread_tag_t tag) {
    LOG(INFO) << "run tagged worker start function tag=" << tag;
}

static void* my_background_task(void*) {
    while (true) {
        LOG(INFO) << "run background task tag=" << bthread_self_tag();
        bthread_usleep(1000000UL);
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::string help_str = "dummy help infomation";
    GFLAGS_NAMESPACE::SetUsageMessage(help_str);

    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_h) {
        fprintf(stderr, "%s\n%s\n%s", help_str.c_str(), help_str.c_str(), help_str.c_str());
        return 0;
    }

    // Set tagged worker function
    bthread_set_tagged_worker_startfn(my_tagged_worker_start_fn);

    // Generally you only need one Server.
    brpc::Server server1;

    // Instance of your service.
    example::EchoServiceImpl echo_service_impl1;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server1.AddService(&echo_service_impl1, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server.
    brpc::ServerOptions options1;
    options1.idle_timeout_sec = FLAGS_idle_timeout_s;
    options1.max_concurrency = FLAGS_max_concurrency;
    options1.internal_port = FLAGS_internal_port1;
    options1.bthread_tag = FLAGS_tag1;
    options1.num_threads = FLAGS_num_threads1;
    if (server1.Start(FLAGS_port1, &options1) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Generally you only need one Server.
    brpc::Server server2;

    // Instance of your service.
    example::EchoServiceImpl echo_service_impl2;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server2.AddService(&echo_service_impl2, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server.
    brpc::ServerOptions options2;
    options2.idle_timeout_sec = FLAGS_idle_timeout_s;
    options2.max_concurrency = FLAGS_max_concurrency;
    options2.internal_port = FLAGS_internal_port2;
    options2.bthread_tag = FLAGS_tag2;
    options2.num_threads = FLAGS_num_threads2;
    if (server2.Start(FLAGS_port2, &options2) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Start backgroup task
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.tag = FLAGS_tag3;
    bthread_start_background(&tid, &attr, my_background_task, nullptr);

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server1.RunUntilAskedToQuit();
    server2.RunUntilAskedToQuit();

    return 0;
}
