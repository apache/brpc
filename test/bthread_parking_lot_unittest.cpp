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

#include <limits.h>
#include <unistd.h>
#include <cstdlib>
#include <thread>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "bthread/parking_lot.h"

namespace {

using bthread::ParkingLot;

const int64_t kTimeoutUs = 5 * 1000 * 1000;

template <typename Predicate>
bool wait_until(Predicate predicate) {
    const int64_t deadline_us = butil::cpuwide_time_us() + kTimeoutUs;
    int spins = 0;
    while (!predicate()) {
        if (butil::cpuwide_time_us() >= deadline_us) {
            return false;
        }
        // Keep short handshakes responsive so the signaler can race with waiter
        // registration instead of usually arriving after the waiter has slept.
        if (++spins < 256) {
            std::this_thread::yield();
        } else {
            usleep(50);
        }
    }
    return true;
}

// ParkingLot blocks OS threads, so use native threads rather than bthreads.
// All assertions are made before cleanup: rescue wakeups must not make a lost
// wakeup test pass. The guard also cleans up on a fatal assertion/early return.
class ParkingLotThreads {
public:
    explicit ParkingLotThreads(ParkingLot* lot)
        : _lot(lot), _cancelled(false), _finished(0) {}

    ~ParkingLotThreads() {
        _cancelled.store(true, butil::memory_order_release);
        const bool joined = wait_until([this] {
            if (_finished.load(butil::memory_order_acquire) == _threads.size()) {
                return true;
            }
            // Do not depend on signal() or stop() working to rescue a failure.
            // Changing the value also rescues a thread not yet in futex_wait.
            _lot->_pending_signal.fetch_add(2, butil::memory_order_release);
            bthread::futex_wake_private(&_lot->_pending_signal, INT_MAX);
            return false;
        });
        if (!joined) {
            ADD_FAILURE() << "ParkingLot test threads could not be rescued";
            // Never detach threads referencing stack objects or hang the suite.
            std::abort();
        }
        for (auto& thread : _threads) {
            thread.join();
        }
    }

    template <typename Function>
    void start(Function function) {
        _threads.emplace_back([this, function] {
            function();
            _finished.fetch_add(1, butil::memory_order_release);
        });
    }

    bool cancelled() const {
        return _cancelled.load(butil::memory_order_acquire);
    }

    bool finished() const {
        return _finished.load(butil::memory_order_acquire) == _threads.size();
    }

private:
    ParkingLot* _lot;
    butil::atomic<bool> _cancelled;
    butil::atomic<size_t> _finished;
    std::vector<std::thread> _threads;
};

class ParkingLotTest : public ::testing::TestWithParam<bool> {
protected:
    void SetUp() override {
        bthread::FLAGS_parking_lot_no_signal_when_no_waiter = GetParam();
    }

