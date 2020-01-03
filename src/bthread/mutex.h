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

// bthread - A M:N threading library to make applications more concurrent.

// Date: 2015/12/14 18:17:04

#ifndef  BTHREAD_MUTEX_H
#define  BTHREAD_MUTEX_H

#include "bthread/types.h"
#include "butil/scoped_lock.h"
#include "bvar/utils/lock_timer.h"

__BEGIN_DECLS
extern int bthread_mutex_init(bthread_mutex_t* __restrict mutex,
                              const bthread_mutexattr_t* __restrict mutex_attr);
extern int bthread_mutex_destroy(bthread_mutex_t* mutex);
extern int bthread_mutex_trylock(bthread_mutex_t* mutex);
extern int bthread_mutex_lock(bthread_mutex_t* mutex);
extern int bthread_mutex_timedlock(bthread_mutex_t* __restrict mutex,
                                   const struct timespec* __restrict abstime);
extern int bthread_mutex_unlock(bthread_mutex_t* mutex);
__END_DECLS

namespace bthread {

// The C++ Wrapper of bthread_mutex

// NOTE: Not aligned to cacheline as the container of Mutex is practically aligned
class Mutex {
public:
    typedef bthread_mutex_t* native_handler_type;
    Mutex() {
        int ec = bthread_mutex_init(&_mutex, NULL);
        if (ec != 0) {
            throw std::system_error(std::error_code(ec, std::system_category()), "Mutex constructor failed");
        }
    }
    ~Mutex() { CHECK_EQ(0, bthread_mutex_destroy(&_mutex)); }
    native_handler_type native_handler() { return &_mutex; }
    void lock() {
        int ec = bthread_mutex_lock(&_mutex);
        if (ec != 0) {
            throw std::system_error(std::error_code(ec, std::system_category()), "Mutex lock failed");
        }
    }
    void unlock() { bthread_mutex_unlock(&_mutex); }
    bool try_lock() { return !bthread_mutex_trylock(&_mutex); }
    // TODO(chenzhangyi01): Complement interfaces for C++11
private:
    DISALLOW_COPY_AND_ASSIGN(Mutex);
    bthread_mutex_t _mutex;   
};

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

// Specialize std::lock_guard and std::unique_lock for bthread_mutex_t

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

#endif  //BTHREAD_MUTEX_H
