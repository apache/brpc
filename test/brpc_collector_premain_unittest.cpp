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

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <gtest/gtest.h>
#include "butil/files/scoped_temp_dir.h"

// Compile these implementations in this translation unit to fix initialization
// order independently of the linker and shared-library loading order. Link the
// remaining dependencies statically so there is only one copy of each flag and
// singleton. The CollectorExitGuard must be registered before the RPC flags.
#include "bvar/collector.cpp"

namespace {

const char kDirectoryEnv[] = "BRPC_COLLECTOR_PREMAIN_TEST_DIR";
std::atomic<bool> g_dump_started{false};
std::atomic<bool> g_dump_finished{false};
bool g_initialized_before_flags = false;

void Require(bool condition, const char* message) {
    if (!condition) {
        (void)write(STDERR_FILENO, message, strlen(message));
        (void)write(STDERR_FILENO, "\n", 1);
        std::_Exit(1);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate) {
    const int64_t deadline = butil::monotonic_time_ms() + 10000;
    while (!predicate() && butil::monotonic_time_ms() < deadline) {
        usleep(1000);
    }
    return predicate();
}

void VerifyAfterFlagDestruction() {
    Require(g_dump_finished.load(),
            "RPC flags were destroyed before the pre-main collector stopped");
}

struct InitCollectorBeforeFlags {
    InitCollectorBeforeFlags() {
        // Only the re-executed death-test child creates an early collector.
        if (getenv(kDirectoryEnv) == nullptr) {
            return;
        }
        alarm(15);
        Require(butil::has_leaky_singleton<bvar::Collector>() == nullptr,
                "collector was already initialized");
        butil::get_leaky_singleton<bvar::Collector>();
        g_initialized_before_flags = true;
        // At exit: RpcDumpExitGuard -> RPC flags -> verification -> Collector's
        // atexit hook -> CollectorExitGuard. Neither fallback can mask failure.
        Require(atexit(VerifyAfterFlagDestruction) == 0,
                "cannot register exit verification");
    }
};
InitCollectorBeforeFlags g_init_collector_before_flags;

}  // namespace

// Keep this below the initializer and its exit verification registration.
#include "brpc/rpc_dump.cpp"

namespace {

class Request : public brpc::SampledRequest {
public:
    Request() {
        request.append("pre-main collector request");
        meta.set_protocol_type(brpc::PROTOCOL_BAIDU_STD);
    }

    void dump_and_destroy(size_t round) override {
        g_dump_started.store(true);
        // Resume only when shutdown starts, so completion cannot race ahead of
        // exit verification if RpcDumpExitGuard is removed. SaveFlags must
        // still be able to read rpc_dump_dir at this point.
        Require(WaitUntil([] {
            return bvar::g_collector_stopped.load(butil::memory_order_acquire);
        }), "collector shutdown did not start");
        brpc::SampledRequest::dump_and_destroy(round);
    }

    void destroy() override {
        delete this;
        g_dump_finished.store(true);
    }
};

void RunExit() {
    Require(g_initialized_before_flags,
            "collector was not initialized before the RPC flags");
    brpc::FLAGS_rpc_dump_dir = getenv(kDirectoryEnv);
    brpc::FLAGS_rpc_dump_max_requests_in_one_file = 1;
    (new Request)->submit();
    Require(WaitUntil([] { return g_dump_started.load(); }),
            "RPC dump did not start");
    std::exit(0);
}

TEST(CollectorPremainDeathTest, stops_before_rpc_flags_are_destroyed) {
#ifdef GTEST_FLAG_SET
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#else
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
#endif
    butil::ScopedTempDir directory;
    const bool parent = getenv(kDirectoryEnv) == nullptr;
    if (parent) {
        ASSERT_TRUE(directory.CreateUniqueTempDir());
        ASSERT_EQ(0, setenv(kDirectoryEnv,
                           directory.path().value().c_str(), 1));
    }
    EXPECT_EXIT(RunExit(), ::testing::ExitedWithCode(0), "");
    if (parent) {
        EXPECT_EQ(0, unsetenv(kDirectoryEnv));
        brpc::SampleIterator it(directory.path().value());
        std::unique_ptr<brpc::SampledRequest> request(it.Next());
        ASSERT_TRUE(request);
        EXPECT_EQ("pre-main collector request", request->request.to_string());
        std::unique_ptr<brpc::SampledRequest> extra(it.Next());
        EXPECT_FALSE(extra);
    }
}

}  // namespace
