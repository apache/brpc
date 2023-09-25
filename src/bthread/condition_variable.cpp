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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Sun Aug  3 12:46:15 CST 2014

#include "butil/atomicops.h"
#include "butil/macros.h"                         // BAIDU_CASSERT
#include "bthread/butex.h"                       // butex_*
#include "bthread/types.h"                       // bthread_cond_t

namespace bthread {
struct CondInternal {
    butil::atomic<bthread_mutex_t*> m;
    butil::atomic<int>* seq;
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
    butil::atomic<int>* const saved_seq = ic->seq;
    saved_seq->fetch_add(1, butil::memory_order_release);
    // don't touch ic any more
    bool no_signal = true;
    bthread::butex_wake(saved_seq, no_signal);
    return 0;
}

int bthread_cond_broadcast(bthread_cond_t* c) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    bthread_mutex_t* m = ic->m.load(butil::memory_order_relaxed);
    butil::atomic<int>* const saved_seq = ic->seq;
    if (!m) {
        return 0;
    }
    void* const saved_butex = m->butex;
    // Wakeup one thread and requeue the rest on the mutex.
    ic->seq->fetch_add(1, butil::memory_order_release);
    bthread::butex_requeue(saved_seq, saved_butex);
    return 0;
}

int bthread_cond_wait(bthread_cond_t* __restrict c,
                      bthread_mutex_t* __restrict m) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    const int expected_seq = ic->seq->load(butil::memory_order_relaxed);
    if (ic->m.load(butil::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(
                expected_m, m, butil::memory_order_relaxed)) {
            return EINVAL;
        }
    }
    bthread_mutex_unlock(m);
    int rc1 = 0;
    if (bthread::butex_wait(ic->seq, expected_seq, NULL) < 0 &&
        errno != EWOULDBLOCK && errno != EINTR/*note*/) {
        // EINTR should not be returned by cond_*wait according to docs on
        // pthread, however spurious wake-up is OK, just as we do here
        // so that users can check flags in the loop often companioning
        // with the cond_wait ASAP. For example:
        //   mutex.lock();
        //   while (!stop && other-predicates) {
        //     cond_wait(&mutex);
        //   }
        //   mutex.unlock();
        // After interruption, above code should wake up from the cond_wait
        // soon and check the `stop' flag and other predicates.
        rc1 = errno;
    }
    const int rc2 = bthread_mutex_lock_contended(m);
    return (rc2 ? rc2 : rc1);
}

int bthread_cond_timedwait(bthread_cond_t* __restrict c,
                           bthread_mutex_t* __restrict m,
                           const struct timespec* __restrict abstime) {
    bthread::CondInternal* ic = reinterpret_cast<bthread::CondInternal*>(c);
    const int expected_seq = ic->seq->load(butil::memory_order_relaxed);
    if (ic->m.load(butil::memory_order_relaxed) != m) {
        // bind m to c
        bthread_mutex_t* expected_m = NULL;
        if (!ic->m.compare_exchange_strong(
                expected_m, m, butil::memory_order_relaxed)) {
            return EINVAL;
        }
    }
    bthread_mutex_unlock(m);
    int rc1 = 0;
    if (bthread::butex_wait(ic->seq, expected_seq, abstime) < 0 &&
        errno != EWOULDBLOCK && errno != EINTR/*note*/) {
        // note: see comments in bthread_cond_wait on EINTR.
        rc1 = errno;
    }
    const int rc2 = bthread_mutex_lock_contended(m);
    return (rc2 ? rc2 : rc1);
}

}  // extern "C"
