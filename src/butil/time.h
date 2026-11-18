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

// Date: Wed Aug 11 10:38:17 2010

// Measuring time

#ifndef BUTIL_BAIDU_TIME_H
#define BUTIL_BAIDU_TIME_H

#include <time.h>                            // timespec, clock_gettime
#include <sys/time.h>                        // timeval, gettimeofday
#include <stdint.h>                          // int64_t, uint64_t

#if defined(NO_CLOCK_GETTIME_IN_MAC)
#include <mach/mach.h>
# define CLOCK_REALTIME CALENDAR_CLOCK
# define CLOCK_MONOTONIC SYSTEM_CLOCK

typedef int clockid_t;

// clock_gettime is not available in MacOS < 10.12
int clock_gettime(clockid_t id, timespec* time);

#endif

namespace butil {

// Get SVN revision of this copy.
const char* last_changed_revision();

// ----------------------
// timespec manipulations
// ----------------------

// Let tm->tv_nsec be in [0, 1,000,000,000) if it's not.
inline void timespec_normalize(timespec* tm) {
    if (tm->tv_nsec >= 1000000000L) {
        const int64_t added_sec = tm->tv_nsec / 1000000000L;
        tm->tv_sec += added_sec;
        tm->tv_nsec -= added_sec * 1000000000L;
    } else if (tm->tv_nsec < 0) {
        const int64_t sub_sec = (tm->tv_nsec - 999999999L) / 1000000000L;
        tm->tv_sec += sub_sec;
        tm->tv_nsec -= sub_sec * 1000000000L;
    }
}

// Add timespec |span| into timespec |*tm|.
inline void timespec_add(timespec *tm, const timespec& span) {
    tm->tv_sec += span.tv_sec;
    tm->tv_nsec += span.tv_nsec;
    timespec_normalize(tm);
}

// Minus timespec |span| from timespec |*tm|. 
// tm->tv_nsec will be inside [0, 1,000,000,000)
inline void timespec_minus(timespec *tm, const timespec& span) {
    tm->tv_sec -= span.tv_sec;
    tm->tv_nsec -= span.tv_nsec;
    timespec_normalize(tm);
}

// ------------------------------------------------------------------
// Get the timespec after specified duration from |start_time|
// ------------------------------------------------------------------
inline timespec nanoseconds_from(timespec start_time, int64_t nanoseconds) {
    start_time.tv_nsec += nanoseconds;
    timespec_normalize(&start_time);
    return start_time;
}

inline timespec microseconds_from(timespec start_time, int64_t microseconds) {
    return nanoseconds_from(start_time, microseconds * 1000L);
}

inline timespec milliseconds_from(timespec start_time, int64_t milliseconds) {
    return nanoseconds_from(start_time, milliseconds * 1000000L);
}

inline timespec seconds_from(timespec start_time, int64_t seconds) {
    return nanoseconds_from(start_time, seconds * 1000000000L);
}

// --------------------------------------------------------------------
// Get the timespec after specified duration from now (CLOCK_REALTIME)
// --------------------------------------------------------------------
inline timespec nanoseconds_from_now(int64_t nanoseconds) {
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return nanoseconds_from(time, nanoseconds);
}

inline timespec microseconds_from_now(int64_t microseconds) {
    return nanoseconds_from_now(microseconds * 1000L);
}

inline timespec milliseconds_from_now(int64_t milliseconds) {
    return nanoseconds_from_now(milliseconds * 1000000L);
}

inline timespec seconds_from_now(int64_t seconds) {
    return nanoseconds_from_now(seconds * 1000000000L);
}

inline timespec timespec_from_now(const timespec& span) {
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    timespec_add(&time, span);
    return time;
}

// ---------------------------------------------------------------------
// Convert timespec to and from a single integer.
// For conversions between timespec and timeval, use TIMEVAL_TO_TIMESPEC
// and TIMESPEC_TO_TIMEVAL defined in <sys/time.h>
// ---------------------------------------------------------------------1
inline int64_t timespec_to_nanoseconds(const timespec& ts) {
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

inline int64_t timespec_to_microseconds(const timespec& ts) {
    return timespec_to_nanoseconds(ts) / 1000L;
}

inline int64_t timespec_to_milliseconds(const timespec& ts) {
    return timespec_to_nanoseconds(ts) / 1000000L;
}

inline int64_t timespec_to_seconds(const timespec& ts) {
    return timespec_to_nanoseconds(ts) / 1000000000L;
}

inline timespec nanoseconds_to_timespec(int64_t ns) {
    timespec ts;
    ts.tv_sec = ns / 1000000000L;
    ts.tv_nsec = ns - ts.tv_sec * 1000000000L;
    return ts;
}

inline timespec microseconds_to_timespec(int64_t us) {
    return nanoseconds_to_timespec(us * 1000L);
}

inline timespec milliseconds_to_timespec(int64_t ms) {
    return nanoseconds_to_timespec(ms * 1000000L);
}

inline timespec seconds_to_timespec(int64_t s) {
    return nanoseconds_to_timespec(s * 1000000000L);
}

// ---------------------------------------------------------------------
// Convert timeval to and from a single integer.                                             
// For conversions between timespec and timeval, use TIMEVAL_TO_TIMESPEC
// and TIMESPEC_TO_TIMEVAL defined in <sys/time.h>
// ---------------------------------------------------------------------
inline int64_t timeval_to_microseconds(const timeval& tv) {
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

inline int64_t timeval_to_milliseconds(const timeval& tv) {
    return timeval_to_microseconds(tv) / 1000L;
}

inline int64_t timeval_to_seconds(const timeval& tv) {
    return timeval_to_microseconds(tv) / 1000000L;
}

inline timeval microseconds_to_timeval(int64_t us) {
    timeval tv;
    tv.tv_sec = us / 1000000L;
    tv.tv_usec = us - tv.tv_sec * 1000000L;
    return tv;
}

inline timeval milliseconds_to_timeval(int64_t ms) {
    return microseconds_to_timeval(ms * 1000L);
}

inline timeval seconds_to_timeval(int64_t s) {
    return microseconds_to_timeval(s * 1000000L);
}

// ---------------------------------------------------------------
// Get system-wide monotonic time.
// ---------------------------------------------------------------
extern int64_t monotonic_time_ns();

inline int64_t monotonic_time_us() { 
    return monotonic_time_ns() / 1000L; 
}

inline int64_t monotonic_time_ms() {
    return monotonic_time_ns() / 1000000L; 
}

inline int64_t monotonic_time_s() {
    return monotonic_time_ns() / 1000000000L;
}

namespace detail {
inline uint64_t clock_cycles() {
#if defined(__x86_64__) || defined(__amd64__)
    unsigned int lo = 0;
    unsigned int hi = 0;
    // We cannot use "=A", since this would use %rax on x86_64
    __asm__ __volatile__ (
        "rdtsc"
        : "=a" (lo), "=d" (hi)
        );
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t virtual_timer_value;
    asm volatile("isb\n\t"
                 "mrs %0, cntvct_el0"
                 : "=r"(virtual_timer_value)
                 :
                 : "memory");
    return virtual_timer_value;
#elif defined(__ARM_ARCH)
  #if (__ARM_ARCH >= 6)
    unsigned int pmccntr;
    unsigned int pmuseren;
    unsigned int pmcntenset;
    // Read the user mode perf monitor counter access permissions.
    asm volatile ("mrc p15, 0, %0, c9, c14, 0" : "=r" (pmuseren));
    if (pmuseren & 1) {  // Allows reading perfmon counters for user mode code.
        asm volatile ("mrc p15, 0, %0, c9, c12, 1" : "=r" (pmcntenset));
        if (pmcntenset & 0x80000000ul) {  // Is it counting?
            asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r" (pmccntr));
            // The counter is set up to count every 64th cycle
            return static_cast<uint64_t>(pmccntr) * 64;  // Should optimize to << 6
        }
    }
  #else
    #error "unsupported arm_arch"
  #endif
#elif defined(__loongarch64)
    uint64_t stable_counter;
    uint64_t counter_id;
    __asm__ __volatile__ (
        "rdtime.d %1, %0"
        : "=r" (stable_counter), "=r" (counter_id)
        );
    return stable_counter;
#elif defined(__riscv)
    uint64_t cycles;
    __asm__ __volatile__ (
        "rdcycle %0"
        : "=r" (cycles)
        );
    return cycles;
#else
  #error "unsupported arch"
#endif
}

// Precompute fixed-point multiplier and right-shift so that
// cycles_to_ns(cycles, mult, shift) == (cycles * 10^9) / freq without runtime
// 64-bit integer divisions.
inline void compute_cpu_frequency_scale(int64_t freq, uint64_t* mult, uint32_t* shift) {
    if (freq <= 0) {
        *mult = 0;
        *shift = 0;
        return;
    }
    const uint64_t ufreq = static_cast<uint64_t>(freq);
#if defined(__SIZEOF_INT128__)
    uint32_t s = 63;
    while (s > 0) {
        __uint128_t num = (static_cast<__uint128_t>(1000000000ULL) << s) + (ufreq - 1);
        __uint128_t m = num / ufreq;
        if (m <= UINT64_MAX) {
            *mult = static_cast<uint64_t>(m);
            *shift = s;
            return;
        }
        --s;
    }
    *mult = (1000000000ULL + ufreq - 1) / ufreq;
    *shift = 0;
#else
    *mult = ufreq;
    *shift = 0;
#endif
}

inline int64_t cycles_to_ns(uint64_t cycles, uint64_t mult, uint32_t shift) {
#if defined(__SIZEOF_INT128__)
    return static_cast<int64_t>((static_cast<__uint128_t>(cycles) * mult) >> shift);
#else
    (void)shift;
    if (mult == 0) {
        return 0;
    }
    const uint64_t sec = cycles / mult;
    const uint64_t remain = cycles % mult;
    return static_cast<int64_t>(remain * 1000000000ULL / mult + sec * 1000000000ULL);
#endif
}

// Be positive iff:
// 1 Intel x86_64 CPU (multiple cores) supporting constant_tsc and
// nonstop_tsc(check flags in /proc/cpuinfo)
// 2 aarch64, where the counter frequency is read from CNTFRQ_EL0.
// Only computed when BUTIL_USE_CPU_FREQUENCY is enabled.
extern int64_t invariant_cpu_freq;
extern uint64_t invariant_cpu_freq_mult;
extern uint32_t invariant_cpu_freq_shift;
}  // namespace detail

// ---------------------------------------------------------------
// Get cpu-wide (wall-) time.
// Cost ~9ns on Intel(R) Xeon(R) CPU E5620 @ 2.40GHz
// ---------------------------------------------------------------
// note: Inlining shortens time cost per-call for 15ns in a loop of many
//       calls to this function.
inline int64_t cpuwide_time_ns() {
#if !defined(BAIDU_INTERNAL) && !BUTIL_USE_CPU_FREQUENCY
    // nearly impossible to get the correct invariant cpu frequency on
    // different CPU and machines. CPU-ID rarely works and frequencies
    // in "model name" and "cpu Mhz" are both unreliable.
    // Since clock_gettime() in newer glibc/kernel is much faster(~30ns)
    // which is closer to the previous impl. of cpuwide_time(~10ns), we
    // simply use the monotonic time to get rid of all related issues.
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000L + now.tv_nsec;
#else
    int64_t cpu_freq = detail::invariant_cpu_freq;
    if (cpu_freq > 0) {
        const uint64_t tsc = detail::clock_cycles();
        return detail::cycles_to_ns(tsc, detail::invariant_cpu_freq_mult,
                                    detail::invariant_cpu_freq_shift);
    } else {
        // Lack of necessary features, return system-wide monotonic time instead.
        return monotonic_time_ns();
    }
#endif // defined(BAIDU_INTERNAL) || BUTIL_USE_CPU_FREQUENCY
}

// Get cpu clock time of the current thread in nanoseconds without the time spent in blocking I/O operations.
// Cost ~200ns
inline int64_t cputhread_time_ns() {
    timespec now;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return now.tv_sec * 1000000000L + now.tv_nsec;
}

inline int64_t cpuwide_time_us() {
    return cpuwide_time_ns() / 1000L;
}

inline int64_t cpuwide_time_ms() { 
    return cpuwide_time_ns() / 1000000L;
}

inline int64_t cpuwide_time_s() {
    return cpuwide_time_ns() / 1000000000L;
}

// --------------------------------------------------------------------
// Get elapse since the Epoch.                                          
// No gettimeofday_ns() because resolution of timeval is microseconds.  
// Cost ~40ns on 2.6.32_1-12-0-0, Intel(R) Xeon(R) CPU E5620 @ 2.40GHz
// --------------------------------------------------------------------
inline int64_t gettimeofday_us() {
    timeval now;
    gettimeofday(&now, nullptr);
    return now.tv_sec * 1000000L + now.tv_usec;
}

inline int64_t gettimeofday_ms() {
    return gettimeofday_us() / 1000L;
}

inline int64_t gettimeofday_s() {
    return gettimeofday_us() / 1000000L;
}

// ----------------------------------------
// Control frequency of operations.
// ----------------------------------------
// Example:
//   EveryManyUS every_1s(1000000L);
//   while (1) {
//       ...
//       if (every_1s) {
//           // be here at most once per second
//       }
//   }
class EveryManyUS {
public:
    explicit EveryManyUS(int64_t interval_us)
        : _last_time_us(cpuwide_time_us())
        , _interval_us(interval_us) {}
    
    operator bool() {
        const int64_t now_us = cpuwide_time_us();
        if (now_us < _last_time_us + _interval_us) {
            return false;
        }
        _last_time_us = now_us;
        return true;
    }

private:
    int64_t _last_time_us;
    const int64_t _interval_us;
};

// ---------------
//  Count elapses
// ---------------
class Timer {
public:

    enum TimerType {
        STARTED,
    };

    Timer() : _stop(0), _start(0) {}
    explicit Timer(const TimerType) : Timer() {
        start();
    }

    // Start this timer
    void start() {
        _start = cpuwide_time_ns();
        _stop = _start;
    }
    
    // Stop this timer
    void stop() {
        _stop = cpuwide_time_ns();
    }

    // Get the elapse from start() to stop(), in various units.
    int64_t n_elapsed() const { return _stop - _start; }
    int64_t u_elapsed() const { return n_elapsed() / 1000L; }
    int64_t m_elapsed() const { return u_elapsed() / 1000L; }
    int64_t s_elapsed() const { return m_elapsed() / 1000L; }

    double n_elapsed(double) const { return (double)(_stop - _start); }
    double u_elapsed(double) const { return (double)n_elapsed() / 1000.0; }
    double m_elapsed(double) const { return (double)u_elapsed() / 1000.0; }
    double s_elapsed(double) const { return (double)m_elapsed() / 1000.0; }
    
private:
    int64_t _stop;
    int64_t _start;
};

}  // namespace butil

#endif  // BUTIL_BAIDU_TIME_H
