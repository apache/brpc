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

#include "bthread/types.h"
#include "bthread/bthread.h"
#include "butil/scoped_lock.h"

namespace bthread {

// The C++ Wrapper of bthread_rwlock

// NOTE: Not aligned to cacheline as the container of RWLock is practically aligned.

class RWLock {
public:
    typedef bthread_rwlock_t* native_handler_type;

    RWLock() {
        int rc = bthread_rwlock_init(&_rwlock, NULL);
        if (rc) {
            throw std::system_error(std::error_code(rc, std::system_category()),
                                    "RWLock constructor failed");
        }
    }

    ~RWLock() {
        CHECK_EQ(0, bthread_rwlock_destroy(&_rwlock));
    }

    DISALLOW_COPY_AND_ASSIGN(RWLock);

    native_handler_type native_handler() { return &_rwlock; }

    void rdlock() {
        int rc = bthread_rwlock_rdlock(&_rwlock);
        if (rc) {
            throw std::system_error(std::error_code(rc, std::system_category()),
                                    "RWLock rdlock failed");
        }
    }

    bool try_rdlock() {
        return 0 == bthread_rwlock_tryrdlock(&_rwlock);
    }

    bool timed_rdlock(const struct timespec* abstime) {
        return 0 == bthread_rwlock_timedrdlock(&_rwlock, abstime);
    }

    void wrlock() {
        int rc = bthread_rwlock_wrlock(&_rwlock);
        if (rc) {
            throw std::system_error(std::error_code(rc, std::system_category()),
                                    "RWLock wrlock failed");
        }
    }

    bool try_wrlock() {
        return 0 == bthread_rwlock_trywrlock(&_rwlock);
    }

    bool timed_wrlock(const struct timespec* abstime) {
        return 0 == bthread_rwlock_timedwrlock(&_rwlock, abstime);
    }

    void unlock() { bthread_rwlock_unlock(&_rwlock); }

private:
    bthread_rwlock_t _rwlock{};
};

// Read lock guard of rwlock.
class RWLockRdGuard {
public:
    explicit RWLockRdGuard(bthread_rwlock_t& rwlock)
        : _rwlock(&rwlock) {
#if !defined(NDEBUG)
        const int rc = bthread_rwlock_rdlock(_rwlock);
        if (rc) {
            LOG(FATAL) << "Fail to rdlock bthread_rwlock_t=" << _rwlock << ", " << berror(rc);
            _rwlock = NULL;
        }
#else
        bthread_rwlock_rdlock(_rwlock);
#endif // NDEBUG
    }

    explicit RWLockRdGuard(RWLock& rwlock)
        : RWLockRdGuard(*rwlock.native_handler()) {}

    ~RWLockRdGuard() {
#ifndef NDEBUG
        if (NULL != _rwlock) {
            bthread_rwlock_unlock(_rwlock);
        }
#else
        bthread_rwlock_unlock(_rwlock);
#endif // NDEBUG
    }

    DISALLOW_COPY_AND_ASSIGN(RWLockRdGuard);

private:
    bthread_rwlock_t* _rwlock;
};

// Write lock guard of rwlock.
class RWLockWrGuard {
public:
    explicit RWLockWrGuard(bthread_rwlock_t& rwlock)
        : _rwlock(&rwlock) {
#if !defined(NDEBUG)
        const int rc = bthread_rwlock_wrlock(_rwlock);
        if (rc) {
            LOG(FATAL) << "Fail to wrlock bthread_rwlock_t=" << _rwlock << ", " << berror(rc);
            _rwlock = NULL;
        }
#else
        bthread_rwlock_wrlock(_rwlock);
#endif // NDEBUG
    }

    explicit RWLockWrGuard(RWLock& rwlock)
        : RWLockWrGuard(*rwlock.native_handler()) {}

    ~RWLockWrGuard() {
#ifndef NDEBUG
        if (NULL != _rwlock) {
            bthread_rwlock_unlock(_rwlock);
        }
#else
        bthread_rwlock_unlock(_rwlock);
#endif // NDEBUG
    }

    DISALLOW_COPY_AND_ASSIGN(RWLockWrGuard);

private:
    bthread_rwlock_t* _rwlock;
};

} // namespace bthread

namespace std {

template <>
class lock_guard<bthread_rwlock_t> {
public:
    lock_guard(bthread_rwlock_t& rwlock, bool read)
        :_rwlock(&rwlock), _read(read) {
#if !defined(NDEBUG)
        int rc;
        if (_read) {
            rc = bthread_rwlock_rdlock(_rwlock);
        } else {
            rc = bthread_rwlock_wrlock(_rwlock);
        }
        if (rc) {
            LOG(FATAL) << "Fail to lock bthread_rwlock_t=" << _rwlock << ", " << berror(rc);
            _rwlock = NULL;
        }
#else
        if (_read) {
            bthread_rwlock_rdlock(_rwlock);
        } else {
            bthread_rwlock_wrlock(_rwlock);
        }
#endif // NDEBUG
    }

    ~lock_guard() {
#ifndef NDEBUG
        if (NULL != _rwlock) {
            bthread_rwlock_unlock(_rwlock);
        }
#else
        bthread_rwlock_unlock(_rwlock);
#endif // NDEBUG
    }

    DISALLOW_COPY_AND_ASSIGN(lock_guard);

private:
    bthread_rwlock_t* _rwlock;
    bool _read;
};

template <>
class lock_guard<bthread::RWLock> {
public:
    lock_guard(bthread::RWLock& rwlock, bool read)
        :_rwlock_guard(*rwlock.native_handler(), read) {}

    DISALLOW_COPY_AND_ASSIGN(lock_guard);

private:
    std::lock_guard<bthread_rwlock_t> _rwlock_guard;
};

} // namespace std
