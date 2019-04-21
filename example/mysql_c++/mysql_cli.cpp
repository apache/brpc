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

#include <signal.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/mysql.h>
#include <brpc/policy/mysql_authenticator.h>

DEFINE_string(connection_type,
              "single",
              "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 3306, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(schema, "brpc_test", "schema");
DEFINE_string(params, "", "params");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 0, "Max retries(not including the first RPC)");

namespace brpc {
const char* logo();
}

// Send `command' to mysql-server via `channel'
static bool access_mysql(brpc::Channel& channel, const char* command) {
    brpc::MysqlRequest request;
    if (!request.Query(command)) {
        LOG(ERROR) << "Fail to add command";
        return false;
    }
    brpc::MysqlResponse response;
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (!cntl.Failed()) {
        std::cout << response << std::endl;
    } else {
        LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
        return false;
    }
    return true;
}

// For freeing the memory returned by readline().
struct Freer {
    void operator()(char* mem) {
        free(mem);
    }
};

static void dummy_handler(int) {}

// The getc for readline. The default getc retries reading when meeting
// EINTR, which is not what we want.
static bool g_canceled = false;
static int cli_getc(FILE* stream) {
    int c = getc(stream);
    if (c == EOF && errno == EINTR) {
        g_canceled = true;
        return '\n';
    }
    return c;
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
        FLAGS_user, FLAGS_password, FLAGS_schema, FLAGS_params);
    if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    if (argc <= 1) {  // interactive mode
        // We need this dummy signal hander to interrupt getc (and returning
        // EINTR), SIG_IGN did not work.
        signal(SIGINT, dummy_handler);

        // Hook getc of readline.
        rl_getc_function = cli_getc;

        // Print welcome information.
        printf("%s\n", brpc::logo());
        printf(
            "This command-line tool mimics the look-n-feel of official "
            "mysql-cli, as a demostration of brpc's capability of"
            " talking to mysql-server. The output and behavior is "
            "not exactly same with the official one.\n\n");

        for (;;) {
            char prompt[128];
            snprintf(prompt, sizeof(prompt), "mysql %s> ", FLAGS_server.c_str());
            std::unique_ptr<char, Freer> command(readline(prompt));
            if (command == NULL || *command == '\0') {
                if (g_canceled) {
                    // No input after the prompt and user pressed Ctrl-C,
                    // quit the CLI.
                    return 0;
                }
                // User entered an empty command by just pressing Enter.
                continue;
            }
            if (g_canceled) {
                // User entered sth. and pressed Ctrl-C, start a new prompt.
                g_canceled = false;
                continue;
            }
            // Add user's command to history so that it's browse-able by
            // UP-key and search-able by Ctrl-R.
            add_history(command.get());

            if (!strcmp(command.get(), "help")) {
                printf("This is a mysql CLI written in brpc.\n");
                continue;
            }
            if (!strcmp(command.get(), "quit")) {
                // Although quit is a valid mysql command, it does not make
                // too much sense to run it in this CLI, just quit.
                return 0;
            }
            access_mysql(channel, command.get());
        }
    } else {
        std::string command;
        command.reserve(argc * 16);
        for (int i = 1; i < argc; ++i) {
            if (i != 1) {
                command.push_back(';');
            }
            command.append(argv[i]);
        }
        if (!access_mysql(channel, command.c_str())) {
            return -1;
        }
    }
    return 0;
}
