// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A multi-threaded client getting keys from a redis-server constantly.

#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <bvar/bvar.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include <brpc/redis.h>

DEFINE_int32(thread_num, 50, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:6379", "IP Address of server");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_string(key, "hello", "The key to be get");
DEFINE_string(value, "world", "The value associated with the key");
DEFINE_int32(batch, 1, "Pipelined Operations");
DEFINE_int32(dummy_port, -1, "port of dummy server(for monitoring)");
DEFINE_int32(backup_request_ms, -1, "Timeout for sending a backup request");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");

struct SenderArgs {
    int base_index;
    brpc::Channel* redis_channel;
};

static void* sender(void* void_args) {
    SenderArgs* args = (SenderArgs*)void_args;

    std::string value;
    std::vector<std::pair<std::string, std::string> > kvs;
    kvs.resize(FLAGS_batch);
    for (int i = 0; i < FLAGS_batch; ++i) {
        kvs[i].first = butil::string_printf(
            "%s_%04d", FLAGS_key.c_str(), args->base_index + i);
        kvs[i].second = butil::string_printf(
            "%s_%04d", FLAGS_value.c_str(), args->base_index + i);
    }

    brpc::RedisRequest request;
    for (int i = 0; i < FLAGS_batch; ++i) {
        CHECK(request.AddCommand("GET %s", kvs[i].first.c_str()));
    }
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        brpc::RedisResponse response;
        brpc::Controller cntl;

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        args->redis_channel->CallMethod(NULL, &cntl, &request, &response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            g_latency_recorder << elp;
            CHECK_EQ(response.reply_size(), FLAGS_batch);
            for (int i = 0; i < FLAGS_batch; ++i) {
                CHECK_EQ(kvs[i].second.c_str(), response.reply(i).data())
                    << "base=" << args->base_index << " i=" << i;
            }
        } else {
            g_error_count << 1;
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << cntl.ErrorText() << " latency=" << elp;
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
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    options.backup_request_ms = FLAGS_backup_request_ms;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Pipeline #batch * #thread_num SET requests into redis so that we
    // have keys to get.
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;
    for (int i = 0; i < FLAGS_batch * FLAGS_thread_num; ++i) {
        if (!request.AddCommand("SET %s_%04d %s_%04d", 
                    FLAGS_key.c_str(), i,
                    FLAGS_value.c_str(), i)) {
            LOG(ERROR) << "Fail to SET " << i << "th request";
            return -1;
        }
    }
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access redis, " << cntl.ErrorText();
        return -1;
    }
    if (FLAGS_batch * FLAGS_thread_num != response.reply_size()) {
        LOG(ERROR) << "Fail to set";
        return -1;
    }
    for (int i = 0; i < FLAGS_batch * FLAGS_thread_num; ++i) {
        CHECK_EQ("OK", response.reply(i).data());
    }
    LOG(INFO) << "Set " << FLAGS_batch * FLAGS_thread_num << " values";

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    bids.resize(FLAGS_thread_num);
    pids.resize(FLAGS_thread_num);
    std::vector<SenderArgs> args;
    args.resize(FLAGS_thread_num);
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        args[i].base_index = i * FLAGS_batch;
        args[i].redis_channel = &channel;
        if (!FLAGS_use_bthread) {
            if (pthread_create(&pids[i], NULL, sender, &args[i]) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        } else {
            if (bthread_start_background(
                    &bids[i], NULL, sender, &args[i]) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }

    while (!brpc::IsAskedToQuit()) {
        sleep(1);
        
        LOG(INFO) << "Accessing redis-server at qps=" << g_latency_recorder.qps(1)
                  << " latency=" << g_latency_recorder.latency(1);
    }

    LOG(INFO) << "redis_client is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(pids[i], NULL);
        } else {
            bthread_join(bids[i], NULL);
        }
    }
    return 0;
}
