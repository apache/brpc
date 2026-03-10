#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/channel.h>
#include <bvar/bvar.h>

#include "test.brpc.fb.h"


DEFINE_int32(thread_num, 1, "Number of threads to send requests");
DEFINE_int32(attachment_size, 0, "Carry so many byte attachment along with requests");
DEFINE_int32(request_size, 16, "Bytes of each request");
DEFINE_string(servers, "0.0.0.0:8002", "IP Address of server");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(dummy_port, -1, "Launch dummy server at this port");

std::string g_request;
butil::IOBuf g_attachment;

bvar::LatencyRecorder g_latency_recorder("client");
bvar::LatencyRecorder g_msg_recorder("msg");
bvar::Adder<int> g_error_count("client_error_count");

static void* sender(void* arg) {
    test::BenchmarkServiceStub stub(static_cast<brpc::Channel*>(arg));
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        brpc::Controller cntl;
        brpc::flatbuffers::Message response;

        cntl.set_log_id(log_id++);
        cntl.request_attachment().append(g_attachment);

        uint64_t msg_begin_ns = butil::cpuwide_time_ns();
        brpc::flatbuffers::MessageBuilder mb;
        auto message = mb.CreateString(g_request);
        auto req = test::CreateBenchmarkRequest(mb, 123, 333, 1111, 2222, 0, message);
        mb.Finish(req);
        brpc::flatbuffers::Message request = mb.ReleaseMessage();

        uint64_t msg_end_ns = butil::cpuwide_time_ns();
        stub.Test(&cntl, &request, &response, NULL);

        if (!cntl.Failed()) {
            g_latency_recorder << cntl.latency_us();
            g_msg_recorder << (msg_end_ns - msg_begin_ns);
        } else {
            g_error_count << 1;
            CHECK(brpc::IsAskedToQuit())
                << "error=" << cntl.ErrorText() << " latency=" << cntl.latency_us();
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
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    // Print parameter information in one line
    LOG(INFO) << "Parameters - request_size : " << FLAGS_request_size
              << ", attachment_size: " << FLAGS_attachment_size
              << ", thread_num: " << FLAGS_thread_num;

    // A Channel represents a communication line to a Server. Notice that
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;

    // Initialize the channel, NULL means using default options.
    brpc::ChannelOptions options;
    options.protocol = "fb_rpc";
    options.connection_type = "";
    options.connect_timeout_ms = std::min(FLAGS_timeout_ms / 2, 100);
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_servers.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }
    if (FLAGS_attachment_size > 0) {
        void* _attachment_addr = malloc(FLAGS_attachment_size);
        if (!_attachment_addr) {
            LOG(ERROR) << "Fail to alloc _attachment from system heap";
            return -1;
        }
        g_attachment.append(_attachment_addr, FLAGS_attachment_size);
        free(_attachment_addr);
    }
    if (FLAGS_request_size < 0) {
        LOG(ERROR) << "Bad request_size=" << FLAGS_request_size;
        return -1;
    }
    g_request.resize(FLAGS_request_size, 'r');

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    std::vector<bthread_t> bids;
    bids.resize(FLAGS_thread_num);
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (bthread_start_background(&bids[i], NULL, sender, &channel) != 0) {
            LOG(ERROR) << "Fail to create bthread";
            return -1;
        }
    }

    while (!brpc::IsAskedToQuit()) {
        sleep(1);
        LOG(INFO) << "Sending EchoRequest at qps=" << (g_latency_recorder.qps(1) / 1000)
                  << "k latency=" << g_latency_recorder.latency(1) << "us"
                  << " msg latency=" << g_msg_recorder.latency(1) << "ns";
    }

    LOG(INFO) << "EchoClient is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        bthread_join(bids[i], NULL);
    }

    LOG(INFO) << "Average QPS: " << (g_latency_recorder.qps()/1000) << "k"
            << " Average latency: " << g_latency_recorder.latency() << "us"
            << " msg latency: " << g_msg_recorder.latency() << "ns";

    return 0;
}