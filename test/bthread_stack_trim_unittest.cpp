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

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/stack.h"
#include "bthread/task_group.h"
#include "butil/build_config.h"
#include "butil/threading/platform_thread.h"
#include "bvar/variable.h"

DECLARE_int32(bthread_stack_trim_idle_s);
DECLARE_int32(bthread_stack_trim_batch_size);
DECLARE_int32(stack_size_small);
DECLARE_int32(stack_size_normal);
DECLARE_int32(stack_size_large);

#if defined(BTHREAD_STACK_TRIM_SUPPORTED)
namespace bthread {
extern int (*g_stack_madvise)(void*, size_t, int);
namespace internal {
size_t trim_idle_stacks_for_test(int64_t idle_us);
}
}
#endif

namespace {
#if defined(BTHREAD_STACK_TRIM_SUPPORTED)

typedef bthread::StackFactory<bthread::NormalStackClass> NormalFactory;

int64_t counter(const char* name) {
    return strtoll(bvar::Variable::describe_exposed(name).c_str(), nullptr, 10);
}

void sleep_ms(int ms) {
    butil::PlatformThread::Sleep(butil::TimeDelta::FromMilliseconds(ms));
}

void trim_all() {
    while (bthread::internal::trim_idle_stacks_for_test(0) != 0) {}
}

int64_t process_rss_kb() {
    FILE* file = fopen("/proc/self/smaps_rollup", "r");
    if (file == nullptr) { file = fopen("/proc/self/smaps", "r"); }
    if (file == nullptr) {
        ADD_FAILURE() << "Cannot read process RSS: " << strerror(errno);
        return -1;
    }
    char line[256];
    long long rss = -1;
    long long value;
    while (fgets(line, sizeof(line), file) != nullptr) {
        if (sscanf(line, "Rss: %lld kB", &value) == 1) {
            if (rss < 0) { rss = 0; }
            rss += value;
        }
    }
    fclose(file);
    EXPECT_GE(rss, 0);
    return rss;
}

// Excludes batches held by the trimmer. Use only after synchronous trimming.
template <typename StackClass>
size_t untrimmed_global_stacks() {
    typedef typename bthread::StackFactory<StackClass>::Wrapper Wrapper;
    auto* pool = butil::ObjectPool<Wrapper>::singleton();
    BAIDU_SCOPED_LOCK(pool->_free_chunks_mutex);
    size_t n = 0;
    for (auto* chunk : pool->_free_chunks) {
        for (size_t i = 0; i < chunk->nfree; ++i) {
            n += chunk->ptrs[i]->context != nullptr;
        }
    }
    return n;
}

size_t resident_pages(bthread::ContextualStack* s) {
    std::vector<unsigned char> pages(s->storage.stacksize / getpagesize());
    if (mincore(static_cast<char*>(s->storage.bottom) - s->storage.stacksize,
                s->storage.stacksize, pages.data()) != 0) {
        ADD_FAILURE() << "mincore failed: " << strerror(errno);
        return SIZE_MAX;
    }
    return std::count_if(pages.begin(), pages.end(),
                         [](unsigned char p) { return (p & 1) != 0; });
}

void flush_stacks(bthread::StackType type = bthread::STACK_TYPE_NORMAL) {
    switch (type) {
    case bthread::STACK_TYPE_SMALL:
        butil::clear_objects<bthread::StackFactory<bthread::SmallStackClass>::Wrapper>();
        break;
    case bthread::STACK_TYPE_LARGE:
        butil::clear_objects<bthread::StackFactory<bthread::LargeStackClass>::Wrapper>();
        break;
    default:
        butil::clear_objects<NormalFactory::Wrapper>();
        break;
    }
}

std::vector<bthread::ContextualStack*> cache_stacks(
    size_t n, bthread::StackType type = bthread::STACK_TYPE_NORMAL) {
    std::vector<bthread::ContextualStack*> stacks;
    for (size_t i = 0; i < n; ++i) {
        auto* s = bthread::get_stack(type, bthread::TaskGroup::task_runner);
        EXPECT_NE(nullptr, s);
        if (!s) { break; }
        // Touch several pages; merely reserving an mmap does not consume RSS.
        memset(static_cast<char*>(s->storage.bottom) - 16384, 0x5a, 8192);
        stacks.push_back(s);
    }
    for (auto* s : stacks) { bthread::return_stack(s); }
    flush_stacks(type);
    return stacks;
}

struct Burst {
    int depth;
    std::atomic<int> ready{0};
    std::atomic<int> errors{0};
    butil::atomic<int>* gate = bthread::butex_create_checked<butil::atomic<int>>();
    explicit Burst(int stack_depth) : depth(stack_depth) { gate->store(0); }
    ~Burst() { bthread::butex_destroy(gate); }
};

NOINLINE void touch_stack(Burst* burst, int depth) {
    volatile char page[4096];
    page[0] = static_cast<char>(depth);
    if (depth != 0) {
        touch_stack(burst, depth - 1);
    } else {
        ++burst->ready;
        while (burst->gate->load() == 0) {
            bthread::butex_wait(burst->gate, 0, nullptr);
        }
        bthread_usleep(1000);
        bthread_yield();
    }
    if (page[0] != depth) { ++burst->errors; }
}

bool run_burst(size_t n, int64_t* ready_rss_kb = nullptr,
               bthread_attr_t attr = BTHREAD_ATTR_NORMAL, int depth = 16) {
    Burst burst(depth);
    std::vector<bthread_t> tids;
    for (size_t i = 0; i < n; ++i) {
        bthread_t tid;
        if (bthread_start_background(&tid, &attr,
                [](void* p) -> void* {
                    auto* burst = static_cast<Burst*>(p);
                    touch_stack(burst, burst->depth);
                    return nullptr;
                }, &burst) != 0) {
            break;
        }
        tids.push_back(tid);
    }
    int64_t deadline = butil::monotonic_time_us() + 10000000;
    while (burst.ready.load() != static_cast<int>(tids.size()) &&
           butil::monotonic_time_us() < deadline) {
        sleep_ms(1);
    }
    bool ready = burst.ready.load() == static_cast<int>(n);
    if (ready_rss_kb != nullptr) { *ready_rss_kb = process_rss_kb(); }
    burst.gate->store(1);
    bthread::butex_wake_all(burst.gate);
    for (bthread_t tid : tids) { bthread_join(tid, nullptr); }
    return ready && burst.errors.load() == 0;
}

struct MadviseOverride {
    explicit MadviseOverride(int (*fn)(void*, size_t, int))
        : old(bthread::g_stack_madvise) { bthread::g_stack_madvise = fn; }
    ~MadviseOverride() { bthread::g_stack_madvise = old; }
    int (*old)(void*, size_t, int);
};

class StackTrimTest : public ::testing::Test {
protected:
    void SetUp() override {
        FLAGS_bthread_stack_trim_idle_s = 0;
        flush_stacks();
        trim_all();
    }
    GFLAGS_NAMESPACE::FlagSaver flags;
};

// Re-exec isolates tests that enable background reclamation.
class StackTrimDeathTest : public StackTrimTest {
    void SetUp() override {
        StackTrimTest::SetUp();
#ifdef GTEST_FLAG_SET
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#else
        ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
#endif
        // Otherwise a failed CHECK only logs and the child still exits with 0.
        ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
            "crash_on_fatal_log", "true").empty());
    }
};

