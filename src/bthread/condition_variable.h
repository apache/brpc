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
// Date: 2015/12/14 21:26:26

#ifndef  BTHREAD_CONDITION_VARIABLE_H
#define  BTHREAD_CONDITION_VARIABLE_H

#include "butil/macros.h"

#ifdef BUTIL_CXX11_ENABLED
#include <condition_variable>
#include <memory>
#include <mutex>
#include <limits>
#include <system_error>
#endif // BUTIL_CXX11_ENABLED

#include "butil/time.h"
#include "bthread/mtx_cv_base.h"

namespace bthread {

// Deprecated in favor of bthread::ConditionVariable2 which has a consistent interface with
// std::condition_variable
class ConditionVariable {
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);

public:
    typedef bthread_cond_t* native_handler_type;

    ConditionVariable() {
        CHECK_EQ(0, bthread_cond_init(&_cond, NULL));
    }

    ~ConditionVariable() {
        CHECK_EQ(0, bthread_cond_destroy(&_cond));
    }

    native_handler_type native_handler() { return &_cond; }

    void wait(std::unique_lock<bthread::Mutex>& lock) {
        bthread_cond_wait(&_cond, lock.mutex()->native_handler());
    }

    void wait(std::unique_lock<bthread_mutex_t>& lock) {
        bthread_cond_wait(&_cond, lock.mutex());
    }

    // Unlike std::condition_variable, we return ETIMEDOUT when time expires
    // rather than std::timeout
    int wait_for(std::unique_lock<bthread::Mutex>& lock,
                 long timeout_us) {
        return wait_until(lock, butil::microseconds_from_now(timeout_us));
    }

    int wait_for(std::unique_lock<bthread_mutex_t>& lock,
                 long timeout_us) {
        return wait_until(lock, butil::microseconds_from_now(timeout_us));
    }

    int wait_until(std::unique_lock<bthread::Mutex>& lock,
                   timespec duetime) {
        const int rc = bthread_cond_timedwait(
                &_cond, lock.mutex()->native_handler(), &duetime);
        return rc == ETIMEDOUT ? ETIMEDOUT : 0;
    }

    int wait_until(std::unique_lock<bthread_mutex_t>& lock,
                   timespec duetime) {
        const int rc = bthread_cond_timedwait(
                &_cond, lock.mutex(), &duetime);
        return rc == ETIMEDOUT ? ETIMEDOUT : 0;
    }

    void notify_one() {
        bthread_cond_signal(&_cond);
    }

    void notify_all() {
        bthread_cond_broadcast(&_cond);
    }

private:
    bthread_cond_t _cond;
};

#ifdef BUTIL_CXX11_ENABLED

class ConditionVariableAny {
public:
    DISALLOW_COPY_AND_ASSIGN(ConditionVariableAny);

    ConditionVariableAny() : _internal_mtx(std::make_shared<bthread::Mutex>()), _cv() {
    }

    ~ConditionVariableAny() = default;

    void notify_one();

    void notify_all();

    template<typename Lock>
    void wait(Lock& lock);

    template<typename Lock, typename Pred>
    void wait(Lock& lock, Pred pred) {
        while (!pred()) {
            wait(lock);
        }
    }

    template<typename Lock, typename Rep, typename Period>
    std::cv_status wait_for(Lock& lock, const std::chrono::duration<Rep, Period>& rel_time) {
        return wait_until(lock, std::chrono::steady_clock::now() + rel_time);
    }

    template<typename Lock, typename Rep, typename Period, typename Pred>
    bool wait_for(Lock& lock,
                  const std::chrono::duration<Rep, Period>& rel_time,
                  Pred pred) {
        return wait_until(lock, std::chrono::steady_clock::now() + rel_time, std::move(pred));
    }

    template<typename Lock, typename Clock, typename Duration>
    std::cv_status wait_until(Lock& lock,
                              const std::chrono::time_point<Clock, Duration>& timeout_time);

    template<typename Lock, typename Clock, typename Duration, typename Pred>
    bool wait_until(Lock& lock,
                    const std::chrono::time_point<Clock, Duration>& timeout_time,
                    Pred pred);

private:
    std::shared_ptr<bthread::Mutex> _internal_mtx;
    bthread::ConditionVariable2 _cv;
};

namespace detail {

template<typename Lock>
struct LockExternalFunctor {
    void operator()(Lock* operand) {
        operand->lock();
    }
};

} // namespace detail

template<typename Lock>
void ConditionVariableAny::wait(Lock& lock) {
    std::shared_ptr<bthread::Mutex> internal_mtx_shared(_internal_mtx);
    std::unique_lock<bthread::Mutex> ilock(*internal_mtx_shared);
    lock.unlock();
    std::unique_ptr<Lock, detail::LockExternalFunctor<Lock>> then_relock_lock_guard(&lock);
    std::lock_guard<std::unique_lock<bthread::Mutex>> unlock_ilock_first_guard(
            ilock, std::adopt_lock);
    _cv.wait(ilock);
}

template<typename Lock, typename Clock, typename Duration>
std::cv_status
ConditionVariableAny::wait_until(Lock& lock,
                                   const std::chrono::time_point<Clock, Duration>& timeout_time) {
    std::shared_ptr<bthread::Mutex> internal_mtx_shared(_internal_mtx);
    std::unique_lock<bthread::Mutex> ilock(*internal_mtx_shared);
    lock.unlock();
    std::unique_ptr<Lock, detail::LockExternalFunctor<Lock>> then_relock_lock_guard(&lock);
    std::lock_guard<std::unique_lock<bthread::Mutex>> unlock_ilock_first_guard(
            ilock, std::adopt_lock);
    return _cv.wait_until(ilock, timeout_time);
}

template<typename Lock, typename Clock, typename Duration, typename Pred>
bool ConditionVariableAny::wait_until(Lock& lock,
                                        const std::chrono::time_point<Clock, Duration>& timeout_time,
                                        Pred pred) {
    while (!pred()) {
        if (wait_until(lock, timeout_time) == std::cv_status::timeout) {
            return pred();
        }
    }
    return true;
}

#endif // BUTIL_CXX11_ENABLED

}  // namespace bthread

#endif  //BTHREAD_CONDITION_VARIABLE_H
