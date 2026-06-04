// Copyright (c) 2019 Baidu, Inc.
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

// A brpc based mysql transaction example
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <bthread/bthread.h>
#include <brpc/channel.h>
#include <brpc/mysql.h>
#include <brpc/policy/mysql_authenticator.h>

DEFINE_string(connection_type, "pooled", "Connection type. Available values: pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 3306, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(schema, "brpc_test", "schema");
DEFINE_string(params, "", "params");
DEFINE_string(collation, "utf8mb4_general_ci", "collation");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 0, "Max retries(not including the first RPC)");
DEFINE_int32(thread_num, 1, "Number of threads to send requests");
DEFINE_int32(count, 1, "Number of request to send pre thread");

namespace brpc {
const char* logo();
}

struct SenderArgs {
    brpc::Channel* mysql_channel;
    brpc::MysqlStatement* mysql_stmt;
    std::vector<std::string> commands;
};

// Send `command' to mysql-server via `channel'
static void* access_mysql(void* void_args) {
    SenderArgs* args = (SenderArgs*)void_args;
    brpc::Channel* channel = args->mysql_channel;
    brpc::MysqlStatement* stmt = args->mysql_stmt;
    const std::vector<std::string>& commands = args->commands;

    for (int i = 0; i < FLAGS_count; ++i) {
        // for (;;) {
        brpc::MysqlRequest request(stmt);
        for (size_t i = 1; i < commands.size(); i += 2) {
            if (commands[i] == "int8") {
                int8_t val = strtol(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add int8 param";
                    return NULL;
                }
            } else if (commands[i] == "uint8") {
                uint8_t val = strtoul(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add uint8 param";
                    return NULL;
                }
            } else if (commands[i] == "int16") {
                int16_t val = strtol(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add uint16 param";
                    return NULL;
                }
            } else if (commands[i] == "uint16") {
                uint16_t val = strtoul(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add uint16 param";
                    return NULL;
                }
            } else if (commands[i] == "int32") {
                int32_t val = strtol(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add int32 param";
                    return NULL;
                }
            } else if (commands[i] == "uint32") {
                uint32_t val = strtoul(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add uint32 param";
                    return NULL;
                }
            } else if (commands[i] == "int64") {
                int64_t val = strtol(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add int64 param";
                    return NULL;
                }
            } else if (commands[i] == "uint64") {
                uint64_t val = strtoul(commands[i + 1].c_str(), NULL, 10);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add uint64 param";
                    return NULL;
                }
            } else if (commands[i] == "float") {
                float val = strtof(commands[i + 1].c_str(), NULL);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add float param";
                    return NULL;
                }
            } else if (commands[i] == "double") {
                double val = strtod(commands[i + 1].c_str(), NULL);
                if (!request.AddParam(val)) {
                    LOG(ERROR) << "Fail to add double param";
                    return NULL;
                }
            } else if (commands[i] == "string") {
                if (!request.AddParam(commands[i + 1])) {
                    LOG(ERROR) << "Fail to add string param";
                    return NULL;
                }
            } else {
                LOG(ERROR) << "Wrong param type " << commands[i];
            }
        }

        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel->CallMethod(NULL, &cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
            return NULL;
        }

        // if (response.reply(0).is_error()) {
        // check response
        std::cout << response << std::endl;
        // }
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

    if (argc <= 1) {
        LOG(ERROR) << "No sql statement args";
    } else {
        std::vector<std::string> commands;
        commands.reserve(argc * 16);
        for (int i = 1; i < argc; ++i) {
            commands.push_back(argv[i]);
        }
        auto stmt(brpc::NewMysqlStatement(channel, commands[0]));
        if (stmt == NULL) {
            LOG(ERROR) << "Fail to create mysql statement";
            return -1;
        }

        std::vector<SenderArgs> args;
        std::vector<bthread_t> bids;
        args.resize(FLAGS_thread_num);
        bids.resize(FLAGS_thread_num);

        for (int i = 0; i < FLAGS_thread_num; ++i) {
            args[i].mysql_channel = &channel;
            args[i].mysql_stmt = stmt.get();
            args[i].commands = commands;
            if (bthread_start_background(&bids[i], NULL, access_mysql, &args[i]) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }

        for (int i = 0; i < FLAGS_thread_num; ++i) {
            bthread_join(bids[i], NULL);
        }
    }

    return 0;
}

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