TEST_F(StackTrimTest, trim_and_rebuild_all_stack_types) {
    for (auto type : {bthread::STACK_TYPE_SMALL, bthread::STACK_TYPE_NORMAL,
                      bthread::STACK_TYPE_LARGE}) {
        auto stacks = cache_stacks(80, type);
        ASSERT_EQ(80u, stacks.size());
        std::vector<bthread::StackStorage> storage;
        for (auto* s : stacks) {
            ASSERT_GT(resident_pages(s), 0u);
            storage.push_back(s->storage);
        }
        int64_t before = counter("bthread_stack_count");
        int64_t released_before = counter("bthread_stack_trimmed_count");
        trim_all();
        EXPECT_EQ(before, counter("bthread_stack_count"));
        EXPECT_EQ(released_before + 80, counter("bthread_stack_trimmed_count"));
        for (size_t i = 0; i < stacks.size(); ++i) {
            auto* s = stacks[i];
            EXPECT_EQ(nullptr, s->context);
            EXPECT_EQ(storage[i].bottom, s->storage.bottom);
            EXPECT_EQ(storage[i].stacksize, s->storage.stacksize);
            EXPECT_EQ(storage[i].guardsize, s->storage.guardsize);
            EXPECT_EQ(storage[i].valgrind_stack_id, s->storage.valgrind_stack_id);
            EXPECT_EQ(0u, resident_pages(s));
        }
        // Rebuilding only touches the existing mapping, including its guard.
        std::vector<bthread::ContextualStack*> reused;
        for (int i = 0; i < 80; ++i) {
            auto* s = bthread::get_stack(type, bthread::TaskGroup::task_runner);
            ASSERT_NE(nullptr, s);
            EXPECT_NE(nullptr, s->context);
            EXPECT_GT(s->storage.guardsize, 0u);
            EXPECT_EQ(type, s->stacktype);
            auto found = std::find(stacks.begin(), stacks.end(), s);
            ASSERT_NE(stacks.end(), found);
            EXPECT_EQ(storage[found - stacks.begin()].bottom, s->storage.bottom);
            reused.push_back(s);
        }
        EXPECT_EQ(before, counter("bthread_stack_count"));
        for (auto* s : reused) { bthread::return_stack(s); }
        flush_stacks(type);
        trim_all();
    }
}

