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

// A brpc based test talk with mongodb server

#include <sstream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
// #include <brpc/mongo.h>
#include "brpc/mongo.h"
#include <brpc/policy/mongo_authenticator.h>
#include <bvar/bvar.h>
#include <bthread/bthread.h>
#include <brpc/server.h>

#include <bson/bson.h>

DEFINE_string(connection_type, "pooled", "Connection type. Available values: pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 27017, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(database, "test", "database");
DEFINE_string(collection, "people", "collection");
// DEFINE_string(data, "ABCDEF", "data");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(thread_num, 1, "Number of threads to send requests");
DEFINE_bool(use_bthread, true, "Use bthread to send requests");
DEFINE_int32(dummy_port, -1, "port of dummy server(for monitoring)");
DEFINE_int32(op_type, 1, "CRUD operation, 0:INSERT, 1:SELECT, 2:UPDATE");
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");

struct SenderArgs {
    int base_index;
    brpc::Channel* mongo_channel;
};

// Send `command' to mongo-server via `channel'
static void* sender(void* void_args) {
    SenderArgs* args = (SenderArgs*)void_args;

    google::protobuf::Message *request = nullptr;
    if (FLAGS_op_type == 0) {
        // insert
        // request = brpc::MakeMongoInsertRequest();
        // request.mutable_insert()->set_collection(FLAGS_database);
        // request.mutable_insert()->set_database(FLAGS_collection);

        // bson_t *doc = bson_new();
        // BSON_APPEND_UTF8(doc, "name", "zhangke");
        // size_t length = 0;
        // char *insert_data = bson_as_canonical_extended_json(doc, &length);
        // request.mutable_insert()->add_documents()->set_doc(insert_data, length);
    } else if (FLAGS_op_type == 1) {
        // query
        brpc::MongoQueryRequest *query_request = new brpc::MongoQueryRequest();
        query_request->set_database(FLAGS_database);
        query_request->set_collection(FLAGS_collection);
        // query_request->set_limit(10);
        request = query_request;
    } else if (FLAGS_op_type == 2) {
        // update

    }

    while (!brpc::IsAskedToQuit()) {
        google::protobuf::Message *response = nullptr;
        if (FLAGS_op_type == 0) {
            // response = new brpc::Mongo
        } else if (FLAGS_op_type == 1) {
            response = new brpc::MongoQueryResponse();
        } else if (FLAGS_op_type == 2) {

        }
        brpc::Controller cntl;
        args->mongo_channel->CallMethod(NULL, &cntl, request, response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            g_latency_recorder << elp;
            // CHECK_EQ(response.reply_size(), FLAGS_batch);
            // for (int i = 0; i < FLAGS_batch; ++i) {
            //     CHECK_EQ(kvs[i].second.c_str(), response.reply(i).data())
            //         << "base=" << args->base_index << " i=" << i;
            // }
            brpc::MongoQueryResponse *query_response = dynamic_cast<brpc::MongoQueryResponse*>(response);
            assert(query_response);
            LOG(INFO) << "query return num:" << query_response->number_returned();
            LOG_IF(INFO, query_response->has_cursorid()) << "cursorid:" << query_response->cursorid();
        } else {
            g_error_count << 1;
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << cntl.ErrorText() << " latency=" << elp;
            // We can't connect to the server, sleep a while. Notice that this
            // is a specific sleeping to prevent this thread from spinning too
            // fast. You should continue the business logic in a production 
            // server rather than sleeping.
        }
        bthread_usleep(2 * 1000 * 1000);
        // bthread_usleep(50000);
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
    options.protocol = brpc::PROTOCOL_MONGO;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
    options.connect_timeout_ms = FLAGS_connect_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    LOG(INFO) << "passwd:" << FLAGS_password;
    // options.auth = new brpc::policy::MysqlAuthenticator(
    //     FLAGS_user, FLAGS_password, FLAGS_schema, FLAGS_params, FLAGS_collation);
    if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    // test CRUD operations
    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    bids.resize(FLAGS_thread_num);
    pids.resize(FLAGS_thread_num);
    std::vector<SenderArgs> args;
    args.resize(FLAGS_thread_num);
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        args[i].base_index = i;
        args[i].mongo_channel = &channel;
        if (!FLAGS_use_bthread) {
            if (pthread_create(&pids[i], NULL, sender, &args[i]) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        } else {
            if (bthread_start_background(&bids[i], NULL, sender, &args[i]) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }

    while (!brpc::IsAskedToQuit()) {
        sleep(1);

        LOG(INFO) << "Accessing mysql-server at qps=" << g_latency_recorder.qps(1)
                  << " latency=" << g_latency_recorder.latency(1);
    }

    LOG(INFO) << "mysql_client is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(pids[i], NULL);
        } else {
            bthread_join(bids[i], NULL);
        }
    }

    return 0;
}
