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

#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <gflags/gflags.h>
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "butil/time.h"
#include "test.pb.h"

DEFINE_string(server, "127.0.0.1:8003", "Performance test server address");
DEFINE_string(mode, "auto", "Initial mode: legacy, auto, tcp_only");
DEFINE_string(mode_file, "",
              "Read a recovery mode from this file while running");
DEFINE_string(connection_type, "short",
              "short exercises new admission on each RPC");
DEFINE_string(transport, "urma", "urma or tcp");
DEFINE_int32(seconds, 60, "Duration of the fault-injection run");
DEFINE_int32(interval_ms, 100, "Pause between RPCs");
DEFINE_int32(timeout_ms, 1000, "RPC deadline");
DEFINE_int32(cooldown_ms, 5000, "Path cooldown");
DEFINE_int32(dummy_port, 8001, "Local /vars port; 0 disables it");
DEFINE_int32(attachment_size, 0,
             "Echo attachment bytes and verify the returned content");

static bool SetMode(const std::string& value) {
    using brpc::urma::UrmaRecoveryMode;
    UrmaRecoveryMode mode;
    if (value == "legacy") {
        mode = UrmaRecoveryMode::LEGACY;
    } else if (value == "auto") {
        mode = UrmaRecoveryMode::AUTO;
    } else if (value == "tcp_only") {
        mode = UrmaRecoveryMode::TCP_ONLY;
    } else {
        return false;
    }
    return brpc::urma::SetUrmaRecoveryMode(mode);
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_transport != "urma" && FLAGS_transport != "tcp") {
        std::cerr << "Invalid transport\n";
        return 1;
    }
    const bool cooldown_ok =
        brpc::urma::SetUrmaRecoveryCooldownMs(FLAGS_cooldown_ms);
    if (FLAGS_seconds <= 0 || FLAGS_interval_ms < 0 || FLAGS_timeout_ms <= 0 ||
        FLAGS_attachment_size < 0 || !cooldown_ok || !SetMode(FLAGS_mode)) {
        std::cerr
            << "Invalid duration, interval, timeout, cooldown, mode or attachment size\n";
        return 1;
    }
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.socket_mode = FLAGS_transport == "urma" ? brpc::SOCKET_MODE_URMA
                                                    : brpc::SOCKET_MODE_TCP;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms;
    options.connect_timeout_ms = FLAGS_timeout_ms;
    options.max_retry = 0;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        return 1;
    }
    if (FLAGS_dummy_port && brpc::StartDummyServerAt(FLAGS_dummy_port) != 0) {
        return 1;
    }
    std::string mode = FLAGS_mode;
    uint64_t succeeded = 0;
    uint64_t failed = 0;
    uint64_t validation_failed = 0;
    std::string payload(static_cast<size_t>(FLAGS_attachment_size), '\0');
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(i % 251);
    }
    const int64_t start = butil::monotonic_time_us();
    int64_t next_report = start;
    std::cout
        << "seconds,mode,success,failure,transport_negotiated,tcp_fallback_admissions,"
           "cooled_peers\n";
    while (!brpc::IsAskedToQuit() &&
           butil::monotonic_time_us() - start <
               static_cast<int64_t>(FLAGS_seconds) * 1000000) {
        if (!FLAGS_mode_file.empty()) {
            std::ifstream input(FLAGS_mode_file);
            std::string value;
            if (input >> value && value != mode) {
                if (SetMode(value)) {
                    mode = value;
                } else {
                    std::cerr << "Ignoring invalid recovery mode: " << value
                              << '\n';
                }
            }
        }
        test::PerfTestRequest request;
        request.set_echo_attachment(!payload.empty());
        test::PerfTestResponse response;
        brpc::Controller cntl;
        const uint64_t sequence = succeeded + failed;
        for (size_t i = 0; i < payload.size() && i < sizeof(sequence); ++i) {
            payload[i] = static_cast<char>(sequence >> (i * 8));
        }
        cntl.request_attachment().append(payload);
        test::PerfTestService_Stub(&channel).Test(&cntl, &request, &response,
                                                  nullptr);
        if (cntl.Failed()) {
            ++failed;
            std::cerr << "RPC failure: " << cntl.ErrorText() << '\n';
        } else if (!payload.empty() &&
                   cntl.response_attachment().to_string() != payload) {
            ++failed;
            ++validation_failed;
            std::cerr << "Attachment mismatch: expected=" << payload.size()
                      << " actual=" << cntl.response_attachment().size()
                      << '\n';
        } else {
            ++succeeded;
        }
        const int64_t now = butil::monotonic_time_us();
        if (now >= next_report) {
            const auto s = brpc::urma::GetUrmaRecoveryStatus();
            std::cout << (now - start) / 1000000 << ',' << mode << ','
                      << succeeded << ',' << failed << ','
                      << s.negotiated_connections << ','
                      << s.tcp_only_admissions + s.peer_cooldown_admissions
                      << ',' << s.cooled_peers << std::endl;
            next_report = now + 1000000;
        }
        usleep(static_cast<useconds_t>(FLAGS_interval_ms) * 1000);
    }
    std::cout << "completed success=" << succeeded << " failure=" << failed;
    if (!payload.empty()) {
        std::cout << " validation_failures=" << validation_failed;
    }
    std::cout << std::endl;
    return succeeded ? 0 : 2;
}
