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
#include <thread>
#include <gtest/gtest.h>
#include "butil/build_config.h"

#if defined(OS_LINUX)
#include <syscall.h>                         // SYS_clock_gettime
#include <unistd.h>                          // syscall
#endif

#include "butil/time.h"
#include "butil/macros.h"
#include "butil/logging.h"

namespace {

TEST(BaiduTimeTest, diff_between_gettimeofday_and_REALTIME) {
    long t1 = butil::gettimeofday_us();
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    long t2 = butil::timespec_to_microseconds(time);
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

TEST(BaiduTimeTest, cost_of_timer) {
    printf("sizeof(time_t)=%lu\n", sizeof(time_t));
    
    butil::Timer t1, t2;
    timespec ts;
    const size_t N = 200000;
    t1.start();
    for (size_t i = 0; i < N; ++i) {
        t2.stop();
    }
    t1.stop();
    printf("Timer::stop() takes %" PRId64 "ns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        clock();
    }
    t1.stop();
    printf("clock() takes %" PRId64 "ns\n", t1.n_elapsed() / N);

    long s = 0;
    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += butil::cpuwide_time_ns();
    }
    t1.stop();
    printf("cpuwide_time() takes %" PRId64 "ns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += butil::gettimeofday_us();
    }
    t1.stop();
    printf("gettimeofday_us takes %" PRId64 "ns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        time(nullptr);
    }
    t1.stop();
    printf("time(NULL) takes %" PRId64 "ns\n", t1.n_elapsed() / N);

    t1.start();
    for (size_t i = 0; i < N; ++i) {
        s += butil::monotonic_time_ns();
    }
    t1.stop();
    printf("monotonic_time_ns takes %" PRId64 "ns s=%ld\n", t1.n_elapsed() / N, s);

    for (size_t i = 0; i < arraysize(clock_desc); ++i) {
#if defined(OS_LINUX)
        if (0 == syscall(SYS_clock_gettime, (clockid_t)i, &ts)) {
            t1.start();
            for (size_t j = 0; j < N; ++j) {
                syscall(SYS_clock_gettime, (clockid_t)i, &ts);
            }
            t1.stop();
            printf("sys   clock_gettime(%s) takes %" PRId64 "ns\n",
                   clock_desc[i], t1.n_elapsed() / N);
        }
#endif
        if (0 == clock_gettime((clockid_t)i, &ts)) {
            t1.start();
            for (size_t j = 0; j < N; ++j) {
                clock_gettime((clockid_t)i, &ts);
            }
            t1.stop();
            printf("glibc clock_gettime(%s) takes %" PRId64 "ns\n",
                   clock_desc[i], t1.n_elapsed() / N);
        }
    }
}

TEST(BaiduTimeTest, timespec) {
    timespec ts1 = { 0, -1 };
    butil::timespec_normalize(&ts1);
    ASSERT_EQ(999999999L, ts1.tv_nsec);
    ASSERT_EQ(-1, ts1.tv_sec);

    timespec ts2 = { 0, 1000000000L };
    butil::timespec_normalize(&ts2);
    ASSERT_EQ(0L, ts2.tv_nsec);
    ASSERT_EQ(1L, ts2.tv_sec);

    timespec ts3 = { 0, 999999999L };
    butil::timespec_normalize(&ts3);
    ASSERT_EQ(999999999L, ts3.tv_nsec);
    ASSERT_EQ(0, ts3.tv_sec);

    timespec ts4 = { 0, 1L };
    butil::timespec_add(&ts4, ts3);
    ASSERT_EQ(0, ts4.tv_nsec);
    ASSERT_EQ(1L, ts4.tv_sec);

    timespec ts5 = { 0, 999999999L };
    butil::timespec_minus(&ts5, ts3);
    ASSERT_EQ(0, ts5.tv_nsec);
    ASSERT_EQ(0, ts5.tv_sec);

    timespec ts6 = { 0, 999999998L };
    butil::timespec_minus(&ts6, ts3);
    ASSERT_EQ(999999999L, ts6.tv_nsec);
    ASSERT_EQ(-1L, ts6.tv_sec);

    timespec ts7 = butil::nanoseconds_from(ts3, 1L);
    ASSERT_EQ(0, ts7.tv_nsec);
    ASSERT_EQ(1L, ts7.tv_sec);

    timespec ts8 = butil::nanoseconds_from(ts3, -1000000000L);
    ASSERT_EQ(999999999L, ts8.tv_nsec);
    ASSERT_EQ(-1L, ts8.tv_sec);

    timespec ts9 = butil::microseconds_from(ts3, 1L);
    ASSERT_EQ(999L, ts9.tv_nsec);
    ASSERT_EQ(1L, ts9.tv_sec);

    timespec ts10 = butil::microseconds_from(ts3, -1000000L);
    ASSERT_EQ(999999999L, ts10.tv_nsec);
    ASSERT_EQ(-1L, ts10.tv_sec);

    timespec ts11 = butil::milliseconds_from(ts3, 1L);
    ASSERT_EQ(999999L, ts11.tv_nsec);
    ASSERT_EQ(1L, ts11.tv_sec);

    timespec ts12 = butil::milliseconds_from(ts3, -1000L);
    ASSERT_EQ(999999999L, ts12.tv_nsec);
    ASSERT_EQ(-1L, ts12.tv_sec);

    timespec ts13 = butil::seconds_from(ts3, 1L);
    ASSERT_EQ(999999999L, ts13.tv_nsec);
    ASSERT_EQ(1, ts13.tv_sec);

    timespec ts14 = butil::seconds_from(ts3, -1L);
    ASSERT_EQ(999999999L, ts14.tv_nsec);
    ASSERT_EQ(-1L, ts14.tv_sec);
}

TEST(BaiduTimeTest, every_many_us) {
    butil::EveryManyUS every_10ms(10000L);
    size_t i = 0;
    const long start_time = butil::gettimeofday_ms();
    while (1) {
        if (every_10ms) {
            printf("enter this branch at %" PRId64 "ms\n",
                   butil::gettimeofday_ms() - start_time);
            if (++i >= 10) {
                break;
            }
        }
    }
}

TEST(BaiduTimeTest, timer_auto_start) {
    butil::Timer t(butil::Timer::STARTED);
    usleep(100);
    t.stop();
    printf("Cost %" PRId64 "us\n", t.u_elapsed());
}

TEST(BaiduTimeTest, clock_cycles_monotonic_and_serialized) {
#if defined(__aarch64__) || defined(__x86_64__) || defined(__amd64__) || defined(__loongarch64)
    uint64_t prev = butil::detail::clock_cycles();
    ASSERT_GT(prev, static_cast<uint64_t>(0));
    for (int i = 0; i < 10000; ++i) {
        const uint64_t cur = butil::detail::clock_cycles();
        ASSERT_GE(cur, prev) << "Non-monotonic clock_cycles at iteration " << i;
        prev = cur;
    }
#endif
#if defined(__aarch64__)
    // Verify cross-thread acquire/release ordering of clock_cycles() on AArch64:
    // because clock_cycles() executes isb before mrs cntvct_el0, the reader thread
    // cannot speculatively hoist the counter read ahead of the acquire load.
    std::atomic<uint64_t> published_tsc{0};
    std::atomic<int> step{0};
    const int kHandoffs = 2000;
    std::thread producer([&]() {
        for (int i = 1; i <= kHandoffs; ++i) {
            while (step.load(std::memory_order_acquire) != i - 1) {
            }
            published_tsc.store(butil::detail::clock_cycles(), std::memory_order_relaxed);
            step.store(i, std::memory_order_release);
        }
    });
    for (int i = 1; i <= kHandoffs; ++i) {
        while (step.load(std::memory_order_acquire) != i) {
        }
        const uint64_t after_acquire_tsc = butil::detail::clock_cycles();
        const uint64_t before_release_tsc = published_tsc.load(std::memory_order_relaxed);
        ASSERT_GE(after_acquire_tsc, before_release_tsc) << "handoff=" << i;
    }
    producer.join();
#endif
}

TEST(BaiduTimeTest, cpu_frequency_scale_accuracy_and_monotonicity) {
    uint64_t zero_mult = 1;
    uint32_t zero_shift = 1;
    butil::detail::compute_cpu_frequency_scale(0, &zero_mult, &zero_shift);
    ASSERT_EQ(static_cast<uint64_t>(0), zero_mult);
    ASSERT_EQ(0U, zero_shift);
    butil::detail::compute_cpu_frequency_scale(-100, &zero_mult, &zero_shift);
    ASSERT_EQ(static_cast<uint64_t>(0), zero_mult);
    ASSERT_EQ(0U, zero_shift);

#if defined(__SIZEOF_INT128__)
    const int64_t freqs[] = {
        19200000LL,    // 19.2 MHz (Qualcomm / ARM generic timer)
        24000000LL,    // 24 MHz (Apple Silicon / ARM generic timer)
        25000000LL,    // 25 MHz (AWS Graviton)
        100000000LL,   // 100 MHz (Ampere Altra)
        1000000000LL,  // 1 GHz (ARMv8.6+ Neoverse)
        2400000000LL,  // 2.4 GHz (x86_64 Xeon)
        3591590000LL,  // ~3.59 GHz (non-round x86_64 TSC)
    };

    for (size_t f_idx = 0; f_idx < arraysize(freqs); ++f_idx) {
        const int64_t freq = freqs[f_idx];
        uint64_t mult = 0;
        uint32_t shift = 0;
        butil::detail::compute_cpu_frequency_scale(freq, &mult, &shift);
        ASSERT_GT(mult, static_cast<uint64_t>(0)) << "freq=" << freq;
        ASSERT_LE(shift, 63U) << "freq=" << freq;

        // 1. Small tick counts [0..10000] + strict monotonicity
        int64_t prev_ns = 0;
        for (uint64_t tsc = 0; tsc <= 10000; ++tsc) {
            const int64_t actual = butil::detail::cycles_to_ns(tsc, mult, shift);
            const int64_t expected = static_cast<int64_t>(
                (static_cast<__uint128_t>(tsc) * 1000000000ULL) / static_cast<uint64_t>(freq));
            ASSERT_NEAR(expected, actual, 1) << "freq=" << freq << " tsc=" << tsc;
            ASSERT_GE(actual, prev_ns) << "freq=" << freq << " tsc=" << tsc;
            prev_ns = actual;
        }

        // 2. 1-second boundary and multi-year uptimes (up to 10^8 seconds ~ 3.17 years)
        const uint64_t sec_anchors[] = {
            1ULL, 60ULL, 3600ULL, 86400ULL, 10000000ULL, 100000000ULL
        };
        for (size_t s_idx = 0; s_idx < arraysize(sec_anchors); ++s_idx) {
            const uint64_t base_tsc = sec_anchors[s_idx] * static_cast<uint64_t>(freq);
            int64_t local_prev = butil::detail::cycles_to_ns(base_tsc - 100, mult, shift);
            for (int64_t offset = -99; offset <= 100; ++offset) {
                const uint64_t tsc = static_cast<uint64_t>(static_cast<int64_t>(base_tsc) + offset);
                const int64_t actual = butil::detail::cycles_to_ns(tsc, mult, shift);
                const int64_t expected = static_cast<int64_t>(
                    (static_cast<__uint128_t>(tsc) * 1000000000ULL) / static_cast<uint64_t>(freq));
                ASSERT_NEAR(expected, actual, 1) << "freq=" << freq << " tsc=" << tsc;
                ASSERT_GE(actual, local_prev) << "freq=" << freq << " tsc=" << tsc;
                local_prev = actual;
            }
        }
    }
#endif
}

TEST(BaiduTimeTest, cpuwide_time_with_cpu_frequency) {
#if BUTIL_USE_CPU_FREQUENCY
    if (butil::detail::invariant_cpu_freq > 0) {
        ASSERT_GT(butil::detail::invariant_cpu_freq_mult, static_cast<uint64_t>(0));
        // 1. Direct cycle-bracket check: regardless of OS scheduling or whether
        // invariant_cpu_freq is CNTFRQ_EL0 (ARM64) or /proc/cpuinfo (x86),
        // cpuwide_time_ns() must lie between the legacy division conversion of
        // two bracketing clock_cycles() reads (within 1ns rounding).
        const uint64_t ufreq = static_cast<uint64_t>(butil::detail::invariant_cpu_freq);
        for (int i = 0; i < 1000; ++i) {
            const uint64_t c0 = butil::detail::clock_cycles();
            const int64_t t_cpu = butil::cpuwide_time_ns();
            const uint64_t c1 = butil::detail::clock_cycles();
            const int64_t ns0 = static_cast<int64_t>(
                (c0 % ufreq) * 1000000000ULL / ufreq + (c0 / ufreq) * 1000000000ULL);
            const int64_t ns1 = static_cast<int64_t>(
                (c1 % ufreq) * 1000000000ULL / ufreq + (c1 / ufreq) * 1000000000ULL);
            ASSERT_GE(t_cpu + 1, ns0);
            ASSERT_LE(t_cpu - 1, ns1);
        }
    }
#endif
    int64_t prev = butil::cpuwide_time_ns();
    for (int i = 0; i < 10000; ++i) {
        const int64_t cur = butil::cpuwide_time_ns();
        ASSERT_GE(cur, prev);
        prev = cur;
    }

    int64_t freq = butil::detail::invariant_cpu_freq;
#if defined(__aarch64__)
    if (freq <= 0) {
        // Exercise the hardware CNTFRQ_EL0 rate even when built with default
        // WITH_CPU_FREQUENCY=OFF.
        __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    }
    ASSERT_GT(freq, 0);
#else
    if (freq <= 0) {
        freq = 24000000LL;
    }
#endif
    uint64_t mult = 0;
    uint32_t shift = 0;
    butil::detail::compute_cpu_frequency_scale(freq, &mult, &shift);

#if defined(__aarch64__)
    // 2. Authoritative hardware timer rate check on AArch64 (works whether
    // WITH_CPU_FREQUENCY is ON or OFF): bracket CNTFRQ_EL0-scaled clock_cycles()
    // with monotonic_time_ns() across a 20ms sleep.
    const int64_t mono_before_start = butil::monotonic_time_ns();
    const int64_t cnt_start_ns =
        butil::detail::cycles_to_ns(butil::detail::clock_cycles(), mult, shift);
    const int64_t mono_after_start = butil::monotonic_time_ns();
    usleep(20000);  // 20ms
    const int64_t mono_before_end = butil::monotonic_time_ns();
    const int64_t cnt_end_ns =
        butil::detail::cycles_to_ns(butil::detail::clock_cycles(), mult, shift);
    const int64_t mono_after_end = butil::monotonic_time_ns();

    const int64_t cnt_elapsed = cnt_end_ns - cnt_start_ns;
    const int64_t min_mono_elapsed = mono_before_end - mono_after_start;
    const int64_t max_mono_elapsed = mono_after_end - mono_before_start;
    EXPECT_GE(cnt_elapsed + 1000000L, min_mono_elapsed);
    EXPECT_LE(cnt_elapsed - 1000000L, max_mono_elapsed);
#endif

#if defined(__aarch64__) || defined(__x86_64__) || defined(__amd64__) || defined(__loongarch64)
    const uint64_t tsc_now = butil::detail::clock_cycles();
    const uint64_t ufreq = static_cast<uint64_t>(freq);
    const int64_t div_ns = static_cast<int64_t>(
        (tsc_now % ufreq) * 1000000000ULL / ufreq + (tsc_now / ufreq) * 1000000000ULL);
    const int64_t mul_ns = butil::detail::cycles_to_ns(tsc_now, mult, shift);
    ASSERT_NEAR(div_ns, mul_ns, 1);
#endif

    // 3. Dependent-chain microbenchmark of 2x 64-bit udiv vs 128-bit mul+shift
    // using volatile runtime parameters so the compiler cannot constant-fold.
    const size_t N = 200000;
    volatile uint64_t vfreq = static_cast<uint64_t>(freq);
    volatile uint64_t vmult = mult;
    volatile uint32_t vshift = shift;
    uint64_t dep = butil::detail::clock_cycles();
    butil::Timer t_div, t_mul;
    t_div.start();
    for (size_t i = 0; i < N; ++i) {
        const uint64_t tsc = (dep & 1ULL) + i + 1000000ULL;
        const uint64_t f = vfreq;
        const uint64_t sec = tsc / f;
        const uint64_t remain = tsc % f;
        dep = remain * 1000000000ULL / f + sec * 1000000000ULL;
    }
    t_div.stop();

    t_mul.start();
    for (size_t i = 0; i < N; ++i) {
        const uint64_t tsc = (dep & 1ULL) + i + 1000000ULL;
        dep = static_cast<uint64_t>(butil::detail::cycles_to_ns(tsc, vmult, vshift));
    }
    t_mul.stop();
    printf("2x 64-bit udiv takes %" PRId64 "ns, 128-bit mul-shift takes %" PRId64 "ns (dep=%" PRIu64 ")\n",
           t_div.n_elapsed() / static_cast<int64_t>(N),
           t_mul.n_elapsed() / static_cast<int64_t>(N), dep);
}

} // namespace
