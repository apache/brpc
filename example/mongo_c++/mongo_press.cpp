// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// A multi-threaded client getting keys from a redis-server constantly.

#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <bvar/bvar.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include <brpc/mongo.h>

DEFINE_string(connection_type, "single",
              "Connection type. Available values: pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 27017, "Port of server");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_string(collection, "test_collection", "collection name");
DEFINE_string(db, "test_db", "database name");

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
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    brpc::Controller cntl;
    butil::bson::UniqueBsonPtr command(
        BCON_NEW("insert", BCON_UTF8(FLAGS_collection.c_str()),
                 "$db", BCON_UTF8(FLAGS_db.c_str()),
                 "comment", BCON_UTF8("brpc mongo press")));

    brpc::MongoMessage req;
    brpc::MongoMessage resp;
    req.set_body(std::move(command));
    req.set_key("documents");
    for (size_t i = 0; i < 10; i++) {
        char user_id[64];
        char user_name[64];
        ::snprintf(user_id, sizeof(user_id), "user-%lu", i);
        ::snprintf(user_name, sizeof(user_name), "user-name-%lu", i);
        req.add_doc_sequence(butil::bson::UniqueBsonPtr(BCON_NEW(
                       "user", BCON_UTF8(user_id),
                       "_id", BCON_INT32(i),
                       "user_name", BCON_UTF8(user_name))));
    }
    LOG(INFO) << "MongoRequest: " << req;
    channel.CallMethod(nullptr, &cntl, &req, &resp, nullptr);

    if (!cntl.Failed()) {
        LOG(INFO) << "OK: \n" << req << "\n" <<  resp;
    } else {
        LOG(INFO) << "Failed: \n" << req << "\n" <<  resp;
        LOG(INFO) << cntl.ErrorText();
        return 0;
    }

    while (!brpc::IsAskedToQuit()) {
        brpc::Controller cntl;
        brpc::MongoMessage req;
        brpc::MongoMessage resp;
        butil::bson::UniqueBsonPtr command(
            BCON_NEW("find", BCON_UTF8(FLAGS_collection.c_str()),
                     "$db", BCON_UTF8(FLAGS_db.c_str()),
                     "comment", BCON_UTF8("brpc mongo press query")));
        req.set_body(std::move(command));
        channel.CallMethod(nullptr, &cntl, &req, &resp, nullptr);
        if (!cntl.Failed()) {
            LOG(INFO) << "OK: \n" << req << "\n" <<  resp;
        } else {
            LOG(INFO) << cntl.ErrorText();
        }
        bthread_usleep(1000*1000);
    }
    return 0;
}
