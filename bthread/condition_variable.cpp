// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Aug  3 12:46:15 CST 2014

#include "base/atomicops.h"
#include "base/macros.h"                         // BAIDU_CASSERT
#include "bthread/butex.h"                       // butex_*
#include "bthread/types.h"                       // bthread_cond_t

// Implement bthread_cond_t related functions

struct bthread_innercond_t {
    base::atomic<bthread_mutex_t*> m;
    base::atomic<int>* seq;
};

BAIDU_CASSERT(sizeof(bthread_innercond_t) == sizeof(bthread_cond_t),
              sizeof_innercond_must_equal_cond);
BAIDU_CASSERT(offsetof(bthread_innercond_t, m) == offsetof(bthread_cond_t, m),
              offsetof_cond_mutex_must_equal);
BAIDU_CASSERT(offsetof(bthread_innercond_t, seq) ==
              offsetof(bthread_cond_t, seq),
              offsetof_cond_seq_must_equal);

extern "C" {

extern int bthread_mutex_unlock(bthread_mutex_t*);
extern void bthread_mutex_lock_contended(bthread_mutex_t*);

int bthread_cond_init(bthread_cond_t* __restrict c,
                      const bthread_condattr_t* __restrict attr) {
    (void) attr;
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
    bthread_innercond_t* ic = reinterpret_cast<bthread_innercond_t*>(c);
    ic->seq->fetch_add(1);
    bthread::butex_wake(ic->seq);
    return 0;
}

int bthread_cond_broadcast(bthread_cond_t* c) {
    bthread_innercond_t* ic = reinterpret_cast<bthread_innercond_t*>(c);
    bthread_mutex_t* m = ic->m.load(base::memory_order_relaxed);
    if (NULL == m) {
        return 0;
    }
    // Wake one thread, and requeue the rest on the MUTEX
    ic->seq->fetch_add(1);
    bthread::butex_requeue(
        ic->seq, bthread::butex_locate(m->butex_memory));
    return 0;
}

int bthread_cond_wait(bthread_cond_t* __restrict c,
                      bthread_mutex_t* __restrict m) {
    bthread_innercond_t* ic = reinterpret_cast<bthread_innercond_t*>(c);
    const int expected_seq = ic->seq->load(base::memory_order_relaxed);
    if (ic->m.load(base::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(expected_m, m)) {
            return EINVAL;
        }
    }
    bthread_mutex_unlock(m);
    bthread::butex_wait(ic->seq, expected_seq, NULL);
    // grab m
    bthread_mutex_lock_contended(m);
    return 0;
}

int bthread_cond_timedwait(bthread_cond_t* __restrict c,
                           bthread_mutex_t* __restrict m,
                           const struct timespec* __restrict abstime) {
    bthread_innercond_t* ic = reinterpret_cast<bthread_innercond_t*>(c);
    const int expected_seq = ic->seq->load(base::memory_order_relaxed);
    if (ic->m.load(base::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(expected_m, m)) {
            return EINVAL;
        }
    }
    int rc = 0;
    bthread_mutex_unlock(m);
    if (bthread::butex_wait(ic->seq, expected_seq, abstime) < 0 &&
        errno == ETIMEDOUT) {
        rc = ETIMEDOUT;
    }
    // grab m
    bthread_mutex_lock_contended(m);
    return rc;
}

}  // extern "C"
