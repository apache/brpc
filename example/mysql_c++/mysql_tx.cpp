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
DEFINE_bool(readonly, false, "readonly transaction");
DEFINE_int32(isolation_level, 0, "transaction isolation level");

namespace brpc {
const char* logo();
}

// Send `command' to mysql-server via `channel'
static bool access_mysql(brpc::Channel& channel, const std::vector<std::string>& commands) {
    brpc::MysqlTransactionOptions options;
    options.readonly = FLAGS_readonly;
    options.isolation_level = brpc::MysqlIsolationLevel(FLAGS_isolation_level);
    auto tx(brpc::NewMysqlTransaction(channel, options));
    if (tx == NULL) {
        LOG(ERROR) << "Fail to create transaction";
        return false;
    }

    for (auto it = commands.begin(); it != commands.end(); ++it) {
        brpc::MysqlRequest request(tx.get());
        if (!request.Query(*it)) {
            LOG(ERROR) << "Fail to add command";
            tx->rollback();
            return false;
        }
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
            tx->rollback();
            return false;
        }
        // check response
        std::cout << response << std::endl;
        for (size_t i = 0; i < response.reply_size(); ++i) {
            if (response.reply(i).is_error()) {
                tx->rollback();
                return false;
            }
        }
    }
    tx->commit();
    return true;
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
        if (!access_mysql(channel, commands)) {
            return -1;
        }
    }
    return 0;
}

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
