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

#include <condition_variable>
#include <mutex>
#include <gtest/gtest.h>
#include "butil/atomicops.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/logging.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/task_group.h"
#include "bthread/bthread.h"
#include "bthread/unstable.h"
#include "bthread/interrupt_pthread.h"

namespace bthread {
extern butil::atomic<TaskControl*> g_task_control;
inline TaskControl* get_task_control() {
    return g_task_control.load(butil::memory_order_consume);
}
} // namespace bthread

namespace {

TEST(ButexTest, wait_on_already_timedout_butex) {
    uint32_t* butex = bthread::butex_create_checked<uint32_t>();
    ASSERT_TRUE(butex);
    timespec now;
    ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &now));
    *butex = 1;
    ASSERT_EQ(-1, bthread::butex_wait(butex, 1, &now));
    ASSERT_EQ(ETIMEDOUT, errno);
    bthread::butex_destroy(butex);
}

struct JoinSleepArg {
    bthread_t tid = 0;
    uint64_t sleep_us = 0;
    butil::atomic<bool> finished{false};
};

void* sleeper(void* arg) {
    JoinSleepArg* a = static_cast<JoinSleepArg*>(arg);
    butil::Timer tm;
    tm.start();
    EXPECT_EQ(0, bthread_usleep(a->sleep_us));
    tm.stop();
    // Sleep may finish late under load, but must not finish early.
    EXPECT_GE(tm.u_elapsed(), static_cast<int64_t>(a->sleep_us));
    a->finished.store(true, butil::memory_order_release);
    return nullptr;
}

void* joiner(void* arg) {
    JoinSleepArg* args = static_cast<JoinSleepArg*>(arg);
    for (JoinSleepArg* a = args; a->tid; ++a) {
        EXPECT_EQ(0, bthread_join(a->tid, nullptr));
        EXPECT_TRUE(a->finished.load(butil::memory_order_acquire));
    }
    for (JoinSleepArg* a = args; a->tid; ++a) {
        EXPECT_EQ(0, bthread_join(a->tid, nullptr));
    }
    return nullptr;
}

struct A {
    uint64_t a;
    char dummy[0];
};

struct B {
    uint64_t a;
};


TEST(ButexTest, with_or_without_array_zero) {
    ASSERT_EQ(sizeof(B), sizeof(A));
}


TEST(ButexTest, join) {
    const size_t N = 6;
    const size_t M = 6;
    JoinSleepArg args[N+1];
    bthread_t jth[M];
    pthread_t pth[M];
    for (size_t i = 0; i < N; ++i) {
        bthread_attr_t attr = (i == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
        args[i].sleep_us = 100000L/*100ms*/ * (i + 1);
        ASSERT_EQ(0, bthread_start_urgent(&args[i].tid, &attr, sleeper, &args[i]));
    }
    // The last argument's zero tid terminates the joiner's iteration.
    for (size_t i = 0; i < M; ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&jth[i], nullptr, joiner, args));
    }
    for (size_t i = 0; i < M; ++i) {
        ASSERT_EQ(0, pthread_create(&pth[i], nullptr, joiner, args));
    }
    
    for (size_t i = 0; i < M; ++i) {
        ASSERT_EQ(0, bthread_join(jth[i], nullptr))
            << "i=" << i << " error=" << berror();
    }
    for (size_t i = 0; i < M; ++i) {
        ASSERT_EQ(0, pthread_join(pth[i], nullptr));
    }
}


struct WaiterArg {
    int expected_result;
    int expected_value;
    butil::atomic<int> *butex;
    const timespec *ptimeout;
};

void* waiter(void* arg) {
    WaiterArg * wa = (WaiterArg*)arg;
    long t1 = butil::gettimeofday_us();
    int rc = bthread::butex_wait(wa->butex, wa->expected_value, wa->ptimeout);
    int saved_errno = errno;
    long t2 = butil::gettimeofday_us();
    if (rc == 0) {
        EXPECT_EQ(wa->expected_result, 0) << bthread_self();
    } else {
        EXPECT_EQ(wa->expected_result, saved_errno) << bthread_self();
        if (saved_errno == ETIMEDOUT && wa->ptimeout) {
            EXPECT_GE(t2, butil::timespec_to_microseconds(*wa->ptimeout));
        }
    }
    LOG(INFO) << "after wait, time=" << (t2-t1) << "us";
    return nullptr;
}