TEST_F(StackTrimTest, batch_size_updates_on_next_batch) {
    // LARGE stacks have one object per chunk, so the budget is exact here.
    ASSERT_EQ(256u, cache_stacks(256, bthread::STACK_TYPE_LARGE).size());
    int64_t before = counter("bthread_stack_trimmed_count");
    ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_batch_size", "1").empty());
    EXPECT_EQ(1u, bthread::internal::trim_idle_stacks_for_test(0));
    EXPECT_EQ(before + 1, counter("bthread_stack_trimmed_count"));
    ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_batch_size", "128").empty());
    EXPECT_EQ(128u, bthread::internal::trim_idle_stacks_for_test(0));
    EXPECT_EQ(before + 129, counter("bthread_stack_trimmed_count"));
    ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_batch_size", "256").empty());
    EXPECT_EQ(127u, bthread::internal::trim_idle_stacks_for_test(0));
    EXPECT_EQ(before + 256, counter("bthread_stack_trimmed_count"));
    EXPECT_EQ(0u, bthread::internal::trim_idle_stacks_for_test(0));
}

TEST_F(StackTrimDeathTest, malloc_stacks_are_not_trimmed) {
    ASSERT_EXIT({
        FLAGS_guard_page_size = 0;
        auto stacks = cache_stacks(80);
        CHECK_EQ(80u, stacks.size());
        std::vector<bthread_fcontext_t> contexts;
        for (auto* s : stacks) { contexts.push_back(s->context); }
        int64_t before = counter("bthread_stack_count");
        trim_all();
        CHECK_EQ(before, counter("bthread_stack_count"));
        CHECK_EQ(0, counter("bthread_stack_trimmed_count"));
        for (size_t i = 0; i < stacks.size(); ++i) {
            CHECK_EQ(0u, stacks[i]->storage.guardsize);
            CHECK_EQ(contexts[i], stacks[i]->context);
            CHECK_EQ(0x5a, *(static_cast<unsigned char*>(
                stacks[i]->storage.bottom) - 16384));
        }
        _exit(0);
    }, ::testing::ExitedWithCode(0), "");
}

