// Copyright (c) 2010 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Wed Aug 11 10:38:17 2010

// Measuring time

#ifndef BAIDU_BASE_TIME_H
#define BAIDU_BASE_TIME_H

#include <time.h>                            // timespec, clock_gettime
#include <sys/time.h>                        // timeval, gettimeofday
#include <stdint.h>                          // int64_t, uint64_t

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
// Cost ~85ns on 2.6.32_1-12-0-0, Intel(R) Xeon(R) CPU E5620 @ 2.40GHz
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
    unsigned int lo = 0;
    unsigned int hi = 0;
    // We cannot use "=A", since this would use %rax on x86_64
    __asm__ __volatile__ (
        "rdtsc"
        : "=a" (lo), "=d" (hi)
        );
    return ((uint64_t)hi << 32) | lo;
}
}  // namespace detail

// ---------------------------------------------------------------
// Get cpu-wide (wall-) time.
// Cost ~9ns on Intel(R) Xeon(R) CPU E5620 @ 2.40GHz
// ---------------------------------------------------------------
inline int64_t cpuwide_time_ns() {
    extern const uint64_t invariant_cpu_freq;  // will be non-zero iff:
    // 1 Intel x86_64 CPU (multiple cores) supporting constant_tsc and
    // nonstop_tsc(check flags in /proc/cpuinfo)
    
    if (invariant_cpu_freq) {
        const uint64_t tsc = detail::clock_cycles();
        const uint64_t sec = tsc / invariant_cpu_freq;
        // TODO: should be OK until CPU's frequency exceeds 16GHz.
        return (tsc - sec * invariant_cpu_freq) * 1000000000L /
            invariant_cpu_freq + sec * 1000000000L;
    }
    // Lack of necessary features, return system-wide monotonic time instead.
    return monotonic_time_ns();
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
    gettimeofday(&now, NULL);
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
    explicit Timer(const TimerType) {
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

// NOTE: Don't call fast_realtime*! they're still experimental.
inline int64_t fast_realtime_ns() {
    extern const uint64_t invariant_cpu_freq;
    extern __thread int64_t tls_cpuwidetime_ns;
    extern __thread int64_t tls_realtime_ns;
    
    if (invariant_cpu_freq) {
        // 1 Intel x86_64 CPU (multiple cores) supporting constant_tsc and
        // nonstop_tsc(check flags in /proc/cpuinfo)
    
        const uint64_t tsc = detail::clock_cycles();
        const uint64_t sec = tsc / invariant_cpu_freq;
        // TODO: should be OK until CPU's frequency exceeds 16GHz.
        const int64_t diff = (tsc - sec * invariant_cpu_freq) * 1000000000L /
            invariant_cpu_freq + sec * 1000000000L - tls_cpuwidetime_ns;
        if (__builtin_expect(diff < 10000000, 1)) {
            return diff + tls_realtime_ns;
        }
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        tls_cpuwidetime_ns += diff;
        tls_realtime_ns = timespec_to_nanoseconds(ts);
        return tls_realtime_ns;
    }
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return timespec_to_nanoseconds(ts);
}

inline int fast_realtime(timespec* ts) {
    extern const uint64_t invariant_cpu_freq;
    extern __thread int64_t tls_cpuwidetime_ns;
    extern __thread int64_t tls_realtime_ns;
    
    if (invariant_cpu_freq) {    
        const uint64_t tsc = detail::clock_cycles();
        const uint64_t sec = tsc / invariant_cpu_freq;
        // TODO: should be OK until CPU's frequency exceeds 16GHz.
        const int64_t diff = (tsc - sec * invariant_cpu_freq) * 1000000000L /
            invariant_cpu_freq + sec * 1000000000L - tls_cpuwidetime_ns;
        if (__builtin_expect(diff < 10000000, 1)) {
            const int64_t now = diff + tls_realtime_ns;
            ts->tv_sec = now / 1000000000L;
            ts->tv_nsec = now - ts->tv_sec * 1000000000L;
            return 0;
        }
        const int rc = clock_gettime(CLOCK_REALTIME, ts);
        tls_cpuwidetime_ns += diff;
        tls_realtime_ns = timespec_to_nanoseconds(*ts);
        return rc;
    }
    return clock_gettime(CLOCK_REALTIME, ts);
}

}  // namespace butil

#endif  // BAIDU_BASE_TIME_H
