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
#include <gflags/gflags.h>
#include <algorithm>
#include <string>
#include <vector>
#include "bthread/sys_futex.h"
#include "bthread/timer_thread.h"
#include "bthread/bthread.h"
#include "butil/logging.h"

namespace {

long timespec_diff_us(const timespec& ts1, const timespec& ts2) {
    return (ts1.tv_sec - ts2.tv_sec) * 1000000L +
        (ts1.tv_nsec - ts2.tv_nsec) / 1000;
}

// A simple class, could keep track of the time when it is invoked.
class TimeKeeper {
public:
    TimeKeeper(timespec run_time)
        : _expect_run_time(run_time), _name(NULL), _sleep_ms(0) {}
    TimeKeeper(timespec run_time, const char* name/*must be string constant*/)
        : _expect_run_time(run_time), _name(name), _sleep_ms(0) {}
    TimeKeeper(timespec run_time, const char* name/*must be string constant*/,
               int sleep_ms)
        : _expect_run_time(run_time), _name(name), _sleep_ms(sleep_ms) {}

    void schedule(bthread::TimerThread* timer_thread) {
        _task_id = timer_thread->schedule(
            TimeKeeper::routine, this, _expect_run_time);
    }

    void run()
    {
        timespec current_time;
        clock_gettime(CLOCK_REALTIME, &current_time);
        if (_name) {
            LOG(INFO) << "Run `" << _name << "' task_id=" << _task_id;
        } else {
            LOG(INFO) << "Run task_id=" << _task_id;
        }
        _run_times.push_back(current_time);
        const int saved_sleep_ms = _sleep_ms;
        if (saved_sleep_ms > 0) {
            timespec timeout = butil::milliseconds_to_timespec(saved_sleep_ms);
            bthread::futex_wait_private(&_sleep_ms, saved_sleep_ms, &timeout);
        }
    }

    void wakeup() {
        if (_sleep_ms != 0) {
            _sleep_ms = 0;
            bthread::futex_wake_private(&_sleep_ms, 1);
        } else {
            LOG(ERROR) << "No need to wakeup "
                       << (_name ? _name : "") << " task_id=" << _task_id;
        }
    }

    // verify the first run is in specified time range.
    void expect_first_run()
    {
        expect_first_run(_expect_run_time);
    }

    void expect_first_run(timespec expect_run_time)
    {
        ASSERT_TRUE(!_run_times.empty());
        long diff = timespec_diff_us(_run_times[0], expect_run_time);
        EXPECT_LE(labs(diff), 50000);
    }
    
    void expect_not_run() {
        EXPECT_TRUE(_run_times.empty());
    }

    static void routine(void *arg)
    {
        TimeKeeper* keeper = (TimeKeeper*)arg;
        keeper->run();
    }

    timespec _expect_run_time;
    bthread::TimerThread::TaskId _task_id;

private:
    const char* _name;
    int _sleep_ms;
    std::vector<timespec> _run_times;
};

TEST(TimerThreadTest, RunTasks) {
    bthread::TimerThread timer_thread;
    ASSERT_EQ(0, timer_thread.start(NULL));

    timespec _2s_later = butil::seconds_from_now(2);
    TimeKeeper keeper1(_2s_later, "keeper1");
    keeper1.schedule(&timer_thread);

    TimeKeeper keeper2(_2s_later, "keeper2");  // same time with keeper1
    keeper2.schedule(&timer_thread);
    
    timespec _1s_later = butil::seconds_from_now(1);
    TimeKeeper keeper3(_1s_later, "keeper3");
    keeper3.schedule(&timer_thread);

    timespec _10s_later = butil::seconds_from_now(10);
    TimeKeeper keeper4(_10s_later, "keeper4");
    keeper4.schedule(&timer_thread);

    TimeKeeper keeper5(_10s_later, "keeper5");
    keeper5.schedule(&timer_thread);
    
    // sleep 1 second, and unschedule task2
    LOG(INFO) << "Sleep 1s";
    sleep(1);
    timer_thread.unschedule(keeper2._task_id);
    timer_thread.unschedule(keeper4._task_id);

    timespec old_time = { 0, 0 };
    TimeKeeper keeper6(old_time, "keeper6");
    keeper6.schedule(&timer_thread);
    const timespec keeper6_addtime = butil::seconds_from_now(0);

    // sleep 10 seconds and stop.
    LOG(INFO) << "Sleep 2s";
    sleep(2);
    LOG(INFO) << "Stop timer_thread";
    butil::Timer tm;
    tm.start();
    timer_thread.stop_and_join();
    tm.stop();
    ASSERT_LE(tm.m_elapsed(), 15);

    // verify all runs in expected time range.
    keeper1.expect_first_run();
    keeper2.expect_not_run();
    keeper3.expect_first_run();
    keeper4.expect_not_run();
    keeper5.expect_not_run();
    keeper6.expect_first_run(keeper6_addtime);
}

// If the scheduled time is before start time, then should run it
// immediately.
TEST(TimerThreadTest, start_after_schedule) {
    bthread::TimerThread timer_thread;
    timespec past_time = { 0, 0 };
    TimeKeeper keeper(past_time, "keeper1");
    keeper.schedule(&timer_thread);
    ASSERT_EQ(bthread::TimerThread::INVALID_TASK_ID, keeper._task_id);
    ASSERT_EQ(0, timer_thread.start(NULL));
    keeper.schedule(&timer_thread);
    ASSERT_NE(bthread::TimerThread::INVALID_TASK_ID, keeper._task_id);
    timespec current_time = butil::seconds_from_now(0);
    sleep(1);  // make sure timer thread start and run
    timer_thread.stop_and_join();
    keeper.expect_first_run(current_time);
}

class TestTask {
public:
    TestTask(bthread::TimerThread* timer_thread, TimeKeeper* keeper1,
             TimeKeeper* keeper2, int expected_unschedule_result)
        : _timer_thread(timer_thread)
        , _keeper1(keeper1)
        , _keeper2(keeper2)
        , _expected_unschedule_result(expected_unschedule_result) {
    }
            
