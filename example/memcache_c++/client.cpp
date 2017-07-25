// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// A multi-threaded client getting keys from a memcache server constantly.

#include <deque>
#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <base/logging.h>
#include <base/string_printf.h>
#include <base/time.h>
#include <base/macros.h>
#include <brpc/channel.h>
#include <brpc/memcache.h>

DEFINE_int32(thread_num, 10, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:11211", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(exptime, 5, "The to-be-got data will be expired after so many seconds");
DEFINE_string(key, "hello", "The key to be get");
DEFINE_string(value, "world", "The value associated with the key");
DEFINE_int32(batch, 1, "Pipelined Operations");

// For latency stats.
static pthread_mutex_t g_latency_mutex = PTHREAD_MUTEX_INITIALIZER;
struct BAIDU_CACHELINE_ALIGNMENT SenderInfo {
    size_t nsuccess;
    int64_t latency_sum;
};
static std::deque<SenderInfo> g_sender_info;

static void* sender(void* arg) {
    // For latency stats.
    SenderInfo* info = NULL;
    int base_index = 0;
    {
        BAIDU_SCOPED_LOCK(g_latency_mutex);
        base_index = (int)g_sender_info.size() * FLAGS_batch;
        g_sender_info.push_back(SenderInfo());
        info = &g_sender_info.back();
    }

    google::protobuf::RpcChannel* channel = 
        static_cast<google::protobuf::RpcChannel*>(arg);

    std::string value;
    std::vector<std::pair<std::string, std::string> > kvs;
    kvs.resize(FLAGS_batch);
    for (int i = 0; i < FLAGS_batch; ++i) {
        kvs[i].first = base::string_printf("%s%d", FLAGS_key.c_str(), base_index + i);
        kvs[i].second = base::string_printf("%s%d", FLAGS_value.c_str(), base_index + i);
    }
    brpc::MemcacheRequest request;
    for (int i = 0; i < FLAGS_batch; ++i) {
        CHECK(request.Get(kvs[i].first));
    }
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        brpc::MemcacheResponse response;
        brpc::Controller cntl;

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        channel->CallMethod(NULL, &cntl, &request, &response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            info->latency_sum += elp;
            ++info->nsuccess;
            for (int i = 0; i < FLAGS_batch; ++i) {
                uint32_t flags;
                if (!response.PopGet(&value, &flags, NULL)) {
                    LOG(INFO) << "Fail to GET the key, " << response.LastError();
                    brpc::AskToQuit();
                    return NULL;
                }
                CHECK(flags == 0xdeadbeef + base_index + i)
                    << "flags=" << flags;
                CHECK(kvs[i].second == value)
                    << "base=" << base_index << " i=" << i << " value=" << value;
            }
        } else {
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << cntl.ErrorText() << " latency=" << elp;
            CHECK(elp < 5000) << "actually " << elp;
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
    options.protocol = brpc::PROTOCOL_MEMCACHE;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Pipeline #batch * #thread_num SET requests into memcache so that we
    // have keys to get.
    brpc::MemcacheRequest request;
    brpc::MemcacheResponse response;
    brpc::Controller cntl;
    for (int i = 0; i < FLAGS_batch * FLAGS_thread_num; ++i) {
        if (!request.Set(base::string_printf("%s%d", FLAGS_key.c_str(), i),
                         base::string_printf("%s%d", FLAGS_value.c_str(), i),
                         0xdeadbeef + i, FLAGS_exptime, 0)) {
            LOG(ERROR) << "Fail to SET " << i << "th request";
            return -1;
        }
    }
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access memcache, " << cntl.ErrorText();
        return -1;
    }
    for (int i = 0; i < FLAGS_batch * FLAGS_thread_num; ++i) {
        if (!response.PopSet(NULL)) {
            LOG(ERROR) << "Fail to SET memcache, i=" << i
                       << ", " << response.LastError();
            return -1;
        }
    }
    LOG(INFO) << "Set " << FLAGS_batch * FLAGS_thread_num
               << " values, expiring after " << FLAGS_exptime << " seconds";
    
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

        // Print qps & latency.
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
        LOG(INFO) << "Accessing memcache server at qps=" << nsuccess - last_counter
                  << " latency=" << avg_latency;
        last_counter = nsuccess;
        last_latency_sum = latency_sum;
    }

    LOG(INFO) << "memcache_client is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(tids[i], NULL);
        } else {
            bthread_join(tids[i], NULL);
        }
    }

    return 0;
}
