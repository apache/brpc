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

// Several basic thread synchronization constructs are extracted into this base header so that
// other higher level constructs can depend on them without introducing circular header
// dependencies.

#ifndef BTHREAD_MTX_CV_BASE_H
#define BTHREAD_MTX_CV_BASE_H

#include "bthread/types.h"
#include "butil/macros.h"
#include "butil/errno.h"
#ifdef BUTIL_CXX11_ENABLED
#include <chrono>
#include <condition_variable>
#include <mutex>
#endif

__BEGIN_DECLS
extern int bthread_mutex_init(bthread_mutex_t* __restrict mutex,
                              const bthread_mutexattr_t* __restrict mutex_attr);
extern int bthread_mutex_destroy(bthread_mutex_t* mutex);
extern int bthread_mutex_trylock(bthread_mutex_t* mutex);
extern int bthread_mutex_lock(bthread_mutex_t* mutex);
extern int bthread_mutex_timedlock(bthread_mutex_t* __restrict mutex,
                                   const struct timespec* __restrict abstime);
extern int bthread_mutex_unlock(bthread_mutex_t* mutex);

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

// The C++ Wrapper of bthread_mutex

// NOTE: Not aligned to cacheline as the container of Mutex is practically aligned
class Mutex {
public:
    // Maybe this is a typo? Anyway it should be deprecated
    typedef bthread_mutex_t* native_handler_type;
    typedef bthread_mutex_t* native_handle_type;

    // Note: Unlike std::mutex, bthread mutex constructor is not constexpr. Use std::mutex for
    // synchronization during static initialization.
    Mutex() {
        int ec = bthread_mutex_init(&_mutex, NULL);
        if (ec != 0) {
            throw std::system_error(std::error_code(ec, std::system_category()),
                                    "Mutex constructor failed");
        }
    }

    ~Mutex() { CHECK_EQ(0, bthread_mutex_destroy(&_mutex)); }

    native_handler_type native_handler() { return &_mutex; } // also typo?
    native_handle_type native_handle() { return &_mutex; }

    void lock() {
        int ec = bthread_mutex_lock(&_mutex);
        if (ec != 0) {
            throw std::system_error(std::error_code(ec, std::system_category()),
                                    "Mutex lock failed");
        }
    }

    void unlock() { bthread_mutex_unlock(&_mutex); }

    bool try_lock() { return !bthread_mutex_trylock(&_mutex); }

#ifdef BUTIL_CXX11_ENABLED
    DISALLOW_COPY_AND_ASSIGN(Mutex);
private:
#else
    private:
        DISALLOW_COPY_AND_ASSIGN(Mutex);
#endif
    bthread_mutex_t _mutex;
};

} // namespace bthread

// Specialize std::lock_guard and std::unique_lock for bthread_mutex_t

// NOTE:
// Technically these specializations invoke Undefined Behaviour for both pre- and post-C++11.
// For pre-C++11 these are adding new classes to namespace std.
// For post-C++11 the specialization of unique_lock is not MoveConstructible, thus
// does not satisfy the original requirements for std::unique_lock.
// Both are prohibited by the C++ standard.
// It would be good if these can be deprecated and later removed for the sake of correctness.
// We should use std::unique_lock<bthread::Mutex> instead, with no need for explicit specialization.

namespace std {

template <> class lock_guard<bthread_mutex_t> {
public:
    explicit lock_guard(bthread_mutex_t & mutex) : _pmutex(&mutex) {
#if !defined(NDEBUG)
        const int rc = bthread_mutex_lock(_pmutex);
        if (rc) {
            LOG(FATAL) << "Fail to lock bthread_mutex_t=" << _pmutex << ", " << berror(rc);
            _pmutex = NULL;
        }
#else
        bthread_mutex_lock(_pmutex);
#endif  // NDEBUG
    }

    ~lock_guard() {
#ifndef NDEBUG
        if (_pmutex) {
            bthread_mutex_unlock(_pmutex);
        }
#else
        bthread_mutex_unlock(_pmutex);
#endif
    }

private:
    DISALLOW_COPY_AND_ASSIGN(lock_guard);
    bthread_mutex_t* _pmutex;
};

template <> class unique_lock<bthread_mutex_t> {
    DISALLOW_COPY_AND_ASSIGN(unique_lock);
public:
    typedef bthread_mutex_t         mutex_type;
    unique_lock() : _mutex(NULL), _owns_lock(false) {}
    explicit unique_lock(mutex_type& mutex)
        : _mutex(&mutex), _owns_lock(false) {
        lock();
    }
    unique_lock(mutex_type& mutex, defer_lock_t)
        : _mutex(&mutex), _owns_lock(false)
    {}
    unique_lock(mutex_type& mutex, try_to_lock_t)
        : _mutex(&mutex), _owns_lock(bthread_mutex_trylock(&mutex) == 0)
    {}
    unique_lock(mutex_type& mutex, adopt_lock_t)
        : _mutex(&mutex), _owns_lock(true)
    {}

    ~unique_lock() {
        if (_owns_lock) {
            unlock();
        }
    }

    void lock() {
        if (!_mutex) {
            CHECK(false) << "Invalid operation";
            return;
        }
        if (_owns_lock) {
            CHECK(false) << "Detected deadlock issue";
            return;
        }
        bthread_mutex_lock(_mutex);
        _owns_lock = true;
    }

    bool try_lock() {
        if (!_mutex) {
            CHECK(false) << "Invalid operation";
            return false;
        }
        if (_owns_lock) {
            CHECK(false) << "Detected deadlock issue";
            return false;
        }
        _owns_lock = !bthread_mutex_trylock(_mutex);
        return _owns_lock;
    }

    void unlock() {
        if (!_owns_lock) {
            CHECK(false) << "Invalid operation";
            return;
        }
        if (_mutex) {
            bthread_mutex_unlock(_mutex);
            _owns_lock = false;
        }
    }

    void swap(unique_lock& rhs) {
        std::swap(_mutex, rhs._mutex);
        std::swap(_owns_lock, rhs._owns_lock);
    }

    mutex_type* release() {
        mutex_type* saved_mutex = _mutex;
        _mutex = NULL;
        _owns_lock = false;
        return saved_mutex;
    }

    mutex_type* mutex() { return _mutex; }
    bool owns_lock() const { return _owns_lock; }
    operator bool() const { return owns_lock(); }

private:
    mutex_type*                     _mutex;
    bool                            _owns_lock;
};

}  // namespace std

