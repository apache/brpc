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

#include "bthread/rwlock.h"
#include "bthread/butex.h"

namespace bthread {

// A `bthread_rwlock_t' is a reader/writer mutual exclusion lock,
// which is a bthread implementation of golang RWMutex.
// For details, see https://github.com/golang/go/blob/master/src/sync/rwmutex.go
// The lock can be held by an arbitrary number of readers or a single writer.

// It is enough for readers. If the reader exceeds this value,
// need to use `int64_t' instead of `int'.
const int RWLockMaxReaders = 1 << 30;

// For reading.
static int rwlock_rdlock_impl(bthread_rwlock_t* __restrict rwlock,
                              const struct timespec* __restrict abstime) {
    int reader_count = ((butil::atomic<int>*)&rwlock->reader_count)
        ->fetch_add(1, butil::memory_order_acquire) + 1;
    // Fast path.
    if (reader_count >= 0) {
        CHECK_LT(reader_count, RWLockMaxReaders);
        return 0;
    }

    // Slow path.
    return bthread_sem_timedwait(&rwlock->reader_sema, abstime);
}

static inline int rwlock_rdlock(bthread_rwlock_t* rwlock) {
    return rwlock_rdlock_impl(rwlock, NULL);
}

static inline int rwlock_timedrdlock(bthread_rwlock_t* __restrict rwlock,
                                     const struct timespec* __restrict abstime) {
    return rwlock_rdlock_impl(rwlock, abstime);
}

// Returns 0 if the lock was acquired, otherwise errno.
static  inline int rwlock_tryrdlock(bthread_rwlock_t* rwlock) {
    while (true) {
        int reader_count = ((butil::atomic<int>*)&rwlock->reader_count)
            ->load(butil::memory_order_relaxed);
        if (reader_count < 0) {
            return EBUSY;
        }
        if (((butil::atomic<int>*)&rwlock->reader_count)
                ->compare_exchange_weak(reader_count, reader_count + 1)) {
            return 0;
        }
    }
}

static inline int rwlock_unrdlock(bthread_rwlock_t* rwlock) {
    int reader_count = ((butil::atomic<int>*)&rwlock->reader_count)
        ->fetch_add(-1, butil::memory_order_relaxed) - 1;
    // Fast path.
    if (reader_count >= 0) {
        return 0;
    }
    // Slow path.

    if (reader_count + 1 == 0 || reader_count + 1 == -RWLockMaxReaders) {
        CHECK(false) << "rwlock_unrdlock of unlocked rwlock";
        return EINVAL;
    }

    // A writer is pending.
    int reader_wait = ((butil::atomic<int>*)&rwlock->reader_wait)
        ->fetch_add(-1, butil::memory_order_relaxed) - 1;
    if (reader_wait != 0) {
        return 0;
    }
    // The last reader unblocks the writer.
    return bthread_sem_post(&rwlock->writer_sema);
}

// For writing.
static inline int rwlock_wrlock_impl(bthread_rwlock_t* __restrict rwlock,
                                     const struct timespec* __restrict abstime) {
    // First, resolve competition with other writers.
    int rc = bthread_mutex_trylock(&rwlock->write_queue_mutex);
    if (0 != rc) {
        rc = bthread_mutex_timedlock(&rwlock->write_queue_mutex, abstime);
        if (0 != rc) {
            return rc;
        }
    }

    // Announce to readers there is a pending writer.
    int reader_count = ((butil::atomic<int>*)&rwlock->reader_count)
        ->fetch_add(-RWLockMaxReaders, butil::memory_order_release);
    // Wait for active readers.
    if (reader_count != 0 &&
        ((butil::atomic<int>*)&rwlock->reader_wait)
            ->fetch_add(reader_count) + reader_count != 0) {
        rc = bthread_sem_trywait(&rwlock->writer_sema);
        if (0 != rc) {
            rc = bthread_sem_timedwait(&rwlock->writer_sema, abstime);
            if (0 != rc) {
                bthread_mutex_unlock(&rwlock->write_queue_mutex);
                return rc;
            }
        }
    }
    rwlock->wlock_flag = true;
    return 0;
}

static inline int rwlock_wrlock(bthread_rwlock_t* rwlock) {
    return rwlock_wrlock_impl(rwlock, NULL);
}

static inline int rwlock_timedwrlock(bthread_rwlock_t* __restrict rwlock,
                                     const struct timespec* __restrict abstime) {
    return rwlock_wrlock_impl(rwlock, abstime);
}

static inline int rwlock_trywrlock(bthread_rwlock_t* rwlock) {
    int rc = bthread_mutex_trylock(&rwlock->write_queue_mutex);
    if (0 != rc) {
        return rc;
    }

    int expected = 0;
    if (!((butil::atomic<int>*)&rwlock->reader_count)
            ->compare_exchange_strong(expected, -RWLockMaxReaders)) {
        bthread_mutex_unlock(&rwlock->write_queue_mutex);
        return EBUSY;
    }
    rwlock->wlock_flag = true;

    return 0;
}

static inline void rwlock_unwrlock_slow(bthread_rwlock_t* rwlock, int reader_count) {
    bthread_sem_post_n(&rwlock->reader_sema, reader_count);
    // Allow other writers to proceed.
    bthread_mutex_unlock(&rwlock->write_queue_mutex);
}

static inline int rwlock_unwrlock(bthread_rwlock_t* rwlock) {
    rwlock->wlock_flag = false;

    // Announce to readers there is no active writer.
    int reader_count = ((butil::atomic<int>*)&rwlock->reader_count)->fetch_add(
        RWLockMaxReaders, butil::memory_order_release) + RWLockMaxReaders;
    if (reader_count >= RWLockMaxReaders) {
        CHECK(false) << "rwlock_unwlock of unlocked rwlock";
        return EINVAL;
    }

    rwlock_unwrlock_slow(rwlock, reader_count);
    return 0;
}

static inline int rwlock_unlock(bthread_rwlock_t* rwlock) {
    if (rwlock->wlock_flag) {
        return rwlock_unwrlock(rwlock);
    } else {
        return rwlock_unrdlock(rwlock);
    }
}

} // namespace bthread

