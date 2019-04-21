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
extern "C" {
#include <mysql/mysql.h>
}
#include <brpc/controller.h>
#include <bvar/bvar.h>
#include <bthread/bthread.h>
#include <brpc/server.h>

DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 3306, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(schema, "brpc_test", "schema");
DEFINE_string(params, "", "params");
DEFINE_string(data, "ABCDEF", "data");
DEFINE_int32(thread_num, 50, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_int32(dummy_port, -1, "port of dummy server(for monitoring)");
DEFINE_int32(op_type, 0, "CRUD operation, 0:INSERT, 1:SELECT, 3:UPDATE");
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");

struct SenderArgs {
    int base_index;
    MYSQL* mysql_conn;
};

const std::string insert =
    "insert into mysqlclient_press(col1,col2,col3,col4) values "
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
        command << "select * from mysqlclient_press where id = " << args->base_index + 1;
    } else if (FLAGS_op_type == 2) {
        command << "update brpc_press set col2 = '" + FLAGS_data + "' where id = "
                << args->base_index + 1;
    } else {
        LOG(ERROR) << "wrong op type " << FLAGS_op_type;
    }

    std::string command_str = command.str();

    while (!brpc::IsAskedToQuit()) {
        const int64_t begin_time_us = butil::cpuwide_time_us();
        const int rc = mysql_real_query(args->mysql_conn, command_str.c_str(), command_str.size());
        if (rc != 0) {
            goto ERROR;
        }

        if (mysql_errno(args->mysql_conn) == 0) {
            if (FLAGS_op_type == 0) {
                CHECK_EQ(mysql_affected_rows(args->mysql_conn), 1);
            } else if (FLAGS_op_type == 1) {
                MYSQL_RES* res = mysql_store_result(args->mysql_conn);
                if (res == NULL) {
                    LOG(INFO) << "not found";
                } else {
                    CHECK_EQ(mysql_num_rows(res), 1);
                    mysql_free_result(res);
                }
            } else if (FLAGS_op_type == 2) {
            }
            const int64_t elp = butil::cpuwide_time_us() - begin_time_us;
            g_latency_recorder << elp;
        } else {
            goto ERROR;
        }

        if (false) {
        ERROR:
            const int64_t elp = butil::cpuwide_time_us() - begin_time_us;
            g_error_count << 1;
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << mysql_error(args->mysql_conn) << " latency=" << elp;
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

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    MYSQL* conn = mysql_init(NULL);
    if (!mysql_real_connect(conn,
                            FLAGS_server.c_str(),
                            FLAGS_user.c_str(),
                            FLAGS_password.c_str(),
                            FLAGS_schema.c_str(),
                            FLAGS_port,
                            NULL,
                            0)) {
        LOG(ERROR) << mysql_error(conn);
        return -1;
    }

    // create table mysqlclient_press
    {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS `mysqlclient_press`(`id` INT UNSIGNED AUTO_INCREMENT, "
            "`col1` "
            "VARCHAR(100) NOT NULL, `col2` VARCHAR(1024) NOT NULL, `col3` decimal(10,0) NOT "
            "NULL, `col4` DATE, PRIMARY KEY ( `id` )) ENGINE=InnoDB DEFAULT CHARSET=utf8;";
        const int rc = mysql_real_query(conn, sql, strlen(sql));
        if (rc != 0) {
            LOG(ERROR) << "Fail to execute sql, " << mysql_error(conn);
            return -1;
        }

        if (mysql_errno(conn) != 0) {
            LOG(ERROR) << "Fail to store result, " << mysql_error(conn);
            return -1;
        }
    }

    // truncate table
    {
        const char* sql = "truncate table mysqlclient_press";
        const int rc = mysql_real_query(conn, sql, strlen(sql));
        if (rc != 0) {
            LOG(ERROR) << "Fail to execute sql, " << mysql_error(conn);
            return -1;
        }

        if (mysql_errno(conn) != 0) {
            LOG(ERROR) << "Fail to store result, " << mysql_error(conn);
            return -1;
        }
    }

    // prepare data for select, update
    if (FLAGS_op_type != 0) {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            const int rc = mysql_real_query(conn, insert.c_str(), insert.size());
            if (rc != 0) {
                LOG(ERROR) << "Fail to execute sql, " << mysql_error(conn);
                return -1;
            }

            if (mysql_errno(conn) != 0) {
                LOG(ERROR) << "Fail to store result, " << mysql_error(conn);
                return -1;
            }
        }
    }

    // test CRUD operations
    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    bids.resize(FLAGS_thread_num);
    pids.resize(FLAGS_thread_num);
    std::vector<SenderArgs> args;
    args.resize(FLAGS_thread_num);
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        MYSQL* conn = mysql_init(NULL);
        if (!mysql_real_connect(conn,
                                FLAGS_server.c_str(),
                                FLAGS_user.c_str(),
                                FLAGS_password.c_str(),
                                FLAGS_schema.c_str(),
                                FLAGS_port,
                                NULL,
                                0)) {
            LOG(ERROR) << mysql_error(conn);
            return -1;
        }
        args[i].base_index = i;
        args[i].mysql_conn = conn;
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