void check_failed_trim(bool partial) {
    auto stacks = cache_stacks(256);
    std::vector<bthread::StackStorage> storage;
    std::vector<size_t> resident;
    for (auto* s : stacks) {
        storage.push_back(s->storage);
        resident.push_back(resident_pages(s));
    }
    int64_t before = counter("bthread_stack_count");
    int64_t released_before = counter("bthread_stack_trimmed_count");
    {
        auto fail = [](void*, size_t, int) { errno = EINVAL; return -1; };
        auto partial_fail = [](void* low, size_t size, int advice) {
            // Discard the page containing the saved context, then report an
            // error. An error alone cannot prove that the old context is safe.
            size_t page = getpagesize();
            if (madvise(static_cast<char*>(low) + size - page, page, advice) != 0) {
                return -1;
            }
            errno = EINVAL;
            return -1;
        };
        MadviseOverride hook(partial ? +partial_fail : +fail);
        trim_all();
    }
    EXPECT_EQ(before, counter("bthread_stack_count"));
    EXPECT_EQ(released_before, counter("bthread_stack_trimmed_count"));
    for (size_t i = 0; i < stacks.size(); ++i) {
        EXPECT_EQ(nullptr, stacks[i]->context);
        EXPECT_EQ(storage[i].bottom, stacks[i]->storage.bottom);
        EXPECT_EQ(storage[i].stacksize, stacks[i]->storage.stacksize);
        EXPECT_EQ(storage[i].guardsize, stacks[i]->storage.guardsize);
        EXPECT_EQ(storage[i].valgrind_stack_id, stacks[i]->storage.valgrind_stack_id);
        EXPECT_EQ(resident[i] - (partial ? 1 : 0), resident_pages(stacks[i]));
    }
    EXPECT_EQ(0u, bthread::internal::trim_idle_stacks_for_test(0));
    ASSERT_EQ(0, bthread_setconcurrency(8));
    ASSERT_TRUE(run_burst(256));  // Must rebuild even when madvise failed.
    int64_t deadline = butil::monotonic_time_us() + 2000000;
    do {
        trim_all();
        if (counter("bthread_stack_trimmed_count") > released_before) { break; }
        sleep_ms(1);
    } while (butil::monotonic_time_us() < deadline);
    EXPECT_GT(counter("bthread_stack_trimmed_count"), released_before);
}

TEST_F(StackTrimTest, failed_trim_rebuilds_context_and_retries_after_reuse) {
    check_failed_trim(false);
}

TEST_F(StackTrimTest, partial_failure_rebuilds_context_and_retries_after_reuse) {
    check_failed_trim(true);
}

