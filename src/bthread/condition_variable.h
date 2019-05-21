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

#ifdef BUTIL_CXX11_ENABLED

// The bthread equivalent of std::condition_variable_any that works with types of Lockables
// other than std::unique_lock<bthread::Mutex>.
// This is a higher level construct that is not directly supported by native bthread APIs.
// Note that just as std::condition_variable_any, ConditionVariableAny has slightly worse
// performance than bthread::Thread. And if you use system level thread mutexes with
// bthread::ConditionVariableAny you will block the underlying thread.
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
    bthread::ConditionVariable _cv;
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
