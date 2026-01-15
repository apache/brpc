#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include "test.brpc.fb.h"

DEFINE_bool(echo_attachment, true, "Echo attachment as well");
DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");
DEFINE_int32(internal_port, -1, "Only allow builtin services at this port");

namespace test{
class BenchmarkServiceImpl : public BenchmarkService {
public:
    BenchmarkServiceImpl() {}
    ~BenchmarkServiceImpl() {}

    void Test(google::protobuf::RpcController* controller,
                const brpc::flatbuffers::Message* request_base,
                brpc::flatbuffers::Message* response,
                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(controller);
        const test::BenchmarkRequest* request = request_base->GetRoot<test::BenchmarkRequest>();
        // Set Response Message
        brpc::flatbuffers::MessageBuilder mb_;
        const char *req_str = request->message()->c_str();
        auto message = mb_.CreateString(req_str);
        auto resp = test::CreateBenchmarkResponse(mb_, request->opcode(),
                    request->echo_attachment(), request->attachment_size(),
                    request->request_id(),request->reserved(), message);
        mb_.Finish(resp);
        *response = mb_.ReleaseMessage();
        if (FLAGS_echo_attachment) {
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
};

}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    test::BenchmarkServiceImpl benchmark_service_impl;

    if (server.AddService(&benchmark_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server. 
    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;
    options.internal_port = FLAGS_internal_port;

    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;

}