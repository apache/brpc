// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Aug  3 12:46:15 CST 2014

#include "base/atomicops.h"
#include "base/macros.h"                         // BAIDU_CASSERT
#include "bthread/butex.h"                       // butex_*
#include "bthread/types.h"                       // bthread_cond_t

namespace bthread {
struct CondInternal {
    base::atomic<bthread_mutex_t*> m;
    base::atomic<int>* seq;
};

BAIDU_CASSERT(sizeof(CondInternal) == sizeof(bthread_cond_t),
              sizeof_innercond_must_equal_cond);
BAIDU_CASSERT(offsetof(CondInternal, m) == offsetof(bthread_cond_t, m),
              offsetof_cond_mutex_must_equal);
BAIDU_CASSERT(offsetof(CondInternal, seq) ==
              offsetof(bthread_cond_t, seq),
              offsetof_cond_seq_must_equal);
}

extern "C" {

extern int bthread_mutex_unlock(bthread_mutex_t*);
extern int bthread_mutex_lock_contended(bthread_mutex_t*);

int bthread_cond_init(bthread_cond_t* __restrict c,
                      const bthread_condattr_t*) {
    c->m = NULL;
    c->seq = bthread::butex_create_checked<int>();
    *c->seq = 0;
    return 0;
}

int bthread_cond_destroy(bthread_cond_t* c) {
    bthread::butex_destroy(c->seq);
    c->seq = NULL;
    return 0;
}

int bthread_cond_signal(bthread_cond_t* c) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    // ic is probably dereferenced after fetch_add, save required fields before
    // this point
    base::atomic<int>* const saved_seq = ic->seq;
    saved_seq->fetch_add(1, base::memory_order_release);
    // don't touch ic any more
    bthread::butex_wake(saved_seq);
    return 0;
}

int bthread_cond_broadcast(bthread_cond_t* c) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    bthread_mutex_t* m = ic->m.load(base::memory_order_relaxed);
    base::atomic<int>* const saved_seq = ic->seq;
    if (!m) {
        return 0;
    }
    void* const saved_butex = m->butex;
    // Wakeup one thread and requeue the rest on the mutex.
    ic->seq->fetch_add(1, base::memory_order_release);
    bthread::butex_requeue(saved_seq, saved_butex);
    return 0;
}

int bthread_cond_wait(bthread_cond_t* __restrict c,
                      bthread_mutex_t* __restrict m) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    const int expected_seq = ic->seq->load(base::memory_order_relaxed);
    if (ic->m.load(base::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(
                expected_m, m, base::memory_order_relaxed)) {
            return EINVAL;
        }
    }
    bthread_mutex_unlock(m);
    int rc1 = 0;
    if (bthread::butex_wait(ic->seq, expected_seq, NULL) < 0 &&
        errno != EWOULDBLOCK) {
        rc1 = errno;
    }
    const int rc2 = bthread_mutex_lock_contended(m);
    return (rc2 ? rc2 : rc1);
}

int bthread_cond_timedwait(bthread_cond_t* __restrict c,
                           bthread_mutex_t* __restrict m,
                           const struct timespec* __restrict abstime) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    const int expected_seq = ic->seq->load(base::memory_order_relaxed);
    if (ic->m.load(base::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(
                expected_m, m, base::memory_order_relaxed)) {
            return EINVAL;
        }
    }
    bthread_mutex_unlock(m);
    int rc1 = 0;
    if (bthread::butex_wait(ic->seq, expected_seq, abstime) < 0 &&
        errno != EWOULDBLOCK) {
        rc1 = errno;
    }
    const int rc2 = bthread_mutex_lock_contended(m);
    return (rc2 ? rc2 : rc1);
}

}  // extern "C"
