#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include "cal_xor.pb.h"

DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");

namespace XorJob {
class CalXorImpl : public CalXor {
public:
    CalXorImpl() {};
    virtual ~CalXorImpl() {};
    virtual void CalculateXor(google::protobuf::RpcController* cntl_base,
                              const CalXorRequest* request,
                              CalXorResponse* response,
                              google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        int64_t num1 = request->num1();
        int64_t num2 = request->num2();
        int64_t num3 = num1^num2;

        response->set_num(num3);
    }
};
}  // namespace XorJob

int main(int argc, char* argv[]) {
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;

    XorJob::CalXorImpl cal_xor_impl;

    if (server.AddService(&cal_xor_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    butil::EndPoint point;
    point = butil::EndPoint(butil::IP_ANY, FLAGS_port);

    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    server.RunUntilAskedToQuit();
    return 0;
}