    // Test builds use -fno-access-control. Inspect state and waiter bookkeeping
    // without adding test-only hooks to the production synchronization path.
    void check_wake_count(int num_waiters, int num_task, int expected_woken) {
        ParkingLot lot;
        butil::atomic<int> ready(0);
        butil::atomic<int> returned(0);
        ParkingLotThreads threads(&lot);
        for (int i = 0; i < num_waiters; ++i) {
            threads.start([&] {
                ready.fetch_add(1, butil::memory_order_release);
                while (!threads.cancelled()) {
                    const auto state = lot.get_state();
                    lot.wait(state);
                    returned.fetch_add(1, butil::memory_order_release);
                }
            });
        }
        ASSERT_TRUE(wait_until([&] { return ready.load() == num_waiters; }));
        if (GetParam()) {
            ASSERT_TRUE(wait_until([&] {
                return lot._waiter_num.load() == num_waiters;
            }));
        }

        // Neither ready nor _waiter_num proves that the kernel has enqueued a
        // waiter. Retry until the actual futex return value confirms the desired
        // batch. This avoids assuming that a sleep is a scheduling barrier.
        int nwoken = -1;
        ASSERT_TRUE(wait_until([&] {
            nwoken = lot.signal(num_task);
            return nwoken == expected_woken || nwoken < 0 ||
                   nwoken > expected_woken;
        }));
        ASSERT_EQ(expected_woken, nwoken);
        ASSERT_TRUE(wait_until([&] { return returned.load() >= nwoken; }));
    }

private:
    GFLAGS_NAMESPACE::FlagSaver _flag_saver;
};

TEST_P(ParkingLotTest, initial_state) {
    ParkingLot::State state;
    ASSERT_EQ(0, state.val);
    ASSERT_FALSE(state.stopped());

    ParkingLot lot;
    ASSERT_EQ(0, lot.get_state().val);
    ASSERT_FALSE(lot.get_state().stopped());
    ASSERT_EQ(0, lot._waiter_num.load());
    ASSERT_EQ(GetParam(), lot._no_signal_when_no_waiter);
}

TEST_P(ParkingLotTest, configuration_is_captured_at_construction) {
    ParkingLot first;
    bthread::FLAGS_parking_lot_no_signal_when_no_waiter = !GetParam();
    ParkingLot second;
    ASSERT_EQ(GetParam(), first._no_signal_when_no_waiter);
    ASSERT_EQ(!GetParam(), second._no_signal_when_no_waiter);
    ASSERT_EQ(0, first.signal(1));
    ASSERT_EQ(0, second.signal(1));
    ASSERT_EQ(2, first.get_state().val);
    ASSERT_EQ(2, second.get_state().val);
}

TEST_P(ParkingLotTest, signal_without_waiters_updates_state) {
    ParkingLot lot;
    const auto initial = lot.get_state();
    ASSERT_EQ(0, lot.signal(1));
    const auto first = lot.get_state();
    ASSERT_EQ(2, first.val);
    ASSERT_EQ(0, lot.signal(3));
    ASSERT_EQ(8, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(0));
    ASSERT_EQ(8, lot.get_state().val);
    ASSERT_FALSE(lot.get_state().stopped());
    // Snapshots are values, not references to the live signal counter.
    ASSERT_EQ(0, initial.val);
    ASSERT_EQ(2, first.val);
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, stale_state_does_not_wait_or_consume_signals) {
    ParkingLot lot;
    const auto initial = lot.get_state();
    lot.signal(1);
    const auto first = lot.get_state();
    lot.signal(2);
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        for (int i = 0; i < 100 && !threads.cancelled(); ++i) {
            lot.wait(initial);
            lot.wait(first);
        }
    });
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_EQ(6, lot.get_state().val);
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, current_state_waits_even_after_previous_signals) {
    ParkingLot lot;
    lot.signal(3);
    const auto current = lot.get_state();
    butil::atomic<bool> ready(false);
    butil::atomic<int> returned(0);
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        ready.store(true, butil::memory_order_release);
        // Allow spurious wakeups without mistaking them for a new signal.
        while (!threads.cancelled() && lot.get_state().val == current.val) {
            lot.wait(current);
        }
        returned.store(1, butil::memory_order_release);
    });
    ASSERT_TRUE(wait_until([&] { return ready.load(); }));
    if (GetParam()) {
        ASSERT_TRUE(wait_until([&] { return lot._waiter_num.load() == 1; }));
    }
    usleep(10 * 1000);
    ASSERT_EQ(0, returned.load());
    const int nwoken = lot.signal(1);
    ASSERT_GE(nwoken, 0);
    ASSERT_LE(nwoken, 1);
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_EQ(1, returned.load());
    ASSERT_EQ(8, lot.get_state().val);
    ASSERT_EQ(0, lot._waiter_num.load());
    ASSERT_EQ(0, lot.signal(1));
}

TEST_P(ParkingLotTest, signal_wakes_one_waiter) {
    check_wake_count(1, 1, 1);
}

TEST_P(ParkingLotTest, signal_wakes_a_limited_batch) {
    check_wake_count(4, 2, 2);
}

TEST_P(ParkingLotTest, signal_is_limited_by_available_waiters) {
    check_wake_count(4, 10, 4);
}

TEST_P(ParkingLotTest, stop_preserves_signal_count_and_is_idempotent) {
    ParkingLot lot;
    lot.signal(3);
    const auto before_stop = lot.get_state();
    lot.stop();
    const auto stopped = lot.get_state();
    ASSERT_TRUE(stopped.stopped());
    ASSERT_EQ(7, stopped.val);
    lot.stop();
    ASSERT_EQ(stopped.val, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(2));
    ASSERT_EQ(11, lot.get_state().val);
    ASSERT_TRUE(lot.get_state().stopped());
    ASSERT_FALSE(before_stop.stopped());
    ASSERT_EQ(6, before_stop.val);
    ASSERT_EQ(7, stopped.val);
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, stop_before_wait_invalidates_old_snapshots) {
    ParkingLot lot;
    const auto initial = lot.get_state();
    lot.signal(2);
    const auto before_stop = lot.get_state();
    lot.stop();
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        // Callers must check State::stopped() before waiting. Only snapshots
        // taken before stop(), not a fresh stopped snapshot, are waitable here.
        lot.wait(initial);
        lot.wait(before_stop);
    });
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_TRUE(lot.get_state().stopped());
    ASSERT_EQ(5, lot.get_state().val);
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, stop_wakes_all_waiters) {
    const int num_waiters = 8;
    ParkingLot lot;
    const auto initial = lot.get_state();
    butil::atomic<int> ready(0);
    ParkingLotThreads threads(&lot);
    for (int i = 0; i < num_waiters; ++i) {
        threads.start([&] {
            ready.fetch_add(1, butil::memory_order_release);
            while (!threads.cancelled() && !lot.get_state().stopped()) {
                lot.wait(initial);
            }
        });
    }
    ASSERT_TRUE(wait_until([&] { return ready.load() == num_waiters; }));
    if (GetParam()) {
        ASSERT_TRUE(wait_until([&] {
            return lot._waiter_num.load() == num_waiters;
        }));
    }
    lot.stop();
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_TRUE(lot.get_state().stopped());
    ASSERT_EQ(1, lot.get_state().val);
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, concurrent_signals_are_not_lost) {
    const int num_signalers = 4;
    const int iterations = 1000;
    ParkingLot lot;
    butil::atomic<int> bad_returns(0);
    ParkingLotThreads threads(&lot);
    for (int i = 1; i <= num_signalers; ++i) {
        threads.start([&, i] {
            for (int j = 0; j < iterations && !threads.cancelled(); ++j) {
                if (lot.signal(i) != 0) {
                    bad_returns.fetch_add(1);
                }
            }
        });
    }
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_EQ(0, bad_returns.load());
    ASSERT_EQ(iterations * num_signalers * (num_signalers + 1),
              lot.get_state().val);
    ASSERT_FALSE(lot.get_state().stopped());
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, get_state_acquires_published_data) {
    ParkingLot lot;
    int payload = 0;
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        payload = 42;
        lot.signal(1);
    });
    // Observe the release in signal() through get_state(), not thread.join()
    // or the helper's completion counter, before reading the non-atomic data.
    ASSERT_TRUE(wait_until([&] { return lot.get_state().val != 0; }));
    ASSERT_EQ(42, payload);
}

