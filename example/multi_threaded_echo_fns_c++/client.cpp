// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// A client sending requests to servers(discovered by naming service) by multiple threads.

#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <base/logging.h>
#include <base/string_printf.h>
#include <base/time.h>
#include <base/macros.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include <deque>
#include "echo.pb.h"

DEFINE_int32(thread_num, 50, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_bool(send_attachment, false, "Carry attachment along with requests");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in protocol/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "file://server_list", "Addresses of servers");
DEFINE_string(load_balancer, "rr", "Name of load balancer");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(backup_timeout_ms, -1, "backup timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(dummy_port, -1, "Port of dummy server");

pthread_mutex_t g_latency_mutex = PTHREAD_MUTEX_INITIALIZER;
struct BAIDU_CACHELINE_ALIGNMENT SenderInfo {
    size_t nsuccess;
    int64_t latency_sum;
};
std::deque<SenderInfo> g_sender_info;

static void* sender(void* arg) {
    // Normally, you should not call a Channel directly, but instead construct
    // a stub Service wrapping it. stub can be shared by all threads as well.
    example::EchoService_Stub stub(static_cast<google::protobuf::RpcChannel*>(arg));

    SenderInfo* info = NULL;
    int thread_index = 0;
    {
        BAIDU_SCOPED_LOCK(g_latency_mutex);
        g_sender_info.push_back(SenderInfo());
        info = &g_sender_info.back();
        thread_index = (int)g_sender_info.size();
    }

    int log_id = 0;
    brpc::Controller cntl;
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        example::EchoRequest request;
        example::EchoResponse response;
        cntl.Reset();

        const int input = ((thread_index & 0xFFF) << 20) | (log_id & 0xFFFFF);
        request.set_value(input);
        cntl.set_log_id(log_id ++);  // set by user

        if (FLAGS_send_attachment) {
            // Set attachment which is wired to network directly instead of 
            // being serialized into protobuf messages.
            cntl.request_attachment().append("foo");
        }

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        stub.Echo(&cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            CHECK(response.value() == request.value() + 1);
            info->latency_sum += cntl.latency_us();
            ++info->nsuccess;
        } else {
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)        
                << "input=(" << thread_index << "," << (input & 0xFFFFF)        
                << ") error=" << cntl.ErrorText() << " latency=" << cntl.latency_us();
            // We can't connect to the server, sleep a while. Notice that this
            // is a specific sleeping to prevent this thread from spinning too
            // fast. You should continue the business logic in a production 
            // server rather than sleeping.
            bthread_usleep(50000);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.backup_request_ms = FLAGS_backup_timeout_ms;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    if (FLAGS_dummy_port > 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    std::vector<bthread_t> tids;
    tids.resize(FLAGS_thread_num);
    if (!FLAGS_use_bthread) {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (pthread_create(&tids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        }
    } else {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (bthread_start_background(
                    &tids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }

    int64_t last_counter = 0;
    int64_t last_latency_sum = 0;
    std::vector<size_t> last_nsuccess(FLAGS_thread_num);
    while (!brpc::IsAskedToQuit()) {
        sleep(1);
        int64_t latency_sum = 0;
        int64_t nsuccess = 0;
        pthread_mutex_lock(&g_latency_mutex);
        CHECK_EQ(g_sender_info.size(), (size_t)FLAGS_thread_num);
        for (size_t i = 0; i < g_sender_info.size(); ++i) {
            const SenderInfo& info = g_sender_info[i];
            latency_sum += info.latency_sum;
            nsuccess += info.nsuccess;
            if (FLAGS_dont_fail) {
                CHECK(info.nsuccess > last_nsuccess[i]);
            }
            last_nsuccess[i] = info.nsuccess;
        }
        pthread_mutex_unlock(&g_latency_mutex);

        const int64_t avg_latency = (latency_sum - last_latency_sum) /
            std::max(nsuccess - last_counter, 1L);
        LOG(INFO) << "Sending EchoRequest at qps=" << nsuccess - last_counter
                  << " latency=" << avg_latency;
        last_counter = nsuccess;
        last_latency_sum = latency_sum;
    }

    LOG(INFO) << "EchoClient is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(tids[i], NULL);
        } else {
            bthread_join(tids[i], NULL);
        }
    }

    return 0;
}
