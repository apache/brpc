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

#include "bvar/collector.h"
#include "bthread/rwlock.h"
#include "bthread/butex.h"

namespace bthread {

// A `bthread_rwlock_t' is a reader/writer mutual exclusion lock,
// which is a bthread implementation of golang RWMutex.
// The lock can be held by an arbitrary number of readers or a single writer.
// For details, see https://github.com/golang/go/blob/master/src/sync/rwmutex.go

// Define in bthread/mutex.cpp
class ContentionProfiler;
extern ContentionProfiler* g_cp;
extern bvar::CollectorSpeedLimit g_cp_sl;
extern bool is_contention_site_valid(const bthread_contention_site_t& cs);
extern void make_contention_site_invalid(bthread_contention_site_t* cs);
extern void submit_contention(const bthread_contention_site_t& csite, int64_t now_ns);

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

    // Don't sample when contention profiler is off.
    if (NULL == bthread::g_cp) {
        return bthread_sem_timedwait(&rwlock->reader_sema, abstime);
    }
    // Ask Collector if this (contended) locking should be sampled.
    const size_t sampling_range = bvar::is_collectable(&bthread::g_cp_sl);
    if (!bvar::is_sampling_range_valid(sampling_range)) { // Don't sample.
        return bthread_sem_timedwait(&rwlock->reader_sema, abstime);
    }

    // Sample.
    const int64_t start_ns = butil::cpuwide_time_ns();
    int rc = bthread_sem_timedwait(&rwlock->reader_sema, abstime);
    const int64_t end_ns = butil::cpuwide_time_ns();
    const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
    // Submit `csite' for each reader immediately after
    // owning rdlock to avoid the contention of `csite'.
    bthread::submit_contention(csite, end_ns);