TEST(ButexTest, sanity) {
    const size_t N = 5;
    WaiterArg args[N * 4];
    pthread_t pthreads[2 * N];
    bthread_t bthreads[2 * N];
    butil::atomic<int>* b1 = bthread::butex_create_checked<butil::atomic<int> >();
    ASSERT_TRUE(b1);
    bthread::butex_destroy(b1);
    
    b1 = bthread::butex_create_checked<butil::atomic<int> >();
    *b1 = 1;
    ASSERT_EQ(0, bthread::butex_wake(b1));

    WaiterArg unmatched_arg = { EWOULDBLOCK, *b1 + 1, b1, nullptr };
    pthread_t unmatched_pthread;
    bthread_t unmatched_bthread;
    ASSERT_EQ(0, pthread_create(&unmatched_pthread, nullptr, waiter, &unmatched_arg));
    ASSERT_EQ(0, bthread_start_urgent(
        &unmatched_bthread, nullptr, waiter, &unmatched_arg));
    ASSERT_EQ(0, pthread_join(unmatched_pthread, nullptr));
    ASSERT_EQ(0, bthread_join(unmatched_bthread, nullptr));

    timespec abstime = butil::seconds_from_now(1);
    for (size_t i = 0; i < 4 * N; ++i) {
        args[i].expected_value = *b1;
        args[i].butex = b1;
        if ((i % 2) == 0) {
            args[i].expected_result = 0;
            args[i].ptimeout = nullptr;
        } else {
            args[i].expected_result = ETIMEDOUT;
            args[i].ptimeout = &abstime;
        }
        if (i < 2*N) {
            ASSERT_EQ(0, pthread_create(&pthreads[i], nullptr, waiter, &args[i]));
        } else {
            ASSERT_EQ(0, bthread_start_urgent(&bthreads[i - 2*N], nullptr, waiter, &args[i]));
        }
    }

    // Join timed waiters before waking anyone, rather than assuming all
    // timeout callbacks have completed after a fixed sleep.
    for (size_t i = 1; i < 2 * N; i += 2) {
        ASSERT_EQ(0, pthread_join(pthreads[i], nullptr));
        ASSERT_EQ(0, bthread_join(bthreads[i], nullptr));
    }
    size_t nwoken = 0;
    int64_t deadline = butil::cpuwide_time_us() + 5000000L;
    while (nwoken < 2 * N) {
        int rc = bthread::butex_wake(b1);
        ASSERT_GE(rc, 0);
        ASSERT_LE(rc, 1);
        nwoken += rc;
        if (rc == 0) {
            ASSERT_LT(butil::cpuwide_time_us(), deadline)
                << "Timed out waiting for untimed waiters to register";
            bthread_usleep(1000);
        }
    }
    ASSERT_EQ(0, bthread::butex_wake(b1));
    for (size_t i = 0; i < 2 * N; i += 2) {
        ASSERT_EQ(0, pthread_join(pthreads[i], nullptr));
        ASSERT_EQ(0, bthread_join(bthreads[i], nullptr));
    }
    bthread::butex_destroy(b1);
}

// A gate deliberately using pthread-backed synchronization: stopping a bthread
// must not consume its pending interruption while it waits at the test gate.
class TestGate {
public:
    void wait() {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this] { return _open; });
    }

    void signal() {
        std::lock_guard<std::mutex> lock(_mutex);
        _open = true;
        _cond.notify_all();
    }

private:
    std::mutex _mutex;
    std::condition_variable _cond;
    bool _open = false;
};

struct ButexWaitArg {
    int* butex;
    int expected_val;
    long wait_msec;
    int error_code;
    TestGate* before_wait;
    TestGate* entering_wait;
};

