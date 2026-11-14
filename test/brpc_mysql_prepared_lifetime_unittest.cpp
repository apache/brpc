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

#include <gtest/gtest.h>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <string>
#include <thread>

#include "bthread/countdown_event.h"
#include "butil/fd_guard.h"
#include "butil/endpoint.h"
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/policy/mysql/mysql.h"

namespace {

class PreparedStatementServer {
public:
    PreparedStatementServer() : _released(false), _ok(false) {}
    ~PreparedStatementServer() {
        release();
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    bool start() {
        butil::EndPoint endpoint;
        if (butil::str2endpoint("127.0.0.1:0", &endpoint) != 0) {
            return false;
        }
        _listener.reset(butil::tcp_listen(endpoint));
        if (_listener < 0 || butil::get_local_side(_listener, &_endpoint) != 0) {
            return false;
        }
        _thread = std::thread(&PreparedStatementServer::serve, this);
        return true;
    }

    int port() const { return _endpoint.port; }
    void release() {
        if (!_released.exchange(true)) {
            _request_destroyed.signal();
        }
    }
    bool join() {
        _thread.join();
        return _ok;
    }

private:
    static bool read_packet(int fd, std::string* payload) {
        char header[4];
        if (recv(fd, header, sizeof(header), MSG_WAITALL) != sizeof(header)) {
            return false;
        }
        size_t len = static_cast<unsigned char>(header[0]) |
            (static_cast<size_t>(static_cast<unsigned char>(header[1])) << 8) |
            (static_cast<size_t>(static_cast<unsigned char>(header[2])) << 16);
        if (len == 0 || len > 1024) {
            return false;
        }
        payload->resize(len);
        return recv(fd, &(*payload)[0], len, MSG_WAITALL) == static_cast<ssize_t>(len);
    }

    void serve() {
        pollfd pfd = {_listener, POLLIN, 0};
        if (poll(&pfd, 1, 5000) <= 0) {
            return;
        }
        butil::fd_guard client(accept(_listener, nullptr, nullptr));
        if (client < 0) {
            return;
        }
        timeval timeout = {5, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
        int no_sigpipe = 1;
        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        std::string packet;
        if (read_packet(client, &packet) && static_cast<unsigned char>(packet[0]) == 0x16) {
            _request_destroyed.wait();
            const char prepare_ok[] = {12, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            const char execute_ok[] = {7, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
#ifdef MSG_NOSIGNAL
            const int send_flags = MSG_NOSIGNAL;
#else
            const int send_flags = 0;
#endif
            if (send(client, prepare_ok, sizeof(prepare_ok), send_flags) == sizeof(prepare_ok) &&
                read_packet(client, &packet) && static_cast<unsigned char>(packet[0]) == 0x17 &&
                send(client, execute_ok, sizeof(execute_ok), send_flags) == sizeof(execute_ok)) {
                _ok = true;
            }
        }
    }

    butil::fd_guard _listener;
    butil::EndPoint _endpoint;
    std::atomic<bool> _released;
    bool _ok;
    bthread::CountdownEvent _request_destroyed;
    std::thread _thread;
};

TEST(MysqlRequestTest, async_prepared_request_outlives_request) {
    PreparedStatementServer server;
    ASSERT_TRUE(server.start());
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = "pooled";
    options.timeout_ms = 5000;
    options.max_retry = 0;
    ASSERT_EQ(0, channel.Init("127.0.0.1", server.port(), &options));
    brpc::MysqlStatementUniquePtr statement = brpc::NewMysqlStatement(channel, "SELECT 1");
    brpc::Controller controller;
    brpc::MysqlResponse response;
    {
        brpc::MysqlRequest request(statement.get());
        channel.CallMethod(nullptr, &controller, &request, &response, brpc::DoNothing());
    }
    server.release();
    brpc::Join(controller.call_id());
    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    ASSERT_EQ(1, response.reply_size());
    EXPECT_TRUE(response.reply(0).is_ok());
    EXPECT_TRUE(server.join());
}

}  // namespace