TEST_P(ParkingLotTest, signal_counter_wraparound_preserves_stop_bit) {
    ParkingLot lot;
    // Atomic signed fetch_add wraps without signed-overflow UB. Keep num_task
    // small enough that the separate signed left shift in signal() is valid.
    ASSERT_EQ(0, lot.signal(INT_MAX / 2));
    ASSERT_EQ(INT_MAX - 1, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(1));
    ASSERT_EQ(INT_MIN, lot.get_state().val);
    ASSERT_FALSE(lot.get_state().stopped());

    lot.stop();
    ASSERT_EQ(INT_MIN + 1, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(INT_MAX / 2));
    ASSERT_EQ(-1, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(1));
    ASSERT_EQ(1, lot.get_state().val);
    ASSERT_TRUE(lot.get_state().stopped());
}

TEST_P(ParkingLotTest, stop_races_with_signals_and_wait) {
    const int iterations = 1000;
    ParkingLot lot;
    butil::atomic<bool> start(false);
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        while (!threads.cancelled()) {
            const auto state = lot.get_state();
            if (state.stopped()) {
                return;
            }
            lot.wait(state);
        }
    });
    threads.start([&] {
        while (!start.load(butil::memory_order_acquire)) {
            if (threads.cancelled()) {
                return;
            }
            std::this_thread::yield();
        }
        for (int i = 0; i < iterations && !threads.cancelled(); ++i) {
            lot.signal(1);
        }
    });
    threads.start([&] {
        while (!start.load(butil::memory_order_acquire)) {
            if (threads.cancelled()) {
                return;
            }
            std::this_thread::yield();
        }
        for (int i = 0; i < iterations && !threads.cancelled(); ++i) {
            lot.stop();
        }
    });
    start.store(true, butil::memory_order_release);
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_EQ(iterations * 2 + 1, lot.get_state().val);
    ASSERT_TRUE(lot.get_state().stopped());
    ASSERT_EQ(0, lot._waiter_num.load());
}

TEST_P(ParkingLotTest, racing_signal_and_wait_do_not_lose_wakeups) {
    const int iterations = 2000;
    ParkingLot lot;
    butil::atomic<int> start(0);
    butil::atomic<int> ready(0);
    butil::atomic<int> completed(0);
    ParkingLotThreads threads(&lot);
    threads.start([&] {
        for (int round = 1; round <= iterations; ++round) {
            while (start.load(butil::memory_order_acquire) != round) {
                if (threads.cancelled()) {
                    return;
                }
                std::this_thread::yield();
            }
            const auto state = lot.get_state();
            ready.store(round, butil::memory_order_release);
            if (round % 3 == 0) {
                std::this_thread::yield();
            }
            lot.wait(state);
            completed.store(round, butil::memory_order_release);
        }
    });

    for (int round = 1; round <= iterations; ++round) {
        SCOPED_TRACE(round);
        start.store(round, butil::memory_order_release);
        ASSERT_TRUE(wait_until([&] {
            return ready.load(butil::memory_order_acquire) == round;
        }));
        if (round % 3 == 1) {
            std::this_thread::yield();
        }
        // The handshake precedes BOTH the waiter registration and signal().
        // Do not wait for _waiter_num here: that would remove the Dekker race.
        // Exactly one signal per round; no later signal may rescue this round.
        const int nwoken = lot.signal(1);
        ASSERT_GE(nwoken, 0);
        ASSERT_LE(nwoken, 1);
        ASSERT_TRUE(wait_until([&] {
            return completed.load(butil::memory_order_acquire) == round;
        })) << "Possible lost wakeup";
        ASSERT_EQ(0, lot._waiter_num.load());
    }
    ASSERT_TRUE(wait_until([&] { return threads.finished(); }));
    ASSERT_EQ(iterations * 2, lot.get_state().val);
    ASSERT_EQ(0, lot.signal(1));
    // This is a real-implementation stress regression, not a deterministic
    // proof of weak-memory ordering. Removing a fence need not fail on x86.
}

INSTANTIATE_TEST_SUITE_P(WaiterCheckModes, ParkingLotTest, ::testing::Bool());

}  // namespace