    void run()
    {
        clock_gettime(CLOCK_REALTIME, &_running_time);
        EXPECT_EQ(_expected_unschedule_result,
                  _timer_thread->unschedule(_keeper1->_task_id));
        _keeper2->schedule(_timer_thread);
    }

    static void routine(void* arg)
    {
        TestTask* task = (TestTask*)arg;
        task->run();
    }
    timespec _running_time;

private:
    bthread::TimerThread* _timer_thread;  // not owned.
    TimeKeeper* _keeper1;  // not owned.
    TimeKeeper* _keeper2;  // not owned.
    int _expected_unschedule_result;
};

// Perform schedule and unschedule inside a running task
TEST(TimerThreadTest, schedule_and_unschedule_in_task) {
    bthread::TimerThread timer_thread;
    timespec past_time = { 0, 0 };
    timespec future_time = { std::numeric_limits<int>::max(), 0 };
    const timespec _500ms_after = butil::milliseconds_from_now(500);

    TimeKeeper keeper1(future_time, "keeper1");
    TimeKeeper keeper2(past_time, "keeper2");
    TimeKeeper keeper3(past_time, "keeper3");
    TimeKeeper keeper4(past_time, "keeper4");
    TimeKeeper keeper5(_500ms_after, "keeper5", 10000/*10s*/);

    ASSERT_EQ(0, timer_thread.start(NULL));
    keeper1.schedule(&timer_thread);  // start keeper1
    keeper3.schedule(&timer_thread);  // start keeper3
    timespec keeper3_addtime = butil::seconds_from_now(0);
    keeper5.schedule(&timer_thread);  // start keeper5
    sleep(1);  // let keeper1/3/5 run

    TestTask test_task1(&timer_thread, &keeper1, &keeper2, 0);
    timer_thread.schedule(TestTask::routine, &test_task1, past_time);

    TestTask test_task2(&timer_thread, &keeper3, &keeper4, -1);
    timer_thread.schedule(TestTask::routine, &test_task2, past_time);

    sleep(1);
    // test_task1/2 should be both blocked by keeper5.
    keeper2.expect_not_run();
    keeper4.expect_not_run();

    // unscheduling (running) keeper5 should have no effect and returns 1
    ASSERT_EQ(1, timer_thread.unschedule(keeper5._task_id));
    
    // wake up keeper5 to let test_task1/2 run.
    keeper5.wakeup();
    sleep(1);

    timer_thread.stop_and_join();
    timespec finish_time;
    clock_gettime(CLOCK_REALTIME, &finish_time);

    keeper1.expect_not_run();
    keeper2.expect_first_run(test_task1._running_time);
    keeper3.expect_first_run(keeper3_addtime);
    keeper4.expect_first_run(test_task2._running_time);
    keeper5.expect_first_run();
}

static void noop_routine(void*) {}

// RAII helper: set a gflag for the duration of a test and restore its previous
// value on scope exit, so tests don't leak configuration into later tests and
// cause order-dependent failures. Also asserts the flag exists and was set.
class ScopedFlag {
public:
    ScopedFlag(const char* name, const char* value) : _name(name) {
        EXPECT_TRUE(GFLAGS_NAMESPACE::GetCommandLineOption(name, &_old_value))
            << "unknown gflag " << name;
        EXPECT_FALSE(
            GFLAGS_NAMESPACE::SetCommandLineOption(name, value).empty())
            << "failed to set gflag " << name << "=" << value;
    }
    ~ScopedFlag() {
        GFLAGS_NAMESPACE::SetCommandLineOption(_name.c_str(), _old_value.c_str());
    }
private:
    std::string _name;
    std::string _old_value;
};

// Tasks that are unscheduled after being pulled into the timer thread's
// internal heap must not stay there (occupying a pooled Task slot) until
// their run_time. With large timeouts that would let memory grow ~ qps *
// timeout. The timer thread should sweep them out and keep the heap bounded.
TEST(TimerThreadTest, sweep_unscheduled_tasks_in_heap) {
    // Lower the sweep threshold so the test doesn't need a huge heap.
    ScopedFlag sweep_flag("brpc_timer_heap_sweep_min_size", "512");

    bthread::TimerThread timer_thread;
    ASSERT_EQ(0, timer_thread.start(NULL));

    // Run far enough in the future that these tasks never fire on their own.
    const timespec far = butil::seconds_from_now(100000);
    const size_t kBatch = 2000;
    const size_t kRounds = 20;

    int64_t max_pending = 0;
    for (size_t r = 0; r < kRounds; ++r) {
        std::vector<bthread::TimerThread::TaskId> ids;
        ids.reserve(kBatch);
        for (size_t i = 0; i < kBatch; ++i) {
            ids.push_back(timer_thread.schedule(noop_routine, NULL, far));
        }
        // A near-term task forces the timer thread to wake up and consume the
        // buckets, so the far tasks above land in the heap (alive).
        timer_thread.schedule(noop_routine, NULL,
                              butil::milliseconds_from_now(1));
        usleep(20000);  // let the timer thread consume the buckets

        // Now unschedule the far tasks: they become dead-in-heap, exactly the
        // case that used to linger until run_time.
        for (size_t i = 0; i < ids.size(); ++i) {
            timer_thread.unschedule(ids[i]);
        }
        // Another near-term task wakes the timer thread again, triggering the
        // sweep that reclaims the dead tasks.
        timer_thread.schedule(noop_routine, NULL,
                              butil::milliseconds_from_now(1));
        usleep(20000);

        // Read the internal heap size directly (brpc tests are built with
        // -fno-access-control, so no public accessor is needed).
        const int64_t pending =
            timer_thread._npending.load(butil::memory_order_relaxed);
        LOG(INFO) << "round=" << r << " pending=" << pending;
        max_pending = std::max(max_pending, pending);
    }

    // Without the sweep, pending would grow to ~ kBatch * kRounds (40000).
    // With it, the heap is bounded by roughly a couple of sweep thresholds.
    EXPECT_LT(max_pending, (int64_t)(kBatch * kRounds) / 4)
        << "dead tasks are not being reclaimed from the heap";

    timer_thread.stop_and_join();
}

// When every pending task is far in the future, the nearest run_time never
// arrives, so the timer thread used to sleep for the whole duration and never
// consume the buckets. Tasks scheduled meanwhile (and the slots they occupy)
// would then be stranded in the buckets until that far deadline. The capped
// wakeup makes the timer thread wake up periodically to drain the buckets so
// their tasks are consumed (and, if unscheduled, reclaimed) within the cap
// rather than after the timeout.
TEST(TimerThreadTest, periodic_wakeup_drains_buckets) {
    ScopedFlag wakeup_flag("brpc_timer_max_wakeup_interval_ms", "50");

    bthread::TimerThread timer_thread;
    ASSERT_EQ(0, timer_thread.start(NULL));

    // Anchor task an hour out: it becomes the nearest task, so the tasks below
    // (with even later run_times) are never the "earliest" and thus never wake
    // the timer via schedule() -- only the periodic wakeup can drain them.
    timer_thread.schedule(noop_routine, NULL, butil::seconds_from_now(3600));
    usleep(100000);  // let the anchor be consumed into the heap
    // Only the anchor is in the heap so far.
    ASSERT_EQ(1, timer_thread._npending.load(butil::memory_order_relaxed));

    // Pile far tasks with strictly-increasing run_times into the buckets. None
    // of these wake the timer.
    const int kN = 2000;
    for (int i = 0; i < kN; ++i) {
        timer_thread.schedule(noop_routine, NULL,
                              butil::seconds_from_now(3600 + 1 + i));
    }

    // Without the periodic wakeup the timer would stay asleep (nearest task is
    // an hour away) and these would sit in the buckets, unconsumed. With it,
    // they are pulled into the heap within a few wakeup intervals.
    usleep(300000);  // several 50ms intervals
    const int64_t pending =
        timer_thread._npending.load(butil::memory_order_relaxed);
    LOG(INFO) << "pending after bucket fill = " << pending << " (scheduled "
              << kN << " + 1 anchor)";
    EXPECT_GE(pending, (int64_t)kN)
        << "buckets were not drained by the periodic wakeup";

    timer_thread.stop_and_join();
}

} // end namespace