TEST_F(StackTrimTest, burst_reclaim_and_second_burst) {
    const char* stress_count = getenv("BRPC_STACK_TRIM_TEST_BURST");
    size_t n = stress_count ? strtoul(stress_count, nullptr, 10) : 2048;
    const char* stress_workers = getenv("BRPC_STACK_TRIM_TEST_WORKERS");
    int workers = stress_workers ? strtol(stress_workers, nullptr, 10) : 8;
    const char* stress_type = getenv("BRPC_STACK_TRIM_TEST_TYPE");
    std::string type = stress_type ? stress_type : "normal";
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    int depth = 16;
    int stack_size = FLAGS_stack_size_normal;
    int cached_per_worker = FLAGS_tc_stack_normal;
    auto count_untrimmed = untrimmed_global_stacks<bthread::NormalStackClass>;
    if (type == "small") {
        attr = BTHREAD_ATTR_SMALL;
        depth = 3;
        stack_size = FLAGS_stack_size_small;
        cached_per_worker = FLAGS_tc_stack_small;
        count_untrimmed = untrimmed_global_stacks<bthread::SmallStackClass>;
    } else if (type == "large") {
        attr = BTHREAD_ATTR_LARGE;
        depth = 64;
        stack_size = FLAGS_stack_size_large;
        cached_per_worker = 1;
        count_untrimmed = untrimmed_global_stacks<bthread::LargeStackClass>;
    } else {
        ASSERT_EQ("normal", type);
    }
    ASSERT_GE(n, 256u);
    ASSERT_EQ(0, bthread_setconcurrency(workers));
    int64_t released_before = counter("bthread_stack_trimmed_count");
    int64_t baseline_rss_kb = process_rss_kb();
    int64_t ready_rss_kb = 0;
    ASSERT_TRUE(run_burst(n, &ready_rss_kb, attr, depth));
    ASSERT_EQ(workers, counter("bthread_worker_count"));
    int64_t peak = counter("bthread_stack_count");
    EXPECT_GE(peak, static_cast<int64_t>(n));
    int64_t before_trim_rss_kb = process_rss_kb();
    int64_t trim_start_us = butil::monotonic_time_us();
    int64_t deadline = trim_start_us + 3000000;
    do {
        trim_all();
        if (counter("bthread_stack_trimmed_count") - released_before >=
                static_cast<int64_t>(n) - workers * cached_per_worker &&
            count_untrimmed() == 0) { break; }
        sleep_ms(1);
    } while (butil::monotonic_time_us() < deadline);
    EXPECT_GE(counter("bthread_stack_trimmed_count") - released_before,
              static_cast<int64_t>(n) - workers * cached_per_worker);
    EXPECT_EQ(0u, count_untrimmed());
    EXPECT_EQ(peak, counter("bthread_stack_count"));
    int64_t trim_elapsed_us = butil::monotonic_time_us() - trim_start_us;
    int64_t after_trim_rss_kb = process_rss_kb();
    RecordProperty("stack_type", type);
    RecordProperty("worker_count", std::to_string(workers));
    RecordProperty("stack_size_kb", std::to_string(stack_size / 1024));
    RecordProperty("touched_stack_kb", std::to_string((depth + 1) * 4));
    RecordProperty("peak_stacks", std::to_string(peak));
    RecordProperty("trimmed_stacks", std::to_string(
        counter("bthread_stack_trimmed_count") - released_before));
    RecordProperty("baseline_rss_kb", std::to_string(baseline_rss_kb));
    RecordProperty("ready_rss_kb", std::to_string(ready_rss_kb));
    RecordProperty("before_trim_rss_kb", std::to_string(before_trim_rss_kb));
    RecordProperty("after_trim_rss_kb", std::to_string(after_trim_rss_kb));
    RecordProperty("trim_elapsed_us", std::to_string(trim_elapsed_us));
    ASSERT_TRUE(run_burst(n, nullptr, attr, depth));
    EXPECT_GE(counter("bthread_stack_count"), static_cast<int64_t>(n));
}

TEST_F(StackTrimTest, reclaim_while_a_second_burst_is_running) {
    cache_stacks(2048);
    ASSERT_EQ(0, bthread_setconcurrency(8));
    std::atomic<bool> stop(false);
    std::thread trimmer([&] {
        while (!stop.load()) {
            bthread::internal::trim_idle_stacks_for_test(0);
            std::this_thread::yield();
        }
    });
    bool ok = run_burst(2048);
    stop = true;
    trimmer.join();
    ASSERT_TRUE(ok);
}

TEST_F(StackTrimDeathTest, rebuilt_stack_guard_is_protected) {
    ASSERT_EXIT({
        auto stacks = cache_stacks(1);
        CHECK_EQ(1u, stacks.size());
        trim_all();
        CHECK(stacks[0]->context == nullptr);
        auto* s = NormalFactory::get_stack(bthread::TaskGroup::task_runner);
        CHECK(s && s->storage.guardsize != 0);
        *static_cast<volatile char*>(
            static_cast<void*>(static_cast<char*>(s->storage.bottom) -
                               s->storage.stacksize - 1)) = 1;
        _exit(0);
    }, ::testing::KilledBySignal(SIGSEGV), "");
}

