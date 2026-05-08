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

#include <memory>
#include "bvar/collector.h"
#include "butil/memory/scope_guard.h"
#include "bthread/rwlock.h"
#include "bthread/mutex.h"
#include "bthread/butex.h"

namespace bthread {

// Defined in bthread/mutex.cpp; reused here so that bthread_rwlock_t
// participates in the global ContentionProfiler just like bthread_mutex_t
// and bthread_sem_t.
class ContentionProfiler;
extern ContentionProfiler* g_cp;
extern bvar::CollectorSpeedLimit g_cp_sl;
extern void submit_contention(const bthread_contention_site_t& csite, int64_t now_ns);

// Lazily arm sampling on first contention. Caller must declare
// `size_t sampling_range' and `int64_t start_ns' in scope:
//   start_ns ==  0 -> not yet decided
//   start_ns == -1 -> decided NOT to sample (profiler off / not selected)
//   start_ns  >  0 -> sampling armed; value is the wall-clock start time
#define BTHREAD_RWLOCK_MAYBE_START_SAMPLING                                       \
    do {                                                                          \
        if (start_ns == 0) {                                                      \
            if (BAIDU_UNLIKELY(g_cp != NULL)) {                                   \
                sampling_range = bvar::is_collectable(&g_cp_sl);                  \
                start_ns = bvar::is_sampling_range_valid(sampling_range) ?        \
                    butil::cpuwide_time_ns() : -1;                                \
            } else {                                                              \
                start_ns = -1;                                                    \
            }                                                                     \
        }                                                                         \
    } while (0)

// Submit one contention sample if sampling was armed for this attempt.
// `start_ns > 0' is the convention used everywhere in this file to indicate
// that BTHREAD_RWLOCK_MAYBE_START_SAMPLING actually decided to sample.
// No-op otherwise. Force-inlined so the uncontended fast path stays cheap.
static BUTIL_FORCE_INLINE void submit_contention_if_sampled(
        int64_t start_ns, size_t sampling_range) {
    if (BAIDU_UNLIKELY(start_ns > 0)) {
        const int64_t end_ns = butil::cpuwide_time_ns();
        const bthread_contention_site_t csite{end_ns - start_ns, sampling_range};
        submit_contention(csite, end_ns);
    }
}

// bthread RWLock
// writer-priority implementation overview
// Three synchronization fields are used:
//
//   * `lock_word' (32-bit butex):
//       bit 31  : 1 if the write lock is held, 0 otherwise.
//       bit 0~30: number of readers currently holding the read lock.
//       Mutually exclusive: when bit 31 is set, the lower 31 bits are 0.
//
//   * `writer_wait_count' (32-bit butex):
//       Number of writers that have entered wrlock() but not yet finished
//       (i.e. currently waiting for the mutex / waiting for lock_word==0 /
//       holding the write lock). Each writer accounts for itself: it is
//       incremented at the very beginning of wrlock() and decremented at
//       the very end of unwrlock()/cleanup().
//       Readers consult this field to implement writer-priority: if any
//       writer is "in flight", new readers yield by waiting on it.
//
//   * `writer_queue_mutex' (bthread_mutex_t):
//       Serializes writers so that at most one writer races for `lock_word'
//       at any time. Other writers queue up on this mutex.
//
// Wakeup channels:
//   * Readers waiting on writers   -> wait on  writer_wait_count, woken by unwrlock/cleanup
//   * Writers waiting on readers   -> wait on  lock_word, woken by unrdlock
//   * Writers waiting on writers   -> wait on  writer_queue_mutex

static int rwlock_rdlock(bthread_rwlock_t* rwlock, bool try_lock,
                         const struct timespec* abstime) {
    auto lock_word = (butil::atomic<unsigned>*)rwlock->lock_word;
    auto writer_wait_count = (butil::atomic<unsigned>*)rwlock->writer_wait_count;

    // Sampling state for the contention profiler (lazily armed on first
    // contention so that the uncontended fast path stays cheap):
    //   start_ns  == 0  -> not yet decided
    //   start_ns  == -1 -> decided NOT to sample
    //   start_ns  >  0  -> sampling armed; submit on exit
    // Each reader samples independently and submits once on its own way out;
    // we deliberately do NOT use rwlock->writer_csite here because that field
    // is exclusively owned by the writer.
    size_t sampling_range = bvar::INVALID_SAMPLING_RANGE;
    int64_t start_ns = 0;
    int rc = 0;

    while (true) {
        // Writer-priority: if any writer is in flight, yield to it.
        // `relaxed' is sufficient here because:
        //   - There is no data published via writer_wait_count;
        //     data visibility is established via the acquire-CAS on
        //     `lock_word' below paired with the release-CAS in unwrlock().
        //   - butex_wait() will re-check the expected value before sleeping,
        //     so we cannot lose a wakeup even if `w' is slightly stale.
        unsigned w = writer_wait_count->load(butil::memory_order_relaxed);
        if (w > 0) {
            if (try_lock) {
                // Don't sample tryrdlock failures: they are by design a
                // non-blocking probe, not a contention event.
                return EBUSY;
            }
            // We are about to block on writer_wait_count; arm sampling
            // before parking so the wait time is included in the report.
            BTHREAD_RWLOCK_MAYBE_START_SAMPLING;
            if (butex_wait(writer_wait_count, w, abstime) < 0 &&
                errno != EWOULDBLOCK && errno != EINTR) {
                rc = errno;
                break;
            }
            continue;
        }

        // No writer in flight: try to add ourselves to the reader count.
        // 2^31 - 1 readers should be enough for any realistic workload.
        unsigned l = lock_word->load(butil::memory_order_relaxed);
        if ((l >> 31) == 0) {
            // Refuse to increment when the reader count has saturated
            // the low 31 bits. Otherwise `l + 1' would flip bit 31 and
            // we would corrupt lock_word into "writer held" state.
            // POSIX-style: report EAGAIN ("max read locks exceeded").
            if (BAIDU_UNLIKELY(l == 0x7FFFFFFFu)) {
                LOG(ERROR) << "Too many readers on bthread_rwlock_t=" << rwlock;
                rc = EAGAIN;
                break;
            }
            // Acquire on success synchronizes-with the release-CAS in
            // unwrlock(), so any data written by the previous writer is
            // visible to us before we start reading.
            if (lock_word->compare_exchange_weak(l, l + 1,
                                                 butil::memory_order_acquire,
                                                 butil::memory_order_relaxed)) {
                rc = 0;
                break;
            }
            // CAS failed (likely another reader bumped r): retry.
        } else if (try_lock) {
            // Write lock is currently held.
            return EBUSY;
        } else {
            // Write lock currently held but not yet self-accounted as a
            // pending writer (very narrow window inside wrlock). Arm
            // sampling now so the spin/wait until writer_wait_count >= 1
            // is also accounted for.
            BTHREAD_RWLOCK_MAYBE_START_SAMPLING;
        }
        // Otherwise (write lock held but not try_lock): spin once more.
        // The next iteration will observe writer_wait_count >= 1 (writers
        // self-account in writer_wait_count for the entire wrlock lifetime),
        // and we will block on it instead of busy spinning.
    }

    // Submit one contention sample for this reader (success or failure).
    submit_contention_if_sampled(start_ns, sampling_range);
    return rc;
}

static int rwlock_unrdlock(bthread_rwlock_t* rwlock) {
    auto lock_word = (butil::atomic<unsigned>*)rwlock->lock_word;
    while (true) {
        unsigned l = lock_word->load(butil::memory_order_relaxed);
        // Misuse detection: the caller must currently hold a read lock.
        // l == 0           -> no lock is held (double unlock?)
        // (l >> 31) != 0   -> write lock is held, not read lock
        if (l == 0 || (l >> 31) != 0) {
            LOG(ERROR) << "Invalid unrdlock on bthread_rwlock_t=" << rwlock
                       << ", lock_word=" << l;
            return EINVAL;
        }
        // Release on success publishes any reads/writes done while holding
        // the read lock to the next acquirer (typically a writer's
        // acquire-CAS in wrlock()).
        if(!(lock_word->compare_exchange_weak(l, l - 1,
                                              butil::memory_order_release,
                                              butil::memory_order_relaxed))) {
            continue;
        }
        // We were the last reader (lock_word transitioned 1 -> 0). Wake the
        // single writer (if any) that may be sleeping on `lock_word' inside
        // wrlock(). At most one writer can be there because writers are
        // serialized by writer_queue_mutex.
        // No-op if nobody is waiting; butex_wake() short-circuits cheaply.
        if (l == 1) {
            butex_wake(lock_word);
        }
        return 0;
    }
}

// Roll back the side effects of a failed wrlock attempt:
//   - Release writer_queue_mutex if we managed to acquire it.
//   - Decrement our share of writer_wait_count.
//   - If we were the last in-flight writer, wake all readers that have
//     been parked by writer-priority (w == 1 means writer_wait_count is now 0).
// Called on EBUSY (try_lock failed), ETIMEDOUT, EINTR-leading-to-fail.
static BUTIL_FORCE_INLINE void rwlock_wrlock_cleanup(bthread_rwlock_t* rwlock, bool write_queue_locked) {
    if (write_queue_locked) {
        bthread_mutex_unlock(&rwlock->writer_queue_mutex);
    }
    auto writer_wait_count = (butil::atomic<unsigned>*)rwlock->writer_wait_count;
    // Withdraw our writer-priority "vote" so readers can make progress.
    auto w = writer_wait_count->fetch_sub(1, butil::memory_order_relaxed);
    // w is the value BEFORE the subtraction, so w == 1 means we were the
    // last writer in flight; wake every reader parked on writer_wait_count.
    if (w == 1) {
        butex_wake_all(writer_wait_count);
    }
}

static int rwlock_wrlock(bthread_rwlock_t* rwlock, bool try_lock,
                         const struct timespec* abstime) {
    auto writer_wait_count = (butil::atomic<unsigned>*)rwlock->writer_wait_count;
    // Step 1: announce ourselves before doing anything else, so that
    // concurrent readers immediately observe writer-priority and back off.
    // This MUST happen before we try to acquire writer_queue_mutex,
    // otherwise a flood of readers could starve us indefinitely.
    // 2^31 in-flight writers should be enough for any realistic workload.
    writer_wait_count->fetch_add(1, butil::memory_order_relaxed);

    // Sampling state for the contention profiler. Both wrlock() and
    // unwrlock() sample independently: wrlock() submits its own wait time
    // on the way out (success or failure); unwrlock() samples its own
    // CAS-spin / mutex_unlock / butex_wake_all latency separately. We do
    // NOT use rwlock->writer_csite here -- the two operations are not
    // forced to share a single sample.
    size_t sampling_range = bvar::INVALID_SAMPLING_RANGE;
    int64_t start_ns = 0;

    // Step 2: serialize with other writers. At most one writer holds
    // `writer_queue_mutex' at a time and races for `lock_word'.
    int rc = bthread_mutex_trylock(&rwlock->writer_queue_mutex);
    if (0 != rc) {
        if (try_lock) {
            // Fail to acquire the wrlock. Don't sample trywrlock failures:
            // they are by design a non-blocking probe, not a contention event.
            rwlock_wrlock_cleanup(rwlock, false);
            return rc;
        }
        // We are about to block on writer_queue_mutex; arm sampling.
        // Note: the inner mutex itself has csite disabled (see init), so
        // its blocking time is only counted once -- here, by the rwlock.
        BTHREAD_RWLOCK_MAYBE_START_SAMPLING;
        rc = bthread_mutex_timedlock(&rwlock->writer_queue_mutex, abstime);
        if (0 != rc) {
            // Fail to acquire the wrlock. Submit the elapsed wait time
            // directly (no unwrlock() will run for this writer).
            submit_contention_if_sampled(start_ns, sampling_range);
            rwlock_wrlock_cleanup(rwlock, false);
            return rc;
        }
    }

    // Step 3: with `writer_queue_mutex' held, wait for all readers to drain
    // and then claim the write bit of `lock_word'.
    auto lock_word = (butil::atomic<unsigned>*)rwlock->lock_word;
    while (true) {
        unsigned l = lock_word->load(butil::memory_order_relaxed);
        if (l != 0) {
            // Readers still hold the lock. Park on `lock_word' until the last
            // reader releases (unrdlock will butex_wake on transition 1->0).
            if (try_lock) {
                errno = EBUSY;
                break;
            }
            // Arm sampling before parking so the wait-for-readers time is
            // counted (in case the queue_mutex acquisition above was uncontended).
            BTHREAD_RWLOCK_MAYBE_START_SAMPLING;
            // Use the freshly read `r' as expected; if lock_word changes
            // before we sleep, butex_wait returns EWOULDBLOCK and we retry.
            if (butex_wait(lock_word, l, abstime) < 0 &&
                errno != EWOULDBLOCK && errno != EINTR) {
                break;
            }
            continue;
        }
        // Acquire on success synchronizes-with release-CAS in
        // unrdlock()/unwrlock(): we will see all data published by the
        // previous reader/writer before we start writing.
        if (lock_word->compare_exchange_weak(l, (unsigned)(1 << 31),
                                             butil::memory_order_acquire,
                                             butil::memory_order_relaxed)) {
            // Submit the writer's wait sample immediately on success.
            // unwrlock() will sample its own latency separately.
            submit_contention_if_sampled(start_ns, sampling_range);
            return 0;
        }
        // CAS may spuriously fail (weak); retry without sleeping.
    }

    // Failure path: snapshot errno before cleanup, because
    // bthread_mutex_unlock / butex_wake_all inside cleanup may invoke
    // syscalls or yield and clobber errno on this thread.
    int saved_errno = errno;
    // Submit the elapsed wait directly; we never reached unwrlock().
    submit_contention_if_sampled(start_ns, sampling_range);
    rwlock_wrlock_cleanup(rwlock, true);
    return saved_errno;
}

static int rwlock_unwrlock(bthread_rwlock_t* rwlock) {
    auto lock_word = (butil::atomic<unsigned>*)rwlock->lock_word;
    auto writer_wait_count = (butil::atomic<unsigned>*)rwlock->writer_wait_count;

    // Sampling state for the contention profiler. unwrlock() samples
    // independently of wrlock(): although the release-CAS itself cannot
    // fail due to writer-writer contention (writers are serialized by
    // writer_queue_mutex), the body still does mutex_unlock(),
    // butex_wake_all() and may spuriously spin on the weak CAS, all of
    // which contribute to the critical-section tail latency.
    size_t sampling_range = bvar::INVALID_SAMPLING_RANGE;
    int64_t start_ns = 0;
    BTHREAD_RWLOCK_MAYBE_START_SAMPLING;

    while (true) {
        unsigned l = lock_word->load(butil::memory_order_relaxed);
        // Misuse detection: we must currently hold the write lock.
        if (BAIDU_UNLIKELY(l != (unsigned)(1 << 31))) {
            LOG(ERROR) << "Invalid unwrlock!";
            return EINVAL;
        }
        // Release-CAS publishes all writes performed under the write lock
        // to the next acquirer (a reader's acquire-CAS or another writer's
        // acquire-CAS). The CAS itself cannot fail due to contention since
        // writers are serialized by writer_queue_mutex; weak failure here is
        // only a spurious CAS failure -- just retry.
        if (!lock_word->compare_exchange_weak(l, 0,
                                              butil::memory_order_release,
                                              butil::memory_order_relaxed)) {
            continue;
        }

        // ---- Order of the next two operations is INTENTIONAL ----
        //
        // We deliberately:
        //   (1) unlock writer_queue_mutex FIRST, then
        //   (2) fetch_sub(writer_wait_count) and conditionally wake readers.
        //
        // Rationale (writer-priority semantics):
        //   * Any writer queued on writer_queue_mutex has already
        //     fetch_add'ed its share into writer_wait_count back in wrlock()
        //     (before it even tried to lock the mutex). So when it wakes
        //     up here and we later fetch_sub, the counter still reflects
        //     "there is at least one more writer in flight": w_old >= 2,
        //     which means w != 1, which means we will NOT wake readers.
        //     Readers must keep yielding to the next writer -- exactly the
        //     writer-priority invariant.
        //   * Only when we are truly the last writer in flight (w_old == 1
        //     after our fetch_sub, i.e. writer_wait_count is now 0) do we
        //     wake_all readers parked on writer_wait_count.
        //
        // Subtle but harmless effect:
        //   Between (1) and (2) there is a small window in which our
        //   own "ghost share" is still counted in writer_wait_count even though
        //   we have effectively left. New readers entering rdlock() during
        //   this window will see writer_wait_count >= 1 and park on it; they
        //   will be woken either by step (2) below (if no successor writer
        //   appeared) or by the successor writer's eventual unwrlock.
        //   No wakeup is ever lost: butex_wait re-checks the expected
        //   value before truly sleeping, and any successor writer will
        //   itself execute this same wake logic on its way out.
        //
        // Reversing the order (fetch_sub before unlock mutex) would break
        // strict writer-priority because woken readers could grab the
        // read lock before a successor writer queued on the mutex even
        // gets a chance to CAS lock_word.
        bthread_mutex_unlock(&rwlock->writer_queue_mutex);
        unsigned w = writer_wait_count->fetch_sub(1, butil::memory_order_relaxed);
        if (w == 1) {
            butex_wake_all(writer_wait_count);
        }

        // Submit our own unwrlock-side sample (CAS spin + mutex_unlock +
        // butex_wake_all). This is independent of the wrlock-side sample.
        submit_contention_if_sampled(start_ns, sampling_range);
        return 0;
    }
}

// Generic unlock entry that dispatches to unwrlock/unrdlock by inspecting
// `lock_word'. This is safe ONLY because the caller must already hold one of
// the two locks: while holding a read lock the high bit of `lock_word' cannot
// flip on, and while holding the write lock the low bits cannot be set.
// Therefore a relaxed load is sufficient to make the dispatch decision.
static int rwlock_unlock(bthread_rwlock_t* rwlock) {
    auto lock_word = (butil::atomic<unsigned>*)rwlock->lock_word;
    unsigned r = lock_word->load(butil::memory_order_relaxed);
    if ((r >> 31) != 0) {
        return rwlock_unwrlock(rwlock);
    } else {
        return rwlock_unrdlock(rwlock);
    }
}

// Deleter that turns butex_create_checked()'s raw pointer into something
// std::unique_ptr can clean up automatically. Using RAII here lets the
// init-error paths just `return rc' without manually unwinding partial
// allocations; ownership is `release()'d only on the all-success path.
struct ButexDeleter {
    void operator()(void* butex) const {
        if (butex != NULL) {
            butex_destroy(butex);
        }
    }
};

static int rwlock_init(bthread_rwlock_t* rwlock) {
    std::unique_ptr<unsigned, ButexDeleter> writer_wait_count(
    butex_create_checked<unsigned>());
    if (writer_wait_count == NULL) {
        LOG(ERROR) << "Fail to create writer_wait_count butex: out of memory";
        return ENOMEM;
    }
    std::unique_ptr<unsigned, ButexDeleter> lock_word(butex_create_checked<unsigned>());
    if (lock_word == NULL) {
        LOG(ERROR) << "Fail to create lock_word butex: out of memory";
        return ENOMEM;
    }
    *writer_wait_count = 0;
    *lock_word = 0;

    bthread_mutexattr_t attr;
    bthread_mutexattr_init(&attr);
    BRPC_SCOPE_EXIT { bthread_mutexattr_destroy(&attr); };
    // Disable csite on the inner queue mutex so the writer's wait time is
    // accounted exactly once -- by the rwlock layer, not double-counted via
    // the inner mutex.
    bthread_mutexattr_disable_csite(&attr);
    const int rc = bthread_mutex_init(&rwlock->writer_queue_mutex, &attr);
    if (rc != 0) {
        LOG(ERROR) << "Fail to init writer_queue_mutex, rc=" << rc;
        return rc;
    }

    // All resources successfully created; transfer butex ownership to
    // rwlock. From here on, bthread_rwlock_destroy() is responsible for
    // releasing them.
    rwlock->writer_wait_count = writer_wait_count.release();
    rwlock->lock_word = lock_word.release();
    return 0;
}

static int rwlock_destroy(bthread_rwlock_t* rwlock) {
    // Destroy the inner mutex first; bthread_mutex_init() allocates an
    // internal butex which would otherwise leak. Pointers are nulled to
    // surface accidental double-destroy / use-after-destroy bugs early.
    int rc = bthread_mutex_destroy(&rwlock->writer_queue_mutex);
    if (rc != 0) {
        LOG(ERROR) << "Fail to destroy writer_queue_mutex, rc=" << rc;
    }
    if (rwlock->writer_wait_count != NULL) {
        butex_destroy(rwlock->writer_wait_count);
        rwlock->writer_wait_count = NULL;
    }
    if (rwlock->lock_word != NULL) {
        butex_destroy(rwlock->lock_word);
        rwlock->lock_word = NULL;
    }
    return rc;
}

} // namespace bthread

__BEGIN_DECLS

int bthread_rwlock_init(bthread_rwlock_t* __restrict rwlock,
                        const bthread_rwlockattr_t* __restrict) {
    return bthread::rwlock_init(rwlock);
}

int bthread_rwlock_destroy(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_destroy(rwlock);
}

int bthread_rwlock_rdlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_rdlock(rwlock, false, NULL);
}

int bthread_rwlock_tryrdlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_rdlock(rwlock, true, NULL);
}

int bthread_rwlock_timedrdlock(bthread_rwlock_t* __restrict rwlock,
                               const struct timespec* __restrict abstime) {
    return bthread::rwlock_rdlock(rwlock, false, abstime);
}

int bthread_rwlock_wrlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_wrlock(rwlock, false, NULL);
}

int bthread_rwlock_trywrlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_wrlock(rwlock, true, NULL);
}

int bthread_rwlock_timedwrlock(bthread_rwlock_t* __restrict rwlock,
                               const struct timespec* __restrict abstime) {
    return bthread::rwlock_wrlock(rwlock, false, abstime);
}

int bthread_rwlock_unlock(bthread_rwlock_t* rwlock) {
    return bthread::rwlock_unlock(rwlock);
}

__END_DECLS
