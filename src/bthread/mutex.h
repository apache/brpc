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

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/12/14 18:17:04

#ifndef  BTHREAD_MUTEX_H
#define  BTHREAD_MUTEX_H

#include "bthread/mtx_cv_base.h"
#include "bthread/types.h"
#include "butil/macros.h"
#include "butil/scoped_lock.h"
#include "bvar/utils/lock_timer.h"
#ifdef BUTIL_CXX11_ENABLED
#include <chrono>
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

// Higher level mutex constructs for C++

namespace bthread {

class TimedMutex {

public:

    DISALLOW_COPY_AND_ASSIGN(TimedMutex);

    TimedMutex() = default;

    ~TimedMutex() {
        std::lock_guard<Mutex> lg(_mtx);
    }

    void lock();

    void unlock();

    bool try_lock();

    template<typename Rep, typename Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& rel_time);

    template<typename Clock, typename Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time);

private:
    Mutex _mtx;
    bool _locked{false};
    ::bthread::ConditionVariable _cv;
};

template<typename Rep, typename Period>
bool TimedMutex::try_lock_for(const std::chrono::duration<Rep, Period>& rel_time) {
    return TimedMutex::try_lock_until(std::chrono::steady_clock::now() + rel_time);
}

template<typename Clock, typename Duration>
bool TimedMutex::try_lock_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
    std::unique_lock<Mutex> lock(_mtx);
    while(_locked) {
        if(Clock::now() >= timeout_time) {
            break;
        }
        _cv.wait_until(lock, timeout_time);
    }
    if (!_locked) {
        _locked = true;
        return true;
    }
    return false;
}

} // namespace bthread

#endif // BUTIL_CXX11_ENABLED

#endif  //BTHREAD_MUTEX_H