    return rc;
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
            // Failed to acquire the read lock because there is a writer.
            return EBUSY;
        }
        if (((butil::atomic<int>*)&rwlock->reader_count)
                ->compare_exchange_weak(reader_count, reader_count + 1,
                                        butil::memory_order_acquire,
                                        butil::memory_order_relaxed)) {
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

    if (BAIDU_UNLIKELY(reader_count + 1 == 0 || reader_count + 1 == -RWLockMaxReaders)) {
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

    if (NULL == bthread::g_cp) {
        bthread_sem_post(&rwlock->writer_sema);
        return 0;
    }
    // Ask Collector if this (contended) locking should be sampled.
    const size_t sampling_range = bvar::is_collectable(&bthread::g_cp_sl);
    if (!sampling_range) { // Don't sample
        bthread_sem_post(&rwlock->writer_sema);
        return 0;
    }

    // Sampling.
    const int64_t start_ns = butil::cpuwide_time_ns();
    bthread_sem_post(&rwlock->writer_sema);
    const int64_t end_ns = butil::cpuwide_time_ns();
    const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
    // Submit `csite' for each reader immediately after
    // releasing rdlock to avoid the contention of `csite'.
    bthread::submit_contention(csite, end_ns);
    return 0;
}

#define DO_CSITE_IF_NEED                                                              \
    do {                                                                              \
        /* Don't sample when contention profiler is off. */                           \
        if (NULL != bthread::g_cp) {                                                  \
            /* Ask Collector if this (contended) locking should be sampled. */        \
            sampling_range = bvar::is_collectable(&bthread::g_cp_sl);                 \
            start_ns = bvar::is_sampling_range_valid(sampling_range) ?                \
                butil::cpuwide_time_ns() : -1;                                        \
        } else {                                                                      \
            start_ns = -1;                                                            \
        }                                                                             \
    } while (0)

#define SUBMIT_CSITE_IF_NEED                                                          \
    do {                                                                              \
        if (ETIMEDOUT == rc && start_ns > 0) {                                        \
            /* Failed to lock due to ETIMEDOUT, submit the elapse directly. */        \
            const int64_t end_ns = butil::cpuwide_time_ns();                          \
            const bthread_contention_site_t csite{end_ns - start_ns, sampling_range}; \
            bthread::submit_contention(csite, end_ns);                                \
        }                                                                             \
    } while (0)

// For writing.
static inline int rwlock_wrlock_impl(bthread_rwlock_t* __restrict rwlock,
                                     const struct timespec* __restrict abstime) {
    // First, resolve competition with other writers.
    int rc = bthread_mutex_trylock(&rwlock->write_queue_mutex);
    size_t sampling_range = bvar::INVALID_SAMPLING_RANGE;
    // -1: don't sample.
    // 0: default value.
    // > 0: Start time of sampling.
    int64_t start_ns = 0;
    if (0 != rc) {
        DO_CSITE_IF_NEED;

        rc = bthread_mutex_timedlock(&rwlock->write_queue_mutex, abstime);
        if (0 != rc) {
            SUBMIT_CSITE_IF_NEED;
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
            if (0 == start_ns) {
                DO_CSITE_IF_NEED;
            }

            rc = bthread_sem_timedwait(&rwlock->writer_sema, abstime);
            if (0 != rc) {
                SUBMIT_CSITE_IF_NEED;
                bthread_mutex_unlock(&rwlock->write_queue_mutex);
                return rc;
            }
        }
    }
    if (start_ns > 0) {
        rwlock->writer_csite.duration_ns = butil::cpuwide_time_ns() - start_ns;
        rwlock->writer_csite.sampling_range = sampling_range;
    }
    rwlock->wlock_flag = true;
    return 0;
}
#undef DO_CSITE_IF_NEED
#undef SUBMIT_CSITE_IF_NEED

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
            ->compare_exchange_strong(expected, -RWLockMaxReaders,
                                      butil::memory_order_acquire,
                                      butil::memory_order_relaxed)) {
        // Failed to acquire the write lock because there are active readers.
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
    if (BAIDU_UNLIKELY(reader_count >= RWLockMaxReaders)) {
        CHECK(false) << "rwlock_unwlock of unlocked rwlock";
        return EINVAL;
    }

    bool is_valid = bthread::is_contention_site_valid(rwlock->writer_csite);
    if (BAIDU_UNLIKELY(is_valid)) {
        bthread_contention_site_t saved_csite = rwlock->writer_csite;
        bthread::make_contention_site_invalid(&rwlock->writer_csite);

        const int64_t unlock_start_ns = butil::cpuwide_time_ns();
        rwlock_unwrlock_slow(rwlock, reader_count);
        const int64_t unlock_end_ns = butil::cpuwide_time_ns();
        saved_csite.duration_ns += unlock_end_ns - unlock_start_ns;
        bthread::submit_contention(saved_csite, unlock_end_ns);
    } else {
        rwlock_unwrlock_slow(rwlock, reader_count);
    }

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
    int rc = bthread_sem_init(&rwlock->reader_sema, 0);
    if (BAIDU_UNLIKELY(0 != rc)) {
        return rc;
    }
    bthread_sem_disable_csite(&rwlock->reader_sema);
    rc = bthread_sem_init(&rwlock->writer_sema, 0);
    if (BAIDU_UNLIKELY(0 != rc)) {
        bthread_sem_destroy(&rwlock->reader_sema);
        return rc;
    }
    bthread_sem_disable_csite(&rwlock->writer_sema);

    rwlock->reader_count = 0;
    rwlock->reader_wait = 0;
    rwlock->wlock_flag = false;

    bthread_mutexattr_t attr;
    bthread_mutexattr_init(&attr);
    bthread_mutexattr_disable_csite(&attr);
    rc = bthread_mutex_init(&rwlock->write_queue_mutex, &attr);
    if (BAIDU_UNLIKELY(0 != rc)) {
        bthread_sem_destroy(&rwlock->reader_sema);
        bthread_sem_destroy(&rwlock->writer_sema);
        return rc;
    }
    bthread_mutexattr_destroy(&attr);

    bthread::make_contention_site_invalid(&rwlock->writer_csite);

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
