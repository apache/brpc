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

#include "butil/memory/scope_guard.h"
#include "bvar/collector.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"

namespace bthread {

// Define in bthread/mutex.cpp
class ContentionProfiler;
extern ContentionProfiler* g_cp;
extern bvar::CollectorSpeedLimit g_cp_sl;
extern bool is_contention_site_valid(const bthread_contention_site_t& cs);
extern void make_contention_site_invalid(bthread_contention_site_t* cs);
extern void submit_contention(const bthread_contention_site_t& csite, int64_t now_ns);

static inline int bthread_sem_trywait(bthread_sem_t* sema) {
    auto whole = (butil::atomic<unsigned>*)sema->butex;
    while (true) {
        unsigned num = whole->load(butil::memory_order_relaxed);
        if (num == 0) {
            return EAGAIN;
        }
        if (whole->compare_exchange_weak(num, num - 1,
                                         butil::memory_order_acquire,
                                         butil::memory_order_relaxed)) {
            return 0;
        }
    }

}

static int bthread_sem_wait_impl(bthread_sem_t* sem, const struct timespec* abstime) {
    bool queue_lifo = false;
    bool first_wait = true;
    size_t sampling_range = bvar::INVALID_SAMPLING_RANGE;
    // -1: don't sample.
    // 0: default value.
    // > 0: Start time of sampling.
    int64_t start_ns = 0;
    auto whole = (butil::atomic<unsigned>*)sem->butex;
    while (true) {
        unsigned num = whole->load(butil::memory_order_relaxed);
        if (num > 0) {
            if (whole->compare_exchange_weak(num, num - 1,
                                             butil::memory_order_acquire,
                                             butil::memory_order_relaxed)) {
                if (start_ns > 0) {
                    const int64_t end_ns = butil::cpuwide_time_ns();
                    const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
                    bthread::submit_contention(csite, end_ns);
                }

                return 0;
            }
        }
        // Don't sample when contention profiler is off.
        if (NULL != bthread::g_cp && start_ns == 0 && sem->enable_csite &&
            !bvar::is_sampling_range_valid(sampling_range)) {
            // Ask Collector if this (contended) sem waiting should be sampled.
            sampling_range = bvar::is_collectable(&bthread::g_cp_sl);
            start_ns = bvar::is_sampling_range_valid(sampling_range) ?
                       butil::cpuwide_time_ns() : -1;
        } else {
            start_ns = -1;
        }
        if (bthread::butex_wait(sem->butex, 0, abstime, queue_lifo) < 0 &&
            errno != EWOULDBLOCK && errno != EINTR) {
            // A sema should ignore interruptions in general since
            // user code is unlikely to check the return value.
            if (ETIMEDOUT == errno && start_ns > 0) {
                // Failed to lock due to ETIMEDOUT, submit the elapse directly.
                const int64_t end_ns = butil::cpuwide_time_ns();
                const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
                bthread::submit_contention(csite, end_ns);
            }

            return errno;
        }
        // Ignore EWOULDBLOCK and EINTR.
        if (first_wait && 0 == errno) {
            first_wait = false;
        }
        if (!first_wait) {
            // Normally, bthreads are queued in FIFO order. But competing with new
            // arriving bthreads over sema, a woken up bthread has good chances of
            // losing. Because new arriving bthreads are already running on CPU and
            // there can be lots of them. In such case, for fairness, to avoid
            // starvation, it is queued at the head of the waiter queue.
            queue_lifo = true;
        }
    }
}

static inline int bthread_sem_post(bthread_sem_t* sem, size_t num) {
    if (num > 0) {
        unsigned n = ((butil::atomic<unsigned>*)sem->butex)
            ->fetch_add(num, butil::memory_order_relaxed);
        const size_t sampling_range = NULL != bthread::g_cp && sem->enable_csite ?
            bvar::is_collectable(&bthread::g_cp_sl) : bvar::INVALID_SAMPLING_RANGE;
        const int64_t start_ns = bvar::is_sampling_range_valid(sampling_range) ?
                                 butil::cpuwide_time_ns() : -1;
        bthread::butex_wake_n(sem->butex, n);
        if (start_ns > 0) {
            const int64_t end_ns = butil::cpuwide_time_ns();
            const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
            bthread::submit_contention(csite, end_ns);
        }
    }
    return 0;
}

} // namespace bthread

__BEGIN_DECLS

int bthread_sem_init(bthread_sem_t* sem, unsigned value) {
    sem->butex = bthread::butex_create_checked<unsigned>();
    if (!sem->butex) {
        return ENOMEM;
    }
    *sem->butex = value;
    sem->enable_csite = true;
    return 0;
}

int bthread_sem_disable_csite(bthread_sem_t* sema) {
    sema->enable_csite = false;
    return 0;
}

int bthread_sem_destroy(bthread_sem_t* semaphore) {
    bthread::butex_destroy(semaphore->butex);
    return 0;
}

int bthread_sem_trywait(bthread_sem_t* sem) {
    return bthread::bthread_sem_trywait(sem);
}

int bthread_sem_wait(bthread_sem_t* sem) {
    return bthread::bthread_sem_wait_impl(sem, NULL);
}

int bthread_sem_timedwait(bthread_sem_t* sem, const struct timespec* abstime) {
    return bthread::bthread_sem_wait_impl(sem, abstime);
}

int bthread_sem_post(bthread_sem_t* sem) {
    return bthread::bthread_sem_post(sem, 1);
}

int bthread_sem_post_n(bthread_sem_t* sem, size_t n) {
    return bthread::bthread_sem_post(sem, n);
}

__END_DECLS