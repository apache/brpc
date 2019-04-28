// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)
//          Shuo Zang (jasonszang@126.com)
// Date: 2015/12/14 18:17:0

#ifndef  BTHREAD_MUTEX_H
#define  BTHREAD_MUTEX_H

#include "bthread/mtx_cv_base.h"
#include "bthread/types.h"
#include "butil/macros.h"
#include "butil/scoped_lock.h"
#include "bvar/utils/lock_timer.h"
#ifdef BUTIL_CXX11_ENABLED
#include <chrono>
#include <limits>
#include <thread>
#include "bthread/bthreadxx.h"
#endif

namespace bthread {

namespace internal {
#ifdef BTHREAD_USE_FAST_PTHREAD_MUTEX
class FastPthreadMutex {
public:
    FastPthreadMutex() : _futex(0) {}
    ~FastPthreadMutex() {}
    void lock();
    void unlock();
    bool try_lock();
private:
    DISALLOW_COPY_AND_ASSIGN(FastPthreadMutex);
    int lock_contended();
    unsigned _futex;
};
#else
typedef butil::Mutex FastPthreadMutex;
#endif
}

}  // namespace bthread

namespace bvar {

template <>
struct MutexConstructor<bthread_mutex_t> {
    bool operator()(bthread_mutex_t* mutex) const { 
        return bthread_mutex_init(mutex, NULL) == 0;
    }
};

template <>
struct MutexDestructor<bthread_mutex_t> {
    bool operator()(bthread_mutex_t* mutex) const { 
        return bthread_mutex_destroy(mutex) == 0;
    }
};

}  // namespace bvar

#ifdef BUTIL_CXX11_ENABLED

namespace bthread {

// The bthread equivalent of std::timed_mutex.
class TimedMutex {

public:
    using native_handle_type = bthread_mutex_t*;

    DISALLOW_COPY_AND_ASSIGN(TimedMutex);

    TimedMutex();

    ~TimedMutex() { CHECK_EQ(0, bthread_mutex_destroy(&_mutex)); }

    void lock();

    void unlock() { bthread_mutex_unlock(&_mutex); }

    bool try_lock() { return !bthread_mutex_trylock(&_mutex); }

    template<typename Rep, typename Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& rel_time);

    template<typename Clock, typename Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time);

private:
    bthread_mutex_t _mutex;
};

template<typename Rep, typename Period>
bool TimedMutex::try_lock_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return TimedMutex::try_lock_until(std::chrono::steady_clock::now() + rel_time);
}

template<typename Clock, typename Duration>
bool TimedMutex::try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
    auto dur = timeout_time - Clock::now();
    auto sys_timeout = std::chrono::time_point_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now() + dur);
    auto nanos_since_epoch = sys_timeout.time_since_epoch();
    auto secs_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(nanos_since_epoch);
    auto max_timespec_secs = std::numeric_limits<decltype(timespec::tv_sec)>::max();
    timespec sp{};
    if (secs_since_epoch.count() < max_timespec_secs) {
        sp.tv_sec = secs_since_epoch.count();
        sp.tv_nsec = static_cast<decltype(sp.tv_nsec)>(
                (nanos_since_epoch - secs_since_epoch).count());
    } else {
        sp.tv_sec = max_timespec_secs;
        sp.tv_nsec = 999999999;
    }
    return !bthread_mutex_timedlock(&_mutex, &sp);
}

class RecursiveTimedMutex;

namespace detail {

constexpr const static BThread::id NOT_A_BTHREAD_ID{};

class RecursiveMutexBase {
public:
    DISALLOW_COPY_AND_ASSIGN(RecursiveMutexBase);

    RecursiveMutexBase() : _counter(0) {
    }

    ~RecursiveMutexBase() {
        std::lock_guard<Mutex> lock(_mtx);
    }

    void lock();

    void unlock();

    bool try_lock();

    friend class ::bthread::RecursiveTimedMutex;

private:

    bool available() noexcept;

    void setup_ownership() noexcept;

    Mutex _mtx;
    ConditionVariable _cv;
    int _counter;
    BThread::id _owner_bthread_id; // Valid only if owner is a bthread
    std::thread::id _owner_std_thread_id; // Valid only if owner is a std thread / pthread
};

}

// The bthread equivalent of std::recursive_mutex.
// This is a higher level construct that is not directly supported by native bthread APIs.
class RecursiveMutex : public detail::RecursiveMutexBase {
};

// The bthread equivalent of std::recursive_timed_mutex.
// This is also a higher level construct not directly supported by native bthread APIs.
class RecursiveTimedMutex : public detail::RecursiveMutexBase {
public:
    template<class Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& rel_time);

    template<typename Clock, typename Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time);
};

template<typename Rep, typename Period>
bool RecursiveTimedMutex::try_lock_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return RecursiveTimedMutex::try_lock_until(std::chrono::steady_clock::now() + rel_time);
}

template<typename Clock, typename Duration>
bool
RecursiveTimedMutex::try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
    std::unique_lock<Mutex> lock(_mtx);
    while(!available()) {
        if(Clock::now() >= timeout_time) {
            break;
        }
        _cv.wait_until(lock, timeout_time);
    }
    if (available()) {
        setup_ownership();
        return true;
    }
    return false;
}

} // namespace bthread

#endif // BUTIL_CXX11_ENABLED

#endif  //BTHREAD_MUTEX_H