__BEGIN_DECLS

int bthread_rwlock_init(bthread_rwlock_t* __restrict rwlock,
                        const bthread_rwlockattr_t* __restrict) {
    bthread_sem_init(&rwlock->reader_sema, 0);
    bthread_sem_init(&rwlock->writer_sema, 0);

    rwlock->reader_count = 0;
    rwlock->reader_wait = 0;
    rwlock->wlock_flag = false;

    bthread_mutex_init(&rwlock->write_queue_mutex, NULL);

    return 0;
}

int bthread_rwlock_destroy(bthread_rwlock_t* rwlock) {
    bthread_sem_destroy(&rwlock->reader_sema);
    bthread_sem_destroy(&rwlock->writer_sema);
    bthread_mutex_destroy(&rwlock->write_queue_mutex);
    return 0;
}

int bthread_rwlock_rdlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_rdlock(rwlock);
}

int bthread_rwlock_tryrdlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_tryrdlock(rwlock);
}

int bthread_rwlock_timedrdlock(bthread_rwlock_t* __restrict rwlock,
                               const struct timespec* __restrict abstime) {
    return bthread::rwlock_timedrdlock(rwlock, abstime);
}

int bthread_rwlock_wrlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_wrlock(rwlock);
}

int bthread_rwlock_trywrlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_trywrlock(rwlock);
}

int bthread_rwlock_timedwrlock(bthread_rwlock_t* __restrict rwlock,
                               const struct timespec* __restrict abstime) {
    return bthread::rwlock_timedwrlock(rwlock, abstime);
}

int bthread_rwlock_unlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_unlock(rwlock);
}

__END_DECLS
