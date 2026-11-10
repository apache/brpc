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
#include <thread>
#include <unistd.h>
#include <gtest/gtest.h>
#include "butil/files/scoped_temp_dir.h"
#include "butil/memory/singleton_on_pthread_once.h"
#include "brpc/rpc_dump.h"

namespace bvar {
class Collector;
}

namespace brpc {
DECLARE_string(rpc_dump_dir);
DECLARE_int32(rpc_dump_max_requests_in_one_file);
}

namespace {

const char kDirectoryEnv[] = "BRPC_COLLECTOR_SHUTDOWN_TEST_DIR";

void Require(bool condition, const char* message) {
    if (!condition) {
        (void)write(STDERR_FILENO, message, strlen(message));
        (void)write(STDERR_FILENO, "\n", 1);
        std::_Exit(1);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int64_t timeout_ms = 10000) {
    const int64_t deadline = butil::monotonic_time_ms() + timeout_ms;
    while (!predicate() && butil::monotonic_time_ms() < deadline) {
        usleep(1000);
    }
    return predicate();
}

struct SampleState {
    std::atomic<int> entered{0};
    std::atomic<int> destroyed{0};
    std::atomic<bool> release{true};
};

class Sample : public bvar::Collected {
public:
    explicit Sample(SampleState* state,
                    bvar::CollectorPreprocessor* preprocessor = nullptr)
        : _state(state), _preprocessor(preprocessor) {}

    void dump_and_destroy(size_t) override {
        _state->entered.fetch_add(1);
        Require(WaitUntil([this] { return _state->release.load(); }),
                "sample was not released");
        destroy();
    }

    void destroy() override {
        SampleState* state = _state;
        delete this;
        state->destroyed.fetch_add(1);
    }

    bvar::CollectorSpeedLimit* speed_limit() override { return nullptr; }
    bvar::CollectorPreprocessor* preprocessor() override { return _preprocessor; }

private:
    SampleState* _state;
    bvar::CollectorPreprocessor* _preprocessor;
};

// Put another sample behind the blocker in the very same dump batch.
class AppendSample : public bvar::CollectorPreprocessor {
public:
    explicit AppendSample(SampleState* state) : _state(state) {}
    void process(std::vector<bvar::Collected*>& samples) override {
        samples.push_back(new Sample(_state));
    }

private:
    SampleState* _state;
};

class CollectorShutdownDeathTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Re-exec children so inherited collectors never lose their workers.
#ifdef GTEST_FLAG_SET
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#else
        ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
#endif
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
    butil::ScopedTempDir _directory;
    bool _owns_directory = false;
};

TEST_F(CollectorShutdownDeathTest, stop_before_first_sample_prevents_creation) {
    EXPECT_EXIT({
        alarm(15);
        Require(butil::has_leaky_singleton<bvar::Collector>() == nullptr,
                "collector already exists");
        bvar::detail::StopCollector();
        bvar::detail::StopCollector();
        SampleState state;
        (new Sample(&state))->submit();
        Require(state.destroyed.load() == 1 && state.entered.load() == 0,
                "late sample was not discarded");
        bvar::CollectorSpeedLimit limit = {};
        limit.sampling_range = bvar::COLLECTOR_SAMPLING_BASE;
        limit.count_before_grabbed.store(1000000, butil::memory_order_relaxed);
        bvar::is_collectable(&limit);
        Require(butil::has_leaky_singleton<bvar::Collector>() == nullptr,
                "shutdown created a collector");
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(CollectorShutdownDeathTest, concurrent_stops_wait_and_discard_pending_samples) {
    EXPECT_EXIT({
        alarm(15);
        SampleState blocked;
        blocked.release.store(false);
        SampleState queued;
        AppendSample append(&queued);
        (new Sample(&blocked, &append))->submit();
        Require(WaitUntil([&] { return blocked.entered.load() == 1; }),
                "dump did not start");
        // Exercise submissions from a thread whose TLS has already been freed.
        std::thread producer([&] {
            for (int i = 0; i < 16; ++i) {
                (new Sample(&queued))->submit();
            }
        });
        producer.join();

        std::atomic<int> stopping{0};
        std::atomic<int> stopped{0};
        auto stop = [&] {
            stopping.fetch_add(1);
            bvar::detail::StopCollector();
            stopped.fetch_add(1);
        };
        std::thread stopper1(stop);
        std::thread stopper2(stop);
        Require(WaitUntil([&] { return stopping.load() == 2; }),
                "stop callers did not start");
        Require(!WaitUntil([&] { return stopped.load() != 0; }, 100),
                "stop returned before the in-flight dump finished");
        blocked.release.store(true);
        stopper1.join();
        stopper2.join();
        Require(blocked.destroyed.load() == 1, "in-flight sample was not released");
        Require(queued.destroyed.load() == 17 && queued.entered.load() == 0,
                "pending samples were dumped or not released");
        (new Sample(&queued))->submit();
        Require(queued.destroyed.load() == 18 && queued.entered.load() == 0,
                "late submission was not discarded");
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(CollectorShutdownDeathTest, stop_races_first_submissions) {
    EXPECT_EXIT({
        alarm(15);
        SampleState state;
        std::atomic<bool> start{false};
        std::atomic<int> ready{0};
        std::thread producers[8];
        for (auto& producer : producers) {
            producer = std::thread([&] {
                ready.fetch_add(1);
                while (!start.load()) {
                    std::this_thread::yield();
                }
                (new Sample(&state))->submit();
            });
        }
        Require(WaitUntil([&] { return ready.load() == 8; }),
                "producers did not start");
        start.store(true);
        bvar::detail::StopCollector();
        const int dumped = state.entered.load();
        for (auto& producer : producers) {
            producer.join();
        }
        bvar::detail::StopCollector();
        Require(state.entered.load() == dumped, "dump started after stop returned");
        SampleState late;
        (new Sample(&late))->submit();
        Require(late.destroyed.load() == 1 && late.entered.load() == 0,
                "racing creation restarted the collector");
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(CollectorShutdownDeathTest, stop_wakes_idle_workers) {
    EXPECT_EXIT({
        alarm(15);
        SampleState state;
        (new Sample(&state))->submit();
        Require(WaitUntil([&] { return state.destroyed.load() == 1; }),
                "sample was not dumped");
        bvar::detail::StopCollector();
        bvar::detail::StopCollector();
        Require(butil::has_leaky_singleton<bvar::Collector>() != nullptr,
                "shutdown destroyed the singleton");
        (new Sample(&state))->submit();
        Require(state.destroyed.load() == 2 && state.entered.load() == 1,
                "idle collector restarted after shutdown");
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

class Request : public brpc::SampledRequest {
public:
    explicit Request(SampleState* state) : _state(state) {
        request.append("collector shutdown request");
        meta.set_protocol_type(brpc::PROTOCOL_BAIDU_STD);
    }

    void dump_and_destroy(size_t round) override {
        _state->entered.fetch_add(1);
        Require(WaitUntil([this] { return _state->release.load(); }),
                "RPC dump was not released");
        // Exercise the actual RpcDumpContext::SaveFlags(), including first use.
        brpc::SampledRequest::dump_and_destroy(round);
    }

    void destroy() override {
        SampleState* state = _state;
        delete this;
        state->destroyed.fetch_add(1);
    }

private:
    SampleState* _state;
};

static SampleState* g_exit_sample = nullptr;
static std::atomic<bool> g_exiting{false};
static int g_expected_requests = 0;

void VerifyExit() {
    Require(g_exit_sample->destroyed.load() == 1,
            "exit did not wait for the RPC dump");
    SampleState late;
    (new Request(&late))->submit();
    Require(late.destroyed.load() == 1 && late.entered.load() == 0,
            "exit allowed a late RPC dump");
    brpc::SampleIterator it(getenv(kDirectoryEnv));
    for (int i = 0; i < g_expected_requests; ++i) {
        std::unique_ptr<brpc::SampledRequest> request(it.Next());
        Require(request &&
                request->request.to_string() == "collector shutdown request",
                "in-flight RPC dump was not persisted before exit");
    }
    std::unique_ptr<brpc::SampledRequest> extra(it.Next());
    Require(!extra, "unexpected extra RPC dump");
}

void RunRpcDumpExit(bool prime_context) {
    alarm(15);
    const char* directory = getenv(kDirectoryEnv);
    Require(directory != nullptr, "missing dump directory");
    brpc::FLAGS_rpc_dump_dir = directory;
    brpc::FLAGS_rpc_dump_max_requests_in_one_file = 1;
    // Registered before Collector creation: its exit hook must finish first.
    Require(atexit(VerifyExit) == 0, "cannot register exit verification");
    if (prime_context) {
        SampleState prime;
        (new Request(&prime))->submit();
        Require(WaitUntil([&] { return prime.destroyed.load() == 1; }),
                "initial RPC dump did not finish");
    }
    g_expected_requests = prime_context ? 2 : 1;
    g_exit_sample = new SampleState;
    g_exit_sample->release.store(false);
    (new Request(g_exit_sample))->submit();
    Require(WaitUntil([] { return g_exit_sample->entered.load() == 1; }),
            "RPC dump did not start");
    std::thread([] {
        Require(WaitUntil([] { return g_exiting.load(); }), "exit did not start");
        usleep(100000);
        g_exit_sample->release.store(true);
    }).detach();
    Require(atexit([] { g_exiting.store(true); }) == 0,
            "cannot register exit notification");
    std::exit(0);
}

TEST_F(CollectorShutdownDeathTest, process_exit_waits_for_first_rpc_dump) {
    EXPECT_EXIT(RunRpcDumpExit(false), ::testing::ExitedWithCode(0), "");
}

TEST_F(CollectorShutdownDeathTest, process_exit_waits_for_new_rpc_dump_round) {
    EXPECT_EXIT(RunRpcDumpExit(true), ::testing::ExitedWithCode(0), "");
}

}  // namespace
