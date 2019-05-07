// Copyright (c) 2014 Baidu, Inc.
// Author: Yang Liu (yangliu@baidu.com)

#include <gtest/gtest.h>
#include <gflags/gflags.h>
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

} // end namespace
