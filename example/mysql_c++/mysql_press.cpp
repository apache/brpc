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

// A brpc based command-line interface to talk with mysql-server

#include <sstream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/mysql.h>
#include <brpc/policy/mysql_authenticator.h>
#include <bvar/bvar.h>
#include <bthread/bthread.h>
#include <brpc/server.h>

DEFINE_string(connection_type, "pooled", "Connection type. Available values: pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 3306, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(schema, "brpc_test", "schema");
DEFINE_string(params, "", "params");
DEFINE_string(collation, "utf8mb4_general_ci", "collation");
DEFINE_string(data, "ABCDEF", "data");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(thread_num, 50, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_int32(dummy_port, -1, "port of dummy server(for monitoring)");
DEFINE_int32(op_type, 0, "CRUD operation, 0:INSERT, 1:SELECT, 2:UPDATE");
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");

struct SenderArgs {
    int base_index;
    brpc::Channel* mysql_channel;
};

const std::string insert =
    "insert into brpc_press(col1,col2,col3,col4) values "
    "('"
    "ABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCA"
    "BCABCABCABCABCABCABCA', '" +
    FLAGS_data +
    "' ,1.5, "
    "now())";
// Send `command' to mysql-server via `channel'
static void* sender(void* void_args) {
    SenderArgs* args = (SenderArgs*)void_args;
    std::stringstream command;
    if (FLAGS_op_type == 0) {
        command << insert;
    } else if (FLAGS_op_type == 1) {
        command << "select * from brpc_press where id = " << args->base_index + 1;
    } else if (FLAGS_op_type == 2) {
        command << "update brpc_press set col2 = '" + FLAGS_data + "' where id = "
                << args->base_index + 1;
    } else {
        LOG(ERROR) << "wrong op type " << FLAGS_op_type;
    }

    brpc::MysqlRequest request;
    if (!request.Query(command.str())) {
        LOG(ERROR) << "Fail to execute command";
        return NULL;
    }

    while (!brpc::IsAskedToQuit()) {
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        args->mysql_channel->CallMethod(NULL, &cntl, &request, &response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            g_latency_recorder << elp;
            if (FLAGS_op_type == 0) {
                CHECK_EQ(response.reply(0).is_ok(), true);
            } else if (FLAGS_op_type == 1) {
                CHECK_EQ(response.reply(0).row_count(), 1);
            } else if (FLAGS_op_type == 2) {
                CHECK_EQ(response.reply(0).is_ok(), true);
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
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
    options.connect_timeout_ms = FLAGS_connect_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    options.auth = new brpc::policy::MysqlAuthenticator(
        FLAGS_user, FLAGS_password, FLAGS_schema, FLAGS_params, FLAGS_collation);
    if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // create table brpc_press
    {
        brpc::MysqlRequest request;
        if (!request.Query(
                "CREATE TABLE IF NOT EXISTS `brpc_press`(`id` INT UNSIGNED AUTO_INCREMENT, `col1` "
                "VARCHAR(100) NOT NULL, `col2` VARCHAR(1024) NOT NULL, `col3` decimal(10,0) NOT "
                "NULL, `col4` DATE, PRIMARY KEY ( `id` )) ENGINE=InnoDB DEFAULT CHARSET=utf8;")) {
            LOG(ERROR) << "Fail to create table";
            return -1;
        }
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            std::cout << response << std::endl;
        } else {
            LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
            return -1;
        }
    }

    // truncate table
    {
        brpc::MysqlRequest request;
        if (!request.Query("truncate table brpc_press")) {
            LOG(ERROR) << "Fail to truncate table";
            return -1;
        }
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            std::cout << response << std::endl;
        } else {
            LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
            return -1;
        }
    }

    // prepare data for select, update
    if (FLAGS_op_type != 0) {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            brpc::MysqlRequest request;
            if (!request.Query(insert)) {
                LOG(ERROR) << "Fail to execute command";
                return -1;
            }
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            if (cntl.Failed()) {
                LOG(ERROR) << cntl.ErrorText();
                return -1;
            }
            if (!response.reply(0).is_ok()) {
                LOG(ERROR) << "prepare data failed";
                return -1;
            }
        }
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
        args[i].mysql_channel = &channel;
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
