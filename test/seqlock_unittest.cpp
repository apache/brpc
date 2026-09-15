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

#include <unistd.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <mutex>
#include <cstdio>
#include "butil/atomicops.h"
#include "butil/time.h"
#include "butil/synchronization/lock.h"
#include "butil/synchronization/seqlock.h"

namespace {

// A multi-word payload. The seqlock's job is to make every reader observe a
// snapshot in which all words are equal; a torn read would see a mix of an old
// and a new value. Every field is a relaxed atomic, as Seqlock requires.
static const int kWords = 8;
struct Payload {
    butil::atomic<uint64_t> w[kWords];

    void relaxed_set(uint64_t v) {
        for (auto& i : w) {
            // Store word by word (not as one atomic group) so that, without the
            // seqlock, a concurrent reader could observe a torn value.
            i.store(v, butil::memory_order_relaxed);
        }
    }
    // Returns the first word and whether all words are equal to it.
    uint64_t relaxed_get(bool* consistent) const {
        uint64_t v0 = w[0].load(butil::memory_order_relaxed);
        *consistent = true;
        for (int i = 1; i < kWords; ++i) {
            if (w[i].load(butil::memory_order_relaxed) != v0) {
                *consistent = false;
            }
        }
        return v0;
    }
};

TEST(SeqlockTest, SingleThreadedReadWrite) {
    butil::Seqlock<> seqlock;
    Payload payload;
    payload.relaxed_set(0);

    for (uint64_t v = 1; v <= 1000; ++v) {
        seqlock.store([&] { payload.relaxed_set(v); });
        uint64_t got = seqlock.load([&] {
            bool consistent = false;
            uint64_t r = payload.relaxed_get(&consistent);
            EXPECT_TRUE(consistent);
            return r;
        });
        ASSERT_EQ(v, got);
    }
}

TEST(SeqlockTest, LoadReturnsValueByType) {
    butil::Seqlock<> seqlock;
    butil::atomic<int> payload(0);
    seqlock.store([&] {
        payload.store(42, butil::memory_order_relaxed);
    });
    // load returns whatever the callback returns, by value.
    int v = seqlock.load([&] {
        return payload.load(butil::memory_order_relaxed);
    });
    ASSERT_EQ(42, v);
    // A different return type also works.
    std::pair<int, int> pr = seqlock.load([&] {
        int v = payload.load(butil::memory_order_relaxed);
        return std::make_pair(v, v + 1);
    });
    ASSERT_EQ(42, pr.first);
    ASSERT_EQ(43, pr.second);
}

TEST(SeqlockTest, MutexSpecializationSingleThreaded) {
    butil::Seqlock<butil::Mutex> seqlock;
    Payload payload;
    payload.relaxed_set(0);
    for (uint64_t v = 1; v <= 1000; ++v) {
        seqlock.store([&] { payload.relaxed_set(v); });
        bool consistent = false;
        uint64_t got = seqlock.load([&] {
            bool c = false;
            uint64_t r = payload.relaxed_get(&c);
            consistent = c;
            return r;
        });
        ASSERT_TRUE(consistent);
        ASSERT_EQ(v, got);
    }
}

// Single-threaded performance comparison: Seqlock vs Mutex.
//
// There is no contention here, so this measures the bare cost of the
// synchronization itself:
//   * Seqlock<>       : one relaxed counter load, two counter stores and a
//                       release fence on the write path; two counter loads and
//                       an acquire fence on the read path (no atomic RMW in
//                       either path).
//   * Seqlock<Mutex>  : the same counter work plus a lock/unlock pair on the
//                       write path; the read path is identical to Seqlock<>.
//   * Mutex           : a lock/unlock pair (atomic RMW + potential syscall on
//                       contention) on BOTH the write and the read path.
//
// Timings are reported, not asserted: absolute numbers depend on the machine,
// the compiler and the current load, so turning them into thresholds would
// make the test flaky. The test only asserts that the measurements ran and
// produced the expected values.
static const int kPerfRounds = 1000000;

// Accumulates every value read so the compiler cannot optimize the read loops
// away as dead code.
static butil::atomic<uint64_t> g_perf_sink(0);

inline void perf_consume(uint64_t v) {
    g_perf_sink.fetch_add(v, butil::memory_order_relaxed);
}

// A pure compiler barrier: it emits no instruction, but forbids the compiler
// from moving memory accesses across it. Without one between the rounds below,
// a write body made purely of relaxed stores is legally collapsible into its
// last iteration -- which is exactly what happens to Seqlock<> once its counter
// increments are plain stores rather than fetch_add, and it measures 0 ns/op.
// The mutex bodies resist that on their own; barriering all of them keeps the
// comparison between the three honest.
inline void perf_barrier() {
    butil::atomic_signal_fence(butil::memory_order_seq_cst);
}

// Runs `body(round)` for `rounds` rounds and returns the elapsed nanoseconds.
// A short warmup runs first so that page faults, branch predictor and cache
// warmup are not charged to the measured loop.
template <typename Body>
int64_t TimeLoopNs(int rounds, Body&& body) {
    int kWarmup = 1000;
    for (int i = 1; i <= kWarmup; ++i) {
        body(static_cast<uint64_t>(i));
        perf_barrier();
    }
    int64_t start_ns = butil::cpuwide_time_ns();
    for (int i = 1; i <= rounds; ++i) {
        body(static_cast<uint64_t>(i));
        perf_barrier();
    }
    return butil::cpuwide_time_ns() - start_ns;
}

inline double ns_per_op(int64_t elapsed_ns, int rounds) {
    return static_cast<double>(elapsed_ns) / rounds;
}

TEST(SeqlockTest, SingleThreadedPerfVsMutex) {
    // Seqlock<>: single-writer, no mutex.
    butil::Seqlock<> seqlock;
    Payload seqlock_payload;
    seqlock_payload.relaxed_set(0);
    int64_t seqlock_write_ns = TimeLoopNs(kPerfRounds, [&](uint64_t v) {
        seqlock.store([&] { seqlock_payload.relaxed_set(v); });
        // Make both the counter and payload observable after every write so
        // the compiler cannot eliminate stores to these local test objects.
        asm volatile("" : : "m"(seqlock), "m"(seqlock_payload) : "memory");
    });
    int64_t seqlock_read_ns = TimeLoopNs(kPerfRounds, [&](uint64_t) {
        perf_consume(seqlock.load([&] {
            bool consistent = false;
            return seqlock_payload.relaxed_get(&consistent);
        }));
    });

    // Seqlock<Mutex>: same read path, writes additionally take a mutex.
    butil::Seqlock<butil::Mutex> mutex_seqlock;
    Payload mutex_seqlock_payload;
    mutex_seqlock_payload.relaxed_set(0);
    int64_t mutex_seqlock_write_ns = TimeLoopNs(kPerfRounds, [&](uint64_t v) {
        mutex_seqlock.store([&] { mutex_seqlock_payload.relaxed_set(v); });
        asm volatile("" : : "m"(mutex_seqlock), "m"(mutex_seqlock_payload)
                     : "memory");
    });
    int64_t mutex_seqlock_read_ns = TimeLoopNs(kPerfRounds, [&](uint64_t) {
        perf_consume(mutex_seqlock.load([&] {
            bool consistent = false;
            return mutex_seqlock_payload.relaxed_get(&consistent);
        }));
    });

    // Plain Mutex baseline: both sides take the lock.
    butil::Mutex mutex;
    Payload mutex_payload;
    mutex_payload.relaxed_set(0);
    int64_t mutex_write_ns = TimeLoopNs(kPerfRounds, [&](uint64_t v) {
        {
            std::lock_guard<butil::Mutex> lk(mutex);
            mutex_payload.relaxed_set(v);
        }
        asm volatile("" : : "m"(mutex), "m"(mutex_payload) : "memory");
    });
    int64_t mutex_read_ns = TimeLoopNs(kPerfRounds, [&](uint64_t) {
        std::lock_guard<butil::Mutex> lk(mutex);
        bool consistent = false;
        perf_consume(mutex_payload.relaxed_get(&consistent));
    });

    printf("\n[seqlock perf] single thread, %d rounds, payload=%d words\n",
           kPerfRounds, kWords);
    printf("%-18s %14s %14s\n", "impl", "write(ns/op)", "read(ns/op)");
    printf("%-18s %14.2f %14.2f\n", "Seqlock<>",
           ns_per_op(seqlock_write_ns, kPerfRounds),
           ns_per_op(seqlock_read_ns, kPerfRounds));
    printf("%-18s %14.2f %14.2f\n", "Seqlock<Mutex>",
           ns_per_op(mutex_seqlock_write_ns, kPerfRounds),
           ns_per_op(mutex_seqlock_read_ns, kPerfRounds));
    printf("%-18s %14.2f %14.2f\n", "Mutex",
           ns_per_op(mutex_write_ns, kPerfRounds),
           ns_per_op(mutex_read_ns, kPerfRounds));
    printf("%-18s %13.2fx %13.2fx\n", "Mutex/Seqlock<>",
           static_cast<double>(mutex_write_ns) / seqlock_write_ns,
           static_cast<double>(mutex_read_ns) / seqlock_read_ns);

    // The loops really ran and every implementation published the last value.
    ASSERT_GT(seqlock_write_ns, 0);
    ASSERT_GT(mutex_seqlock_write_ns, 0);
    ASSERT_GT(mutex_write_ns, 0);
    ASSERT_GT(seqlock_read_ns, 0);
    ASSERT_GT(mutex_seqlock_read_ns, 0);
    ASSERT_GT(mutex_read_ns, 0);
    ASSERT_GT(g_perf_sink.load(butil::memory_order_relaxed), 0u);

    bool consistent = false;
    ASSERT_EQ(static_cast<uint64_t>(kPerfRounds),
              seqlock_payload.relaxed_get(&consistent));
    ASSERT_TRUE(consistent);
    ASSERT_EQ(static_cast<uint64_t>(kPerfRounds),
              mutex_seqlock_payload.relaxed_get(&consistent));
    ASSERT_TRUE(consistent);
    ASSERT_EQ(static_cast<uint64_t>(kPerfRounds),
              mutex_payload.relaxed_get(&consistent));
    ASSERT_TRUE(consistent);
}

// Concurrent consistency tests
struct SharedState {
    butil::Seqlock<>* single_writer_seqlock = nullptr; // single-writer lock
    butil::Seqlock<butil::Mutex>* multi_writer_seqlock = nullptr; // multi-writer lock
    Payload payload;
    butil::atomic<bool> stopped{false};
    butil::atomic<uint64_t> version{0}; // source of the value written
    butil::atomic<uint64_t> reads{0}; // total reads performed
    butil::atomic<uint64_t> torn{0}; // inconsistent snapshots observed
};

void* SingleWriterThread(void* arg) {
    SharedState* shared_state = static_cast<SharedState*>(arg);
    while (!shared_state->stopped.load(butil::memory_order_relaxed)) {
        uint64_t version = shared_state->version.fetch_add(1, butil::memory_order_relaxed) + 1;
        shared_state->single_writer_seqlock->store([&] {
            shared_state->payload.relaxed_set(version);
        });
    }
    return nullptr;
}

void* MultiWriterThread(void* arg) {
    SharedState* shared_state = static_cast<SharedState*>(arg);
    while (!shared_state->stopped.load(butil::memory_order_relaxed)) {
        uint64_t version = shared_state->version.fetch_add(1, butil::memory_order_relaxed) + 1;
        shared_state->multi_writer_seqlock->store([&] {
            shared_state->payload.relaxed_set(version);
        });
    }
    return nullptr;
}

struct ReaderArg {
    SharedState* shared_state;
    bool multi_writer;
};

void* ReaderThread(void* arg) {
    ReaderArg* reader_arg = static_cast<ReaderArg*>(arg);
    SharedState* shared_state = reader_arg->shared_state;
    uint64_t local_reads = 0;
    uint64_t local_torn = 0;
    while (!shared_state->stopped.load(butil::memory_order_relaxed)) {
        bool consistent = false;
        auto load_body = [&] {
            bool c = false;
            uint64_t r = shared_state->payload.relaxed_get(&c);
            consistent = c;
            return r;
        };
        if (reader_arg->multi_writer) {
            shared_state->multi_writer_seqlock->load(load_body);
        } else {
            shared_state->single_writer_seqlock->load(load_body);
        }
        ++local_reads;
        if (!consistent) {
            ++local_torn;
        }
    }
    shared_state->reads.fetch_add(local_reads, butil::memory_order_relaxed);
    shared_state->torn.fetch_add(local_torn, butil::memory_order_relaxed);
    return nullptr;
}

TEST(SeqlockTest, SingleWriterManyReaders) {
    SharedState shared_state;
    butil::Seqlock<> sl;
    shared_state.single_writer_seqlock = &sl;
    shared_state.payload.relaxed_set(0);

    const int kReaders = 4;
    pthread_t writer;
    pthread_t readers[kReaders];
    ReaderArg args[kReaders];

    ASSERT_EQ(0, pthread_create(&writer, nullptr, SingleWriterThread, &shared_state));
    for (int i = 0; i < kReaders; ++i) {
        args[i].shared_state = &shared_state;
        args[i].multi_writer = false;
        ASSERT_EQ(0, pthread_create(&readers[i], nullptr, ReaderThread, &args[i]));
    }

    usleep(500 * 1000);  // 0.5s of hammering
    shared_state.stopped.store(true, butil::memory_order_relaxed);

    pthread_join(writer, nullptr);
    for (auto reader : readers) {
        pthread_join(reader, nullptr);
    }

    ASSERT_GT(shared_state.reads.load(), 0u);
    ASSERT_EQ(0u, shared_state.torn.load()) << "readers observed torn snapshots";
}

TEST(SeqlockTest, MultiWriterManyReaders) {
    SharedState shared_state;
    butil::Seqlock<butil::Mutex> seqlock;
    shared_state.multi_writer_seqlock = &seqlock;
    shared_state.payload.relaxed_set(0);

    const int kWriters = 3;
    const int kReaders = 4;
    pthread_t writers[kWriters];
    pthread_t readers[kReaders];
    ReaderArg args[kReaders];

    for (auto& writer : writers) {
        ASSERT_EQ(0, pthread_create(&writer, nullptr, MultiWriterThread, &shared_state));
    }
    for (int i = 0; i < kReaders; ++i) {
        args[i].shared_state = &shared_state;
        args[i].multi_writer = true;
        ASSERT_EQ(0, pthread_create(&readers[i], nullptr, ReaderThread, &args[i]));
    }

    usleep(500 * 1000);
    shared_state.stopped.store(true, butil::memory_order_relaxed);

    for (auto writer : writers) {
        pthread_join(writer, nullptr);
    }
    for (auto reader : readers) {
        pthread_join(reader, nullptr);
    }

    ASSERT_GT(shared_state.reads.load(), 0u);
    ASSERT_EQ(0u, shared_state.torn.load()) << "readers observed torn snapshots";
}

}  // namespace