void* wait_butex(void* void_arg) {
    ButexWaitArg* arg = static_cast<ButexWaitArg*>(void_arg);
    if (arg->before_wait) {
        arg->before_wait->wait();
    }
    if (arg->entering_wait) {
        arg->entering_wait->signal();
    }
    timespec ts = butil::milliseconds_from_now(arg->wait_msec);
    int rc = bthread::butex_wait(arg->butex, arg->expected_val,
                               arg->wait_msec < 0 ? nullptr : &ts);
    int saved_errno = errno;
    if (arg->error_code) {
        EXPECT_EQ(-1, rc);
        EXPECT_EQ(arg->error_code, saved_errno);
    } else {
        EXPECT_EQ(0, rc);
    }
    return nullptr;
}

TEST(ButexTest, wait_without_stop) {
    int* butex = bthread::butex_create_checked<int>();
    *butex = 7;
    butil::Timer tm;
    long WAIT_MSEC = 500;
    for (int i = 0; i < 2; ++i) {
        bthread_attr_t attr = (i == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
        ButexWaitArg arg = { butex, *butex, WAIT_MSEC, ETIMEDOUT,
                            nullptr, nullptr };
        bthread_t th;
        
        tm.start();
        ASSERT_EQ(0, bthread_start_urgent(&th, &attr, wait_butex, &arg));
        ASSERT_EQ(0, bthread_join(th, nullptr));
        tm.stop();
        
        // Timer delivery and rescheduling may be arbitrarily delayed by load.
        ASSERT_GE(tm.m_elapsed(), WAIT_MSEC);
    }
    bthread::butex_destroy(butex);
}

TEST(ButexTest, stop_after_running) {
    int* butex = bthread::butex_create_checked<int>();
    *butex = 7;
    // Repetition also covers interruption between waiter registration and
    // the pthread's futex wait, which must not leak EWOULDBLOCK to the caller.
    for (int i = 0; i < 100; ++i) {
        bthread_attr_t attr = (i % 2 == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
        bthread_t th;
        TestGate entering_wait;
        // No timeout may race with stop. The waiter must report EINTR.
        ButexWaitArg arg = { butex, *butex, -1, EINTR,
                            nullptr, &entering_wait };

        ASSERT_EQ(0, bthread_start_urgent(&th, &attr, wait_butex, &arg));
        entering_wait.wait();
        EXPECT_EQ(0, bthread_stop(th));
        ASSERT_EQ(0, bthread_join(th, nullptr));
        ASSERT_EQ(EINVAL, bthread_stop(th));
    }
    bthread::butex_destroy(butex);
}

TEST(ButexTest, stop_before_running) {
    int* butex = bthread::butex_create_checked<int>();
    *butex = 7;

    for (int i = 0; i < 2; ++i) {
        bthread_attr_t attr =
            (i == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) | BTHREAD_NOSIGNAL;
        bthread_t th;
        TestGate before_wait;
        ButexWaitArg arg = { butex, *butex, -1, EINTR,
                            &before_wait, nullptr };

        ASSERT_EQ(0, bthread_start_background(&th, &attr, wait_butex, &arg));
        EXPECT_EQ(0, bthread_stop(th));
        // NOSIGNAL suppresses notification, but does not prevent a worker
        // from picking up the task. The gate enforces stop-before-wait.
        before_wait.signal();
        bthread_flush();
        ASSERT_EQ(0, bthread_join(th, nullptr));
        ASSERT_EQ(EINVAL, bthread_stop(th));
    }
    bthread::butex_destroy(butex);
}

struct JoinWaiterArg {
    bthread_t tid;
    TestGate entering_join;
    butil::atomic<bool> finished{false};
};

void* join_the_waiter(void* arg) {
    JoinWaiterArg* a = static_cast<JoinWaiterArg*>(arg);
    a->entering_join.signal();
    EXPECT_EQ(0, bthread_join(a->tid, nullptr));
    a->finished.store(true, butil::memory_order_release);
    return nullptr;
}

TEST(ButexTest, join_cant_be_wakeup) {
    int* butex = bthread::butex_create_checked<int>();
    *butex = 7;
    for (int i = 0; i < 2; ++i) {
        bthread_attr_t attr =
            (i == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
        bthread_t th, th2;
        TestGate entering_wait;
        ButexWaitArg arg = { butex, *butex, -1, EINTR,
                            nullptr, &entering_wait };
        ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, wait_butex, &arg));
        // Start the target before a pthread-stack joiner can block its worker.
        entering_wait.wait();
        JoinWaiterArg join_arg;
        join_arg.tid = th;
        ASSERT_EQ(0, bthread_start_urgent(&th2, &attr, join_the_waiter, &join_arg));
        join_arg.entering_join.wait();
        EXPECT_EQ(0, bthread_stop(th2));
        // Give the interrupted joiner a chance to run. There is no deadline
        // on the target waiter, so scheduler delays cannot make it exit.
        EXPECT_EQ(0, bthread_usleep(50000));
        EXPECT_FALSE(join_arg.finished.load(butil::memory_order_acquire));
        EXPECT_TRUE(bthread::TaskGroup::exists(th));
        EXPECT_TRUE(bthread::TaskGroup::exists(th2));
        EXPECT_EQ(0, bthread_stop(th));
        ASSERT_EQ(0, bthread_join(th2, nullptr));
        ASSERT_EQ(0, bthread_join(th, nullptr));
        EXPECT_TRUE(join_arg.finished.load(butil::memory_order_acquire));
        ASSERT_EQ(EINVAL, bthread_stop(th));
        ASSERT_EQ(EINVAL, bthread_stop(th2));
    }
    bthread::butex_destroy(butex);
}

struct StopSleepArg {
    bool pthread_task;
    TestGate* before_sleep;
    TestGate entering_sleep;
    TestGate allow_exit;
};

void* stoppable_sleeper(void* arg) {
    StopSleepArg* a = static_cast<StopSleepArg*>(arg);
    if (a->before_sleep) {
        a->before_sleep->wait();
    }
    // Pthread-stack tasks use native usleep, which bthread_stop cannot
    // interrupt. Normal bthreads must return ESTOP rather than time out.
    int64_t sleep_us = a->pthread_task ? 100000L : 60000000L;
    a->entering_sleep.signal();
    butil::Timer tm;
    tm.start();
    int rc = bthread_usleep(sleep_us);
    int saved_errno = errno;
    tm.stop();
    if (a->pthread_task) {
        EXPECT_EQ(0, rc);
        EXPECT_GE(tm.u_elapsed(), sleep_us);
    } else {
        EXPECT_EQ(-1, rc);
        EXPECT_EQ(ESTOP, saved_errno);
    }
    // Keep the task alive even if the controller is descheduled longer than
    // the native sleep, so stop() cannot race with task destruction.
    a->allow_exit.wait();
    return nullptr;
}

void TestStopSleep(bool stop_before_sleep, bool wait_until_sleeping) {
    for (int i = 0; i < 2; ++i) {
        bthread_attr_t attr = (i == 0 ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
        StopSleepArg arg;
        arg.pthread_task = (i == 0);
        TestGate before_sleep;
        arg.before_sleep = stop_before_sleep ? &before_sleep : nullptr;
        bthread_t th;
        if (stop_before_sleep) {
            attr = attr | BTHREAD_NOSIGNAL;
            ASSERT_EQ(0, bthread_start_background(
                &th, &attr, stoppable_sleeper, &arg));
        } else {
            ASSERT_EQ(0, bthread_start_urgent(&th, &attr, stoppable_sleeper, &arg));
            arg.entering_sleep.wait();
            if (wait_until_sleeping && !arg.pthread_task) {
                // Observe timer registration instead of assuming a fixed
                // delay is enough for the worker to enter sleep.
                bthread::TaskMeta* meta = bthread::TaskGroup::address_meta(th);
                int64_t deadline = butil::cpuwide_time_us() + 5000000L;
                bool sleeping = false;
                do {
                    pthread_spin_lock(&meta->version_lock);
                    sleeping = (meta->current_sleep != 0);
                    pthread_spin_unlock(&meta->version_lock);
                    if (sleeping) {
                        break;
                    }
                    bthread_usleep(1000);
                } while (butil::cpuwide_time_us() < deadline);
                EXPECT_TRUE(sleeping) << "Timed out waiting for sleep registration";
            }
        }
        EXPECT_EQ(0, bthread_stop(th));
        before_sleep.signal();
        arg.allow_exit.signal();
        if (stop_before_sleep) {
            bthread_flush();
        }
        ASSERT_EQ(0, bthread_join(th, nullptr));
        ASSERT_EQ(EINVAL, bthread_stop(th));
    }
}

TEST(ButexTest, stop_after_slept) {
    TestStopSleep(false, true);
}

TEST(ButexTest, stop_just_when_sleeping) {
    TestStopSleep(false, false);
}

TEST(ButexTest, stop_before_sleeping) {
    TestStopSleep(true, false);
}

struct SignalArg {
    pthread_t waiter;
    WaiterArg* wait_arg;
    butil::atomic<bool> stop{false};
};

void* signal_waiter(void* arg) {
    SignalArg* a = static_cast<SignalArg*>(arg);
    waiter(a->wait_arg);
    a->stop.store(true);
    return nullptr;
}

void* trigger_signal(void* arg) {
    SignalArg* a = static_cast<SignalArg*>(arg);
    long t1 = butil::gettimeofday_us();
    for (size_t i = 0; i < 50 && !a->stop.load(); ++i) {
      usleep(100000);
      if (a->stop.load() || bthread::interrupt_pthread(a->waiter) == ESRCH) {
        LOG(INFO) << "waiter thread end, trigger count=" << i;
        break;
      }
    }
    long t2 = butil::gettimeofday_us();
    LOG(INFO) << "trigger signal thread end, elapsed=" << (t2-t1) << "us";
    return nullptr;
}

TEST(ButexTest, wait_with_signal_triggered) {
    butil::Timer tm;

    int64_t WAIT_MSEC = 500;
    WaiterArg waiter_args;
    pthread_t waiter_th, tigger_th;
    butil::atomic<int>* butex =
        bthread::butex_create_checked<butil::atomic<int> >();
    ASSERT_TRUE(butex);
    *butex = 1;
    ASSERT_EQ(0, bthread::butex_wake(butex));

    timespec abstime = butil::milliseconds_from_now(WAIT_MSEC);
    waiter_args.expected_value = *butex;
    waiter_args.butex = butex;
    waiter_args.expected_result = ETIMEDOUT;
    waiter_args.ptimeout = &abstime;
    tm.start();
    SignalArg signal_arg;
    signal_arg.wait_arg = &waiter_args;
    ASSERT_EQ(0, pthread_create(&waiter_th, nullptr, signal_waiter, &signal_arg));
    signal_arg.waiter = waiter_th;
    int signal_rc = pthread_create(
        &tigger_th, nullptr, trigger_signal, &signal_arg);
    EXPECT_EQ(0, signal_rc);
    // Keep the target joinable until the signalling thread has stopped.
    // pthread_kill on a pthread_t whose lifetime ended at join is undefined.
    if (signal_rc == 0) {
        EXPECT_EQ(0, pthread_join(tigger_th, nullptr));
    }
    ASSERT_EQ(0, pthread_join(waiter_th, nullptr));
    tm.stop();
    auto wait_elapsed_ms = tm.m_elapsed();
    LOG(INFO) << "waiter thread end, elapsed " << wait_elapsed_ms << " ms";

    // The timeout is absolute and starts before the worker is scheduled.
    // Check the deadline itself rather than a narrow elapsed-time window.
    EXPECT_GE(butil::gettimeofday_us(),
              butil::timespec_to_microseconds(abstime));

    bthread::butex_destroy(butex);
}

} // namespace
