// Copyright (c) 2014 Baidu, Inc.
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

// A server to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>
#include "butil/logging.h"
#include "brpc/server.h"
#include "brpc/service.h"
#include "brpc/policy/etcd_service_validate_listener.h"
#include "brpc/cmd_flags.h"
#include "echo/echo.pb.h"


// Your implementation of example::EchoService
// Notice that implementing brpc::Describable grants the ability to put
// additional information in /status.
namespace sogou {
namespace nlu {
namespace rpc {
namespace example {

class EchoServiceImpl : public EchoService, public brpc::BaseService {
public:
    EchoServiceImpl() {};
    virtual ~EchoServiceImpl() {};
    bool checkValid(){
        return true;
    }
    virtual void echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        // The purpose of following logs is to help you to understand
        // how clients interact with servers more intuitively. You should 
        // remove these logs in performance-sensitive servers.
        // You should also noticed that these logs are different from what
        // we wrote in other projects: they use << instead of printf-style
        // functions. But don't worry, these logs are fully compatible with
        // comlog. You can mix them with comlog or ullog functions freely.
        // The noflush prevents the log from being flushed immediately.
        LOG(INFO) << "Received request[log_id=" << cntl->log_id() 
                  << "] from " << cntl->remote_side() 
                  << " to " << cntl->local_side() << noflush;
        LOG(INFO) << ": " << request->message() << noflush;
        LOG(INFO);

        // Fill response.
        response->set_message(request->message());

        // You can compress the response by setting Controller, but be aware
        // that compression may be costly, evaluate before turning on.
        // cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);

    }
};
}  // namespace example
}  // namespace rpc
}  // namespace nlu
}  // namespace sogou

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    sogou::nlu::rpc::example::EchoServiceImpl echo_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Etcd service validate listener
    brpc::policy::EtcdServiceValidateListener etcdServiceValidateListener;

    // Start the server.
    brpc::ServerOptions options;
    options.service_Validate_listener = &etcdServiceValidateListener;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }
    
    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
