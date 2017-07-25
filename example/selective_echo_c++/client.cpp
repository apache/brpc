// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// A client sending requests to server in parallel by multiple threads.

#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <base/logging.h>
#include <base/string_printf.h>
#include <base/time.h>
#include <base/macros.h>
#include <brpc/selective_channel.h>
#include <brpc/parallel_channel.h>
#include <deque>
#include "echo.pb.h"

DEFINE_int32(thread_num, 50, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_int32(attachment_size, 0, "Carry so many byte attachment along with requests");
DEFINE_int32(request_size, 16, "Bytes of each request");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in protocol/brpc/options.proto");
DEFINE_string(starting_server, "0.0.0.0:8014", "IP Address of the first server, port of i-th server is `first-port + i'");
DEFINE_string(load_balancer, "rr", "Name of load balancer");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(backup_ms, -1, "backup timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");

std::string g_request;
std::string g_attachment;
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
    {
        BAIDU_SCOPED_LOCK(g_latency_mutex);
        g_sender_info.push_back(SenderInfo());
        info = &g_sender_info.back();
    }

    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        example::EchoRequest request;
        example::EchoResponse response;
        brpc::Controller cntl;

        request.set_message(g_request);
        cntl.set_log_id(log_id++);  // set by user

        if (!g_attachment.empty()) {
            // Set attachment which is wired to network directly instead of 
            // being serialized into protobuf messages.
            cntl.request_attachment().append(g_attachment);
        }

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        stub.Echo(&cntl, &request, &response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            info->latency_sum += elp;
            ++info->nsuccess;
        } else {
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << cntl.ErrorText() << " latency=" << elp;
            CHECK_LT(elp, 5000);
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
    brpc::SelectiveChannel channel;
    brpc::ChannelOptions schan_options;
    schan_options.timeout_ms = FLAGS_timeout_ms;
    schan_options.backup_request_ms = FLAGS_backup_ms;
    schan_options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_load_balancer.c_str(), &schan_options) != 0) {
        LOG(ERROR) << "Fail to init SelectiveChannel";
        return -1;
    }

    LOG(INFO) << "Topology:\n"
        << "SelectiveChannel[\n"
        << "  Channel[list://0.0.0.0:8004,0.0.0.0:8005,0.0.0.0:8006]\n"      
        << "  ParallelChannel[\n"
        << "    Channel[0.0.0.0:8007]\n"
        << "    Channel[0.0.0.0:8008]\n"
        << "    Channel[0.0.0.0:8009]]\n"
        << "  SelectiveChannel[\n"
        << "    Channel[list://0.0.0.0:8010,0.0.0.0:8011,0.0.0.0:8012]\n"
        << "    Channel[0.0.0.0:8013]\n"
        << "    Channel[0.0.0.0:8014]]]\n";

    // Add sub channels.
    // ================
    std::vector<brpc::ChannelBase*> sub_channels;
    
    // Add an ordinary channel.
    brpc::Channel* sub_channel1 = new brpc::Channel;
    base::EndPoint pt;
    if (str2endpoint(FLAGS_starting_server.c_str(), &pt) != 0 &&
        hostname2endpoint(FLAGS_starting_server.c_str(), &pt) != 0) {
        LOG(ERROR) << "Invalid address=`" << FLAGS_starting_server << "'";
        return -1;
    }
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    std::ostringstream os;
    os << "list://";
    for (int i = 0; i < 3; ++i) {
        os << base::EndPoint(pt.ip, pt.port++) << ",";
    }
    if (sub_channel1->Init(os.str().c_str(), FLAGS_load_balancer.c_str(),
                           &options) != 0) {
        LOG(ERROR) << "Fail to init ordinary channel";
        return -1;
    }
    sub_channels.push_back(sub_channel1);

    // Add a parallel channel.
    brpc::ParallelChannel* sub_channel2 = new brpc::ParallelChannel;
    brpc::ParallelChannelOptions pchan_options;
    pchan_options.fail_limit = 1;
    if (sub_channel2->Init(&pchan_options) != 0) {
        LOG(ERROR) << "Fail to init sub_channel2";
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        brpc::ChannelOptions options;
        options.protocol = FLAGS_protocol;
        options.connection_type = FLAGS_connection_type;
        brpc::Channel* c = new brpc::Channel;
        if (c->Init(base::EndPoint(pt.ip, pt.port++), &options) != 0) {
            LOG(ERROR) << "Fail to init sub channel[" << i << "] of pchan";
            return -1;
        }
        if (sub_channel2->AddChannel(c, brpc::OWNS_CHANNEL, NULL, NULL) != 0) {
            LOG(ERROR) << "Fail to add sub channel[" << i << "] into pchan";
            return -1;
        }
    }
    sub_channels.push_back(sub_channel2);

    // Add another selective channel with default options.
    brpc::SelectiveChannel* sub_channel3 = new brpc::SelectiveChannel;
    if (sub_channel3->Init(FLAGS_load_balancer.c_str(), NULL) != 0) {
        LOG(ERROR) << "Fail to init schan";
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        brpc::Channel* c = new brpc::Channel;
        if (i == 0) {
            os.str("");
            os << "list://";
            for (int j = 0; j < 3; ++j) {
                os << base::EndPoint(pt.ip, pt.port++) << ",";
            }
            if (c->Init(os.str().c_str(), FLAGS_load_balancer.c_str(),
                        &options) != 0) {
                LOG(ERROR) << "Fail to init sub channel[" << i << "] of schan";
                return -1;
            }
        } else {
            if (c->Init(base::EndPoint(pt.ip, pt.port++), &options) != 0) {
                LOG(ERROR) << "Fail to init sub channel[" << i << "] of schan";
                return -1;
            }
        }
        if (sub_channel3->AddChannel(c, NULL)) {
            LOG(ERROR) << "Fail to add sub channel[" << i << "] into schan";
            return -1;
        }
    }
    sub_channels.push_back(sub_channel3);

    // Add all sub channels into schan.
    for (size_t i = 0; i < sub_channels.size(); ++i) {
        // note: we don't need the handle for channel removal;
        if (channel.AddChannel(sub_channels[i], NULL/*note*/) != 0) {
            LOG(ERROR) << "Fail to add sub_channel[" << i << "]";
            return -1;
        }
    }
    if (FLAGS_attachment_size > 0) {
        g_attachment.resize(FLAGS_attachment_size, 'a');
    }
    if (FLAGS_request_size <= 0) {
        LOG(ERROR) << "Bad request_size=" << FLAGS_request_size;
        return -1;
    }
    g_request.resize(FLAGS_request_size, 'r');

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
                CHECK(info.nsuccess > last_nsuccess[i]) << "i=" << i;
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
