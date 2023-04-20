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
#include "echo.pb.h"

DEFINE_bool(echo_attachment, true, "Echo attachment as well");
DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_string(listen_addr, "",
              "Server listen address, may be IPV4/IPV6/UDS."
              " If this is set, the flag port will be ignored");
DEFINE_int32(idle_timeout_s, -1,
             "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(logoff_ms, 2000,
             "Maximum duration of server's LOGOFF state "
             "(waiting for client to close connection before server stops)");
DEFINE_int32(num_threads1, 2, "thread number for pool1");
DEFINE_int32(num_threads2, 2, "thread number for pool2");

// Your implementation of example::EchoService
// Notice that implementing brpc::Describable grants the ability to put
// additional information in /status.
namespace example {
butil::atomic<int> ntls(0);
struct MyThreadLocalData {
    MyThreadLocalData() : y(0) {
        ntls.fetch_add(1, butil::memory_order_relaxed);
    }
    ~MyThreadLocalData() { ntls.fetch_sub(1, butil::memory_order_relaxed); }
    static void deleter(void* d) { delete static_cast<MyThreadLocalData*>(d); }

    int y;
};

class MyThreadLocalDataFactory : public brpc::DataFactory {
public:
    void* CreateData() const { return new MyThreadLocalData; }

    void DestroyData(void* d) const { MyThreadLocalData::deleter(d); }
};

class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl(){};
    virtual ~EchoServiceImpl(){};
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request, EchoResponse* response,
                      google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        MyThreadLocalData* tls =
            static_cast<MyThreadLocalData*>(brpc::thread_local_data());
        if (tls == NULL) {
            cntl->SetFailed(
                "Require ServerOptions.thread_local_data_factory "
                "to be set with a correctly implemented instance");
            LOG(ERROR) << cntl->ErrorText();
            return;
        }
        // The purpose of following logs is to help you to understand
        // how clients interact with servers more intuitively. You should
        // remove these logs in performance-sensitive servers.
        LOG(INFO) << "Received request[log_id=" << cntl->log_id() << "] from "
                  << cntl->remote_side() << " to " << cntl->local_side() << ": "
                  << request->message()
                  << " (attached=" << cntl->request_attachment() << ")";

        // Fill response.
        response->set_message(request->message());

        // You can compress the response by setting Controller, but be aware
        // that compression may be costly, evaluate before turning on.
        // cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);

        if (FLAGS_echo_attachment) {
            // Set attachment which is wired to network directly instead of
            // being serialized into protobuf messages.
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
    virtual void Echo2(google::protobuf::RpcController* cntl_base,
                       const EchoRequest* request, EchoResponse* response,
                       google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        MyThreadLocalData* tls =
            static_cast<MyThreadLocalData*>(brpc::thread_local_data());
        if (tls == NULL) {
            cntl->SetFailed(
                "Require ServerOptions.thread_local_data_factory "
                "to be set with a correctly implemented instance");
            LOG(ERROR) << cntl->ErrorText();
            return;
        }
        // The purpose of following logs is to help you to understand
        // how clients interact with servers more intuitively. You should
        // remove these logs in performance-sensitive servers.
        LOG(INFO) << "Received request[log_id=" << cntl->log_id() << "] from "
                  << cntl->remote_side() << " to " << cntl->local_side() << ": "
                  << request->message()
                  << " (attached=" << cntl->request_attachment() << ")";

        // Fill response.
        response->set_message(request->message());

        // You can compress the response by setting Controller, but be aware
        // that compression may be costly, evaluate before turning on.
        // cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);

        if (FLAGS_echo_attachment) {
            // Set attachment which is wired to network directly instead of
            // being serialized into protobuf messages.
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
};

struct MyUserCodeThreadPoolArgs {
    ::google::protobuf::Service* service;
    const ::google::protobuf::MethodDescriptor* method;
    ::google::protobuf::RpcController* controller;
    const ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
    ::google::protobuf::Closure* done;
};
// user defined policy
class MyUserCodeThreadAssignPolicy : public brpc::UserCodeThreadAssignPolicy {
public:
    MyUserCodeThreadAssignPolicy() {}
    virtual ~MyUserCodeThreadAssignPolicy() {}
    size_t Index(void* arg, size_t range) {
        auto myArg = static_cast<MyUserCodeThreadPoolArgs*>(arg);
        auto request = static_cast<const EchoRequest*>(myArg->request);
        auto hash = std::hash<std::string>{}(request->message());
        LOG(INFO) << "MyUserCodeThreadAssignPolicy message="
                  << request->message() << " hash=" << hash
                  << " range=" << range;
        return hash % range;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(MyUserCodeThreadAssignPolicy);
};

}  // namespace example

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    // Instance of your service.
    example::EchoServiceImpl echo_service_impl;

    // Generally you only need one Server.
    brpc::Server server;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    brpc::UserCodeThreadPoolConf conf(
        "p1", FLAGS_num_threads1, nullptr,
        brpc::DefaultUserCodeThreadAssignPolicy());
    if (!server.SetThreadPool(&echo_service_impl, "Echo", conf)) {
        LOG(ERROR) << "Fail to set thread pool";
        return -1;
    }
    // register validate here, avoid pool not exist
    BRPC_VALIDATE_GFLAG(num_threads1, [](const char*, int32_t val) {
        return brpc::UserCodeThreadPool::SetPoolThreads("p1", val);
    });

    example::MyUserCodeThreadAssignPolicy policy2;
    brpc::UserCodeThreadPoolConf conf2("p2", FLAGS_num_threads2, nullptr,
                                       &policy2);
    if (!server.SetThreadPool(&echo_service_impl, "Echo2", conf2)) {
        LOG(ERROR) << "Fail to set thread pool";
        return -1;
    }
    // register validate here, avoid pool not exist
    BRPC_VALIDATE_GFLAG(num_threads2, [](const char*, int32_t val) {
        return brpc::UserCodeThreadPool::SetPoolThreads("p2", val);
    });

    butil::EndPoint point;
    if (!FLAGS_listen_addr.empty()) {
        if (butil::str2endpoint(FLAGS_listen_addr.c_str(), &point) < 0) {
            LOG(ERROR) << "Invalid listen address:" << FLAGS_listen_addr;
            return -1;
        }
    } else {
        point = butil::EndPoint(butil::IP_ANY, FLAGS_port);
    }
    // Start the server.
    // The factory to create MySessionLocalData. Must be valid when server is
    // running.
    example::MyThreadLocalDataFactory thread_local_data_factory;

    // For more options see `brpc/server.h'.
    brpc::ServerOptions options;
    options.thread_local_data_factory = &thread_local_data_factory;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
