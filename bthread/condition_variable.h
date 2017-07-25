// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/12/14 21:26:26

#ifndef  BTHREAD_CONDITION_VARIABLE_H
#define  BTHREAD_CONDITION_VARIABLE_H

#include "base/time.h"
#include "bthread/mutex.h"

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
        return wait_until(lock, base::microseconds_from_now(timeout_us));
    }

    int wait_for(std::unique_lock<bthread_mutex_t>& lock,
                 long timeout_us) {
        return wait_until(lock, base::microseconds_from_now(timeout_us));
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
