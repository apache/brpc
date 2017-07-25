// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// A server to receive requests from ubrpc clients.
// This server can be accessed by the client in public/baidu-rpc-ub/example/echo_c++_compack_ubrpc as well.

#include <gflags/gflags.h>
#include <base/logging.h>
#include <brpc/server.h>
#include <brpc/policy/ubrpc2pb_protocol.h>
#include "echo.pb.h"

DEFINE_int32(port, 8500, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(logoff_ms, 2000, "Maximum duration of server's LOGOFF state "
             "(waiting for client to close connection before server stops)");

// Your implementation of EchoService
namespace example {
class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {};
    virtual ~EchoServiceImpl() {};
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        LOG(INFO) << "Received request[log_id=" << cntl->log_id() 
                  << "] from " << cntl->remote_side() 
                  << " to " << cntl->local_side()
                  << ": " << request->DebugString();

        // Fill response.
        response->set_message(request->message());
        // the idl method returns void, no need to set_idl_result().
    }

    virtual void EchoWithMultiArgs(
        google::protobuf::RpcController* cntl_base,
        const MultiRequests* request,
        MultiResponses* response,
        google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        LOG(INFO) << "Received request[log_id=" << cntl->log_id() 
                  << "] from " << cntl->remote_side() 
                  << " to " << cntl->local_side()
                  << ": req1=" << request->req1().message()
                  << " req2=" << request->req2().message();

        // Fill response.
        response->mutable_res1()->set_message(request->req1().message());
        response->mutable_res2()->set_message(request->req2().message());
        // tell RPC that the idl method have more than one request/response.
        cntl->set_idl_names(brpc::idl_multi_req_multi_res);
        // the idl method returns uint32_t, we need to set it.
        cntl->set_idl_result(17);
    }
};
}  // namespace

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

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
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.nshead_service = new brpc::policy::UbrpcCompackAdaptor;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
