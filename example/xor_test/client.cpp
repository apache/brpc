#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>

#include "cal_xor.pb.h"

#include "press.h"

DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 

const int pthread_num = 1;

struct Unit {
    Unit(): xor_job(nullptr), press_test(nullptr) {}
    Unit(XorJob::CalXor_Stub* xor_job_, PressTest* press_test_): 
         xor_job(xor_job_),
         press_test(press_test_) {}
    XorJob::CalXor_Stub* xor_job;
    PressTest* press_test;
};

static void* worker(void* arg) {
    Unit* unit = static_cast<Unit*>(arg);
    XorJob::CalXor_Stub* xor_job = unit->xor_job;
    PressTest* press_test = unit->press_test;

    XorJob::CalXorRequest request;
    XorJob::CalXorResponse response;

    while (!brpc::IsAskedToQuit()) {
        brpc::Controller cntl;
        press_test->SetCurrentTime();
        request.set_num1(123);
        request.set_num2(123);
        xor_job->CalculateXor(&cntl, &request, &response, NULL);

        if (press_test->GetLantency() == -1) {
            break;
        }
        std::cout << "CalXor received: " << response.num() << std::endl;
    }

    return nullptr;
}

int main(int argc, char* argv[]) {
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    
    brpc::Channel channel;
    
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    XorJob::CalXor_Stub *xor_job[pthread_num];
    for (int i = 0; i < pthread_num; i++) {
        xor_job[i] = new XorJob::CalXor_Stub(&channel);
    }

    PressTest press_test[pthread_num];
    Unit* unit[pthread_num];
    std::vector<pthread_t> pts(pthread_num);
    for (int i = 0; i < pthread_num; ++i) {
        unit[i] = new Unit(xor_job[i], &press_test[i]);
        if (pthread_create(&pts[i], NULL, worker, unit[i]) != 0) {
            LOG(ERROR) << "Fail to create pthread";
            return -1;
        }
    }

    for (int i = 0; i < pthread_num; i++) {
      pthread_join(pts[i], NULL);
    }

    printf("start to print press result\n");
    PressResult press_result;
    for (int i = 0; i < pthread_num; i++) {
        press_result.CollectResult(&press_test[i]);
    }
    press_result.PrintResult();

    return 0;
}
