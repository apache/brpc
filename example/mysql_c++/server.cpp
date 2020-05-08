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

// A server to receive CreateDatabaseRequest and send back CreateDatabaseResponse.

#include <algorithm>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/policy/mysql_protocol.h>

DEFINE_bool(echo_attachment, true, "Mysql attachment as well");
DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(logoff_ms, 2000, "Maximum duration of server's LOGOFF state "
             "(waiting for client to close connection before server stops)");
DEFINE_bool(show_full_sql, false, "Show the full sql in query");

// Your implementation of example::MysqlService
// Notice that implementing brpc::Describable grants the ability to put
// additional information in /status.
namespace example {
class MysqlServiceImpl : public ::brpc::policy::MysqlService {
public:
    static const size_t SqlShowLengthLimit = 1024;
    MysqlServiceImpl() {};
    virtual ~MysqlServiceImpl() {};
    void Query(google::protobuf::RpcController* cntl_base,
               const ::brpc::policy::QueryRequest* request,
               ::brpc::policy::QueryResponse* response,
               google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        const std::string& sql = request->sql();
        if (FLAGS_show_full_sql) {
            LOG(INFO) << "Query: " << sql;
        } else {
            auto length_limited  = std::min(SqlShowLengthLimit, sql.length());
            LOG(INFO) << "Query: " << butil::StringPiece(sql.c_str(), length_limited);
        }

        // sql parser works here.
        // sqlparser.parse(sql);
        // ......
        // There is also a MysqlConnContext in the Socket
        // which can be retrieved from Controller, it's a nice place
        // to store/read connection-based state.
    }

    void Quit(google::protobuf::RpcController* cntl_base,
              const ::brpc::policy::QuitRequest* request,
              ::brpc::policy::QuitResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        LOG(INFO) << "Quit";
    }

    void Ping(google::protobuf::RpcController* cntl_base,
              const ::brpc::policy::PingRequest* request,
              ::brpc::policy::PingResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        LOG(INFO) << "Ping";
    }

    void UnknownMethod(google::protobuf::RpcController* cntl_base,
                       const ::brpc::policy::UnknownMethodRequest* request,
                       ::brpc::policy::UnknownMethodResponse* response,
                       google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        LOG(INFO) << "Unknown command id: " << request->command_id();

        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        cntl->SetFailed(brpc::ERESPONSE, "Unknown command id: %d",
                        request->command_id());
    }
};
}  // namespace example

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    example::MysqlServiceImpl mysql_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&mysql_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    // Unlike the HTTP protocol, the mysql protocol has no obvious protocol identity,
    // and cannot coexist with other protocols.
    // The mysql protocol should be the only one enabled.
    options.enabled_protocols = "mysql";
    // We should order the brpc framework to call a specified function
    // to send the mysql initial handshake packet on new client connection established.
    options.on_new_connection_server_send_initial_packet = brpc::policy::ServerSendInitialPacketDemo;
    //options.auth = new MysqlAuthenticator;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start MysqlServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
