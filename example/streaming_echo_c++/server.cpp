// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// A server to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>
#include <base/logging.h>
#include <brpc/server.h>
#include "echo.pb.h"
#include <brpc/stream.h>

DEFINE_bool(send_attachment, true, "Carry attachment along with response");
DEFINE_int32(port, 8001, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(logoff_ms, 2000, "Maximum duration of server's LOGOFF state "
             "(waiting for client to close connection before server stops)");

class StreamReceiver : public brpc::StreamInputHandler {
public:
    virtual int on_received_messages(brpc::StreamId id, 
                                     base::IOBuf *const messages[], 
                                     size_t size) {
        LOG(INFO) << "Received from Stream=" << id << ": " << noflush;
        for (size_t i = 0; i < size; ++i) {
            LOG(INFO) << "msg[" << i << "]=" << *messages[i] << noflush;
        }
        LOG(INFO);
        return 0;
    }
    virtual void on_idle_timeout(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " has no data transmission for a while";
    }
    virtual void on_closed(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " is closed";
    }

};

// Your implementation of example::EchoService
class StreamingEchoService : public example::EchoService {
public:
    StreamingEchoService() : _sd(brpc::INVALID_STREAM_ID) {};
    virtual ~StreamingEchoService() {
        brpc::StreamClose(_sd);
    };
    virtual void Echo(google::protobuf::RpcController* controller,
                      const example::EchoRequest* /*request*/,
                      example::EchoResponse* response,
                      google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(controller);
        brpc::StreamOptions stream_options;
        stream_options.handler = &_receiver;
        if (brpc::StreamAccept(&_sd, *cntl, &stream_options) != 0) {
            cntl->SetFailed("Fail to accept stream");
            return;
        }
        response->set_message("Accepted stream");
    }

private:
    StreamReceiver _receiver;
    brpc::StreamId _sd;
};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    StreamingEchoService echo_service_impl;

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
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
