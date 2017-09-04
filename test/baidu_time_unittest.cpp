// Copyright (c) 2014 baidu-rpc authors.
// Author: Ge,Jun (gejun@baidu.com)
// Date: 2010-12-04 11:59

#include <gtest/gtest.h>
#include <syscall.h>                         // SYS_clock_gettime
#include <unistd.h>                          // syscall
#include "base/time.h"
#include "base/macros.h"
#include "base/logging.h"

namespace base {
extern const clockid_t monotonic_clockid;
extern const uint64_t invariant_cpu_freq;
}

namespace {

TEST(BaiduTimeTest, cpuwide_time) {
    const long t1 = base::cpuwide_time_us();
    usleep(10000);
    const long t2 = base::cpuwide_time_us();
    printf("elp=%luus freq=%lu\n", t2-t1, base::invariant_cpu_freq);
}

TEST(BaiduTimeTest, diff_between_gettimeofday_and_REALTIME) {
    long t1 = base::gettimeofday_us();
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    long t2 = base::timespec_to_microseconds(time);
    LOG(INFO) << "t1=" << t1 << " t2=" << t2;
}

const char* clock_desc[] = {
    "CLOCK_REALTIME",                 //0
    "CLOCK_MONOTONIC",                //1
    "CLOCK_PROCESS_CPUTIME_ID",       //2
    "CLOCK_THREAD_CPUTIME_ID",        //3
    "CLOCK_MONOTONIC_RAW",            //4
    "CLOCK_REALTIME_COARSE",          //5
    "CLOCK_MONOTONIC_COARSE",         //6
    "CLOCK_BOOTTIME",                 //7
    "CLOCK_REALTIME_ALARM",           //8
    "CLOCK_BOOTTIME_ALARM",           //9
    "CLOCK_SGI_CYCLE",                //10
    "CLOCK_TAI"                       //11
};

TEST(BaiduTimeTest, fast_realtime) {
    for (size_t i = 0; i < 10; ++i) {
        long t1 = base::gettimeofday_us();
        timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        long t2 = base::timespec_to_microseconds(time);
        base::fast_realtime(&time);
        long t3 = base::timespec_to_microseconds(time);
        long t4 = base::fast_realtime_ns() / 1000L;
        LOG(INFO) << "t1=" << t1 << " t2=" << t2 << " t3=" << t3 << " t4=" << t4;
        usleep(7000);
    }
}

TEST(BaiduTimeTest, cost_of_timer) {
    printf("sizeof(time_t)=%lu\n", sizeof(time_t));
    
    base::Timer t1, t2;
    timespec ts;
    const size_t N = 200000;
    t1.start();
    for (size_t i = 0; i < N; ++i) {
        t2.stop();
    }
    t1.stop();
    printf("Timer::stop() takes %ldns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        clock();
    }
    t1.stop();
    printf("clock() takes %ldns\n", t1.n_elapsed() / N);

    long s = 0;
    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += base::cpuwide_time_ns();
    }
    t1.stop();
    printf("cpuwide_time() takes %ldns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += base::fast_realtime_ns();
    }
    t1.stop();
    printf("fast_realtime_ns takes %luns\n", t1.n_elapsed() / N);
    
    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += base::gettimeofday_us();
    }
    t1.stop();
    printf("gettimeofday_us takes %luns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        timespec ts2;
        base::fast_realtime(&ts2);
    }
    t1.stop();
    printf("fast_realtime takes %luns\n", t1.n_elapsed() / N);

    for (size_t i = 0; i < arraysize(clock_desc); ++i) {
        if (0 == syscall(SYS_clock_gettime, (clockid_t)i, &ts)) {
            t1.start();
            for (size_t j = 0; j < N; ++j) {
                syscall(SYS_clock_gettime, (clockid_t)i, &ts);
            }
            t1.stop();
            printf("sys   clock_gettime(%s) takes %luns\n",
                   clock_desc[i], t1.n_elapsed() / N);
        }
        if (0 == clock_gettime((clockid_t)i, &ts)) {
            t1.start();
            for (size_t j = 0; j < N; ++j) {
                clock_gettime((clockid_t)i, &ts);
            }
            t1.stop();
            printf("glibc clock_gettime(%s) takes %luns\n",
                   clock_desc[i], t1.n_elapsed() / N);
        }
    }

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        time(NULL);
    }
    t1.stop();
    printf("time(NULL) takes %luns\n", t1.n_elapsed() / N);
}

