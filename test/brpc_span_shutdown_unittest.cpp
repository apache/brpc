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
#include <thread>
#include <unistd.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/files/scoped_temp_dir.h"
#include "butil/time.h"
#include "brpc/span.h"

namespace brpc {
DECLARE_string(rpcz_database_dir);
bool has_span_db();
}

namespace {

const char kDirectoryEnv[] = "BRPC_SPAN_SHUTDOWN_TEST_DIR";

void Require(bool condition, const char* message) {
    if (!condition) {
        (void)write(STDERR_FILENO, message, strlen(message));
        (void)write(STDERR_FILENO, "\n", 1);
        std::_Exit(1);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int64_t timeout_ms = 10000) {
    const int64_t deadline_ms = butil::monotonic_time_ms() + timeout_ms;
    while (!predicate() && butil::monotonic_time_ms() < deadline_ms) {
        usleep(1000);
    }
    return predicate();
}

void PrepareChild() {
    // Also bound direct shutdown calls and thread joins if a regression hangs.
    alarm(15);
    const char* directory = getenv(kDirectoryEnv);
    Require(directory != nullptr, "missing test database directory");
    brpc::FLAGS_rpcz_database_dir = directory;
    brpc::ClearTlsParentSpan();
    Require(!brpc::has_span_db(), "child already has a span database");
}

std::shared_ptr<brpc::Span> NewSpan() {
    const int64_t now = butil::cpuwide_time_us();
    std::shared_ptr<brpc::Span> span = brpc::Span::CreateClientSpan(
        "test.Shutdown/Call", butil::gettimeofday_us() - now);
    Require(static_cast<bool>(span), "cannot create span");
    span->set_start_send_us(now);
    span->set_start_callback_us(now + 1);
    return span;
}

std::shared_ptr<brpc::Span> StartBlockedDump() {
    std::shared_ptr<brpc::Span> span = NewSpan();
    // The dumper publishes the DB before Index() reads info(). This lock keeps
    // the dump in flight without adding a hook to the production collector.
    Require(pthread_spin_lock(&span->_info_spinlock) == 0,
            "cannot lock span annotations");
    brpc::Span::Submit(span, butil::cpuwide_time_us());
    Require(WaitUntil([] { return brpc::has_span_db(); }),
            "collector did not open the span database");
    return span;
}

struct BlockerState {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

class CollectorBlocker : public bvar::Collected {
public:
    explicit CollectorBlocker(BlockerState* state) : _state(state) {}

    void dump_and_destroy(size_t) override {
        _state->entered.store(true);
        Require(WaitUntil([this] { return _state->release.load(); }),
                "collector blocker was not released");
        destroy();
    }

    void destroy() override { delete this; }
    bvar::CollectorSpeedLimit* speed_limit() override { return nullptr; }

private:
    BlockerState* _state;
};

class SpanShutdownDeathTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Exec each child: forking an existing collector would leave its
        // mutexes and queues without their worker threads.
        ::testing::FLAGS_gtest_death_test_style = "threadsafe";
        if (getenv(kDirectoryEnv) == nullptr) {
            ASSERT_TRUE(_directory.CreateUniqueTempDir());
            ASSERT_EQ(0, setenv(kDirectoryEnv, _directory.path().value().c_str(), 1));
            _owns_directory = true;
        }
    }

    void TearDown() override {
        if (_owns_directory) {
            EXPECT_EQ(0, unsetenv(kDirectoryEnv));
        }
    }

private:
    // The parent removes the child's files after EXPECT_EXIT has reaped it.
    butil::ScopedTempDir _directory;
    bool _owns_directory = false;
};

TEST_F(SpanShutdownDeathTest, stop_waits_for_inflight_dump) {
    EXPECT_EXIT({
        PrepareChild();
        std::shared_ptr<brpc::Span> span = StartBlockedDump();
        std::atomic<bool> stopping{false};
        std::atomic<bool> stopped{false};
        std::thread stopper([&] {
            stopping.store(true);
            brpc::StopSpanIndexing();
            stopped.store(true);
        });
        Require(WaitUntil([&] { return stopping.load(); }),
                "shutdown thread did not start");
        Require(!WaitUntil([&] { return stopped.load(); }, 100),
                "shutdown returned while a span dump was blocked");
        Require(pthread_spin_unlock(&span->_info_spinlock) == 0,
                "cannot release span annotations");
        Require(WaitUntil([&] { return stopped.load(); }),
                "shutdown did not finish after the dump was released");
        stopper.join();
        Require(!brpc::has_span_db(), "shutdown retained the span database");
        std::_Exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(SpanShutdownDeathTest, stop_skips_queued_first_span) {
    EXPECT_EXIT({
        PrepareChild();
        BlockerState blocker;
        (new CollectorBlocker(&blocker))->submit();
        Require(WaitUntil([&] { return blocker.entered.load(); }),
                "collector blocker did not start");
        std::shared_ptr<brpc::Span> span = NewSpan();
        std::weak_ptr<brpc::Span> weak_span = span;
        brpc::Span::Submit(span, butil::cpuwide_time_us());
        span.reset();
        Require(!weak_span.expired(), "queued span was not retained");

        // No span has reached dump_to_db(), so shutdown must already be
        // available without relying on lazy database initialization.
        brpc::StopSpanIndexing();
        Require(!brpc::has_span_db(), "shutdown opened the span database");
        blocker.release.store(true);
        Require(WaitUntil([&] { return weak_span.expired(); }),
                "collector did not release the queued span");
        Require(!brpc::has_span_db(), "late collector dump reopened the database");
        std::_Exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(SpanShutdownDeathTest, stop_is_idempotent_and_late_dump_is_skipped) {
    EXPECT_EXIT({
        PrepareChild();
        std::shared_ptr<brpc::Span> span = NewSpan();
        brpc::StopSpanIndexing();
        brpc::StopSpanIndexing();
        span->dump_to_db();
        Require(!brpc::has_span_db(), "direct late dump reopened the database");
        std::_Exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(SpanShutdownDeathTest, process_exit_waits_for_inflight_dump) {
    EXPECT_EXIT({
        PrepareChild();
        std::shared_ptr<brpc::Span> span = NewSpan();
        std::atomic<bool> locked{false};
        std::atomic<bool> exiting{false};
        std::thread([span, &locked, &exiting] {
            // Keep lock and unlock on the same thread, as required by POSIX.
            Require(pthread_spin_lock(&span->_info_spinlock) == 0,
                    "cannot lock span annotations during exit");
            locked.store(true);
            Require(WaitUntil([&] { return exiting.load(); }),
                    "main thread did not begin process exit");
            usleep(200000);
            // Print while the dump is still blocked. Without an exit barrier
            // the process finishes before this thread can emit the marker.
            const char marker[] = "releasing span during process exit\n";
            Require(write(STDERR_FILENO, marker, sizeof(marker) - 1) ==
                    static_cast<ssize_t>(sizeof(marker) - 1),
                    "cannot write the exit marker");
            Require(pthread_spin_unlock(&span->_info_spinlock) == 0,
                    "cannot release span annotations during exit");
        }).detach();
        Require(WaitUntil([&] { return locked.load(); }),
                "annotation holder did not start");
        brpc::Span::Submit(span, butil::cpuwide_time_us());
        Require(WaitUntil([] { return brpc::has_span_db(); }),
                "collector did not open the span database");
        // Exercise the registered process-exit hook, with no explicit stop
        // and no wait for the collector to release this span.
        exiting.store(true);
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "releasing span during process exit");
}

}  // namespace
