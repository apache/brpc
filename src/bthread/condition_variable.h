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

#include "butil/time.h"
#include "bthread/mutex.h"

__BEGIN_DECLS
extern int bthread_cond_init(bthread_cond_t* __restrict cond,
                             const bthread_condattr_t* __restrict cond_attr);
extern int bthread_cond_destroy(bthread_cond_t* cond);
extern int bthread_cond_signal(bthread_cond_t* cond);
extern int bthread_cond_broadcast(bthread_cond_t* cond);
extern int bthread_cond_wait(bthread_cond_t* __restrict cond,
                             bthread_mutex_t* __restrict mutex);
extern int bthread_cond_timedwait(
    bthread_cond_t* __restrict cond,
    bthread_mutex_t* __restrict mutex,
    const struct timespec* __restrict abstime);
__END_DECLS

namespace bthread {

class ConditionVariable {
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
public:
    typedef bthread_cond_t*         native_handler_type;
    
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
    bthread_cond_t                  _cond;
};

}  // namespace bthread

#endif  //BTHREAD_CONDITION_VARIABLE_H