TEST_F(StackTrimTest, destructor_releases_mapping_without_context) {
    int64_t before = counter("bthread_stack_count");
    {
        NormalFactory::Wrapper stack(bthread::TaskGroup::task_runner);
        ASSERT_NE(nullptr, stack.context);
        ASSERT_EQ(0, madvise(static_cast<char*>(stack.storage.bottom) -
                             stack.storage.stacksize,
                             stack.storage.stacksize, MADV_DONTNEED));
        stack.context = nullptr;
        EXPECT_EQ(before + 1, counter("bthread_stack_count"));
    }
    EXPECT_EQ(before, counter("bthread_stack_count"));
}

void check_background(bool runtime_enable) {
    // This thread keeps exactly one warm stack in TLS. A producer puts a known
    // number of other stacks in the global pool; no bthread borrows them.
    auto* warm = NormalFactory::get_stack(bthread::TaskGroup::task_runner);
    CHECK(warm != nullptr);
    bthread_fcontext_t warm_context = warm->context;
    NormalFactory::return_stack(warm);
    if (!runtime_enable) {
        CHECK(!GFLAGS_NAMESPACE::SetCommandLineOption(
            "bthread_stack_trim_idle_s", "1").empty());
    }
    std::thread producer([] { CHECK_EQ(256u, cache_stacks(256).size()); });
    producer.join();
    int64_t before = counter("bthread_stack_count");
    sleep_ms(1200);
    CHECK_EQ(0, counter("bthread_stack_trimmed_count"));  // Not initialized yet.

    CHECK_EQ(0, bthread_setconcurrency(8));
    bthread_t tid;
    CHECK_EQ(0, bthread_start_background(&tid, &BTHREAD_ATTR_PTHREAD,
        [](void*) -> void* { return nullptr; }, nullptr));
    CHECK_EQ(0, bthread_join(tid, nullptr));
    if (runtime_enable) {
        sleep_ms(1200);
        CHECK_EQ(before, counter("bthread_stack_count"));
        CHECK_EQ(0, counter("bthread_stack_trimmed_count"));
        // No validator callback: the thread must already exist after init.
        FLAGS_bthread_stack_trim_idle_s = 1;
    }
    int64_t deadline = butil::monotonic_time_us() + 10000000;
    // The counter is published after a batch completes. Waiting for all 256
    // stacks also accounts for batches already removed from the global list.
    while (counter("bthread_stack_trimmed_count") < 256 &&
           butil::monotonic_time_us() < deadline) {
        sleep_ms(20);
    }
    CHECK_EQ(256, counter("bthread_stack_trimmed_count"));
    CHECK_EQ(before, counter("bthread_stack_count"));
    // Repeated TLS use stays warm across several idle periods.
    for (int i = 0; i < 120; ++i) {
        auto* s = NormalFactory::get_stack(bthread::TaskGroup::task_runner);
        CHECK_EQ(warm, s);
        CHECK_EQ(warm_context, s->context);
        NormalFactory::return_stack(s);
        sleep_ms(20);
    }
    CHECK_EQ(256, counter("bthread_stack_trimmed_count"));
}

TEST_F(StackTrimDeathTest, startup_enable_waits_for_bthread_initialization) {
    ASSERT_EXIT({ check_background(false); _exit(0); },
                ::testing::ExitedWithCode(0), "");
}

TEST_F(StackTrimDeathTest, runtime_enable_uses_existing_thread) {
    ASSERT_EXIT({ check_background(true); _exit(0); },
                ::testing::ExitedWithCode(0), "");
}

TEST_F(StackTrimTest, reject_negative_idle_timeout) {
    EXPECT_TRUE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_idle_s", "-1").empty());
}

TEST_F(StackTrimTest, reject_nonpositive_batch_size) {
    int before = FLAGS_bthread_stack_trim_batch_size;
    EXPECT_TRUE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_batch_size", "0").empty());
    EXPECT_TRUE(GFLAGS_NAMESPACE::SetCommandLineOption(
        "bthread_stack_trim_batch_size", "-1").empty());
    EXPECT_EQ(before, FLAGS_bthread_stack_trim_batch_size);
}

#endif // BTHREAD_STACK_TRIM_SUPPORTED
}  // namespace