TEST(BaiduTimeTest, timespec) {
    timespec ts1 = { 0, -1 };
    base::timespec_normalize(&ts1);
    ASSERT_EQ(999999999L, ts1.tv_nsec);
    ASSERT_EQ(-1, ts1.tv_sec);

    timespec ts2 = { 0, 1000000000L };
    base::timespec_normalize(&ts2);
    ASSERT_EQ(0L, ts2.tv_nsec);
    ASSERT_EQ(1L, ts2.tv_sec);

    timespec ts3 = { 0, 999999999L };
    base::timespec_normalize(&ts3);
    ASSERT_EQ(999999999L, ts3.tv_nsec);
    ASSERT_EQ(0, ts3.tv_sec);

    timespec ts4 = { 0, 1L };
    base::timespec_add(&ts4, ts3);
    ASSERT_EQ(0, ts4.tv_nsec);
    ASSERT_EQ(1L, ts4.tv_sec);

    timespec ts5 = { 0, 999999999L };
    base::timespec_minus(&ts5, ts3);
    ASSERT_EQ(0, ts5.tv_nsec);
    ASSERT_EQ(0, ts5.tv_sec);

    timespec ts6 = { 0, 999999998L };
    base::timespec_minus(&ts6, ts3);
    ASSERT_EQ(999999999L, ts6.tv_nsec);
    ASSERT_EQ(-1L, ts6.tv_sec);

    timespec ts7 = base::nanoseconds_from(ts3, 1L);
    ASSERT_EQ(0, ts7.tv_nsec);
    ASSERT_EQ(1L, ts7.tv_sec);

    timespec ts8 = base::nanoseconds_from(ts3, -1000000000L);
    ASSERT_EQ(999999999L, ts8.tv_nsec);
    ASSERT_EQ(-1L, ts8.tv_sec);

    timespec ts9 = base::microseconds_from(ts3, 1L);
    ASSERT_EQ(999L, ts9.tv_nsec);
    ASSERT_EQ(1L, ts9.tv_sec);

    timespec ts10 = base::microseconds_from(ts3, -1000000L);
    ASSERT_EQ(999999999L, ts10.tv_nsec);
    ASSERT_EQ(-1L, ts10.tv_sec);

    timespec ts11 = base::milliseconds_from(ts3, 1L);
    ASSERT_EQ(999999L, ts11.tv_nsec);
    ASSERT_EQ(1L, ts11.tv_sec);

    timespec ts12 = base::milliseconds_from(ts3, -1000L);
    ASSERT_EQ(999999999L, ts12.tv_nsec);
    ASSERT_EQ(-1L, ts12.tv_sec);

    timespec ts13 = base::seconds_from(ts3, 1L);
    ASSERT_EQ(999999999L, ts13.tv_nsec);
    ASSERT_EQ(1, ts13.tv_sec);

    timespec ts14 = base::seconds_from(ts3, -1L);
    ASSERT_EQ(999999999L, ts14.tv_nsec);
    ASSERT_EQ(-1L, ts14.tv_sec);
}

TEST(BaiduTimeTest, every_many_us) {
    base::EveryManyUS every_10ms(10000L);
    size_t i = 0;
    const long start_time = base::gettimeofday_ms();
    while (1) {
        if (every_10ms) {
            printf("enter this branch at %lums\n", base::gettimeofday_ms() - start_time);
            if (++i >= 10) {
                break;
            }
        }
    }
}

TEST(BaiduTimeTest, monotonic_time) {
    const long t1 = base::monotonic_time_ms();
    usleep(10000L);
    const long t2 = base::monotonic_time_ms();
    printf("clockid=%d %lums\n", base::monotonic_clockid, t2-t1);
}

TEST(BaiduTimeTest, timer_auto_start) {
    base::Timer t(base::Timer::STARTED);
    usleep(100);
    t.stop();
    printf("Cost %ldus\n", t.u_elapsed());
}

}