namespace bthread {

// The bthread equivalent of std::condition_variable. The interface matches that of
// std::condition_variable with several extra non-standard-compatible functions and members
// for historical reasons.
class ConditionVariable {

public:

    typedef bthread_cond_t* native_handle_type;

    ConditionVariable() {
        CHECK_EQ(0, bthread_cond_init(&_cond, nullptr));
    }

    ~ConditionVariable() {
        CHECK_EQ(0, bthread_cond_destroy(&_cond));
    }

    native_handle_type native_handle() {
        return &_cond;
    }

    void notify_one() BUTIL_NOEXCEPT {
        bthread_cond_signal(&_cond);
    }

    void notify_all() BUTIL_NOEXCEPT {
        bthread_cond_broadcast(&_cond);
    }

    void wait(std::unique_lock<bthread::Mutex>& lock) BUTIL_NOEXCEPT;

    template<typename Pred>
    void wait(std::unique_lock<bthread::Mutex>& lock, Pred pred) {
        while (!pred()) {
            wait(lock);
        }
    }

#ifdef BUTIL_CXX11_ENABLED

    template<typename Rep, typename Period>
    std::cv_status wait_for(std::unique_lock<bthread::Mutex>& lock,
                            const std::chrono::duration<Rep, Period>& rel_time);

    template<typename Rep, typename Period, typename Pred>
    bool wait_for(std::unique_lock<bthread::Mutex>& lock,
                  const std::chrono::duration<Rep, Period>& rel_time,
                  Pred pred) {
        return wait_until(lock, std::chrono::steady_clock::now() + rel_time, std::move(pred));
    }

    template<typename Clock, typename Duration>
    std::cv_status wait_until(std::unique_lock<bthread::Mutex>& lock,
                              const std::chrono::time_point<Clock, Duration>& timeout_time) {
        wait_for(lock, timeout_time - Clock::now());
        return Clock::now() < timeout_time ? std::cv_status::no_timeout : std::cv_status::timeout;
    }

    template<typename Clock, typename Duration, typename Pred>
    bool wait_until(std::unique_lock<bthread::Mutex>& lock,
                    const std::chrono::time_point<Clock, Duration>& timeout_time,
                    Pred pred);

#endif // BUTIL_CXX11_ENABLED

    // These are the original non-standard-compatible interfaces
    typedef bthread_cond_t* native_handler_type;
    native_handler_type native_handler() { return &_cond; }
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
    // End of non-standard-compatible interfaces

#ifdef BUTIL_CXX11_ENABLED
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
private:
#else // BUTIL_CXX11_ENABLED
private:
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
#endif // BUTIL_CXX11_ENABLED

#ifdef BUTIL_CXX11_ENABLED
    void do_timed_wait(std::unique_lock<bthread::Mutex>& lock,
                       const std::chrono::time_point<std::chrono::system_clock,
                               std::chrono::nanoseconds>& tp) noexcept;

    template<typename Rep, typename Period>
    std::chrono::nanoseconds ceil_nanoseconds(const std::chrono::duration<Rep, Period>& dur);
#endif // BUTIL_CXX11_ENABLED

    bthread_cond_t _cond{};
};

template<typename Rep, typename Period>
std::cv_status ConditionVariable::wait_for(std::unique_lock<bthread::Mutex>& lock,
                                            const std::chrono::duration<Rep, Period>& rel_time) {
    if (rel_time < rel_time.zero()) {
        return std::cv_status::timeout;
    }
    auto sys_now = std::chrono::system_clock::now();
    auto steady_now = std::chrono::steady_clock::now();
    if (sys_now.max() - rel_time > sys_now) {
        do_timed_wait(lock, sys_now + ceil_nanoseconds(rel_time));
    } else {
        do_timed_wait(lock, sys_now.max());
    }
    if (std::chrono::steady_clock::now() - steady_now < rel_time) {
        return std::cv_status::no_timeout;
    } else {
        return std::cv_status::timeout;
    }
}

template<typename Clock, typename Duration, typename Pred>
bool ConditionVariable::wait_until(std::unique_lock<bthread::Mutex>& lock,
                                    const std::chrono::time_point<Clock, Duration>& timeout_time,
                                    Pred pred) {
    while (!pred()) {
        if (wait_until(lock, timeout_time) == std::cv_status::timeout) {
            return pred();
        }
    }
    return true;
}

template<typename Rep, typename Period>
std::chrono::nanoseconds
ConditionVariable::ceil_nanoseconds(const std::chrono::duration<Rep, Period>& dur) {
    std::chrono::nanoseconds result = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
    if (result < dur) {
        ++result;
    }
    return result;
}

} // namespace bthread

#endif // BTHREAD_MTX_CV_BASE_H
