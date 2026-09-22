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

#ifndef BUTIL_SYNCHRONIZATION_SEQLOCK_H
#define BUTIL_SYNCHRONIZATION_SEQLOCK_H

#include <stdint.h>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

#include "butil/atomicops.h"
#include "butil/compiler_specific.h"
#include "butil/macros.h"
#include "butil/processor.h"

namespace butil {
namespace internal {

// Detects std::reference_wrapper<T>.
template <typename T>
struct IsReferenceWrapper : std::false_type {};
template <typename T>
struct IsReferenceWrapper<std::reference_wrapper<T>> : std::true_type {};

// A sequence counter for consistent reads without acquiring a reader mutex
// around caller-owned payloads. This is an implementation detail of butil::Seqlock;
// use butil::Seqlock instead.
//
// SeqCounter does not protect payload accesses from C++ data races. Callers must
// access shared payloads atomically (typically with relaxed ordering) or use
// another mechanism that makes concurrent accesses valid.
//
//
// Cache-line aligned so the sequence counter does not falsely share a line
// with adjacent data (e.g. the writer mutex in Seqlock<Mutex> or a neighbouring
// payload), which would bounce the line between readers and writers.
class BAIDU_CACHELINE_ALIGNMENT SeqCounter {
public:
    SeqCounter() : _seq(0) {}
    DISALLOW_COPY_AND_ASSIGN(SeqCounter);

    // Repeatedly invoke `load_payload` until it observes one consistent version.
    // `load_payload` may run several times when it races with a writer, so it
    // must be cheap and side-effect free: only read the shared payload and
    // return a copy, never mutate observable state. It must also access the
    // shared payload without causing a C++ data race (relaxed atomics).
    //
    // The callback MUST return an owning value (a copy of the data), never a
    // pointer, reference, or view (string_view, span, reference_wrapper, ...)
    // into the payload. The consistency guarantee covers only the bytes copied
    // out before the validating load: once load() returns, a later writer may
    // mutate the payload, so any handle that still points into it no longer
    // refers to a consistent snapshot.
    template <typename Load>
    typename std::decay<decltype(std::declval<Load&>()())>::type
    load(Load&& load_payload) const {
        typedef typename std::decay<decltype(std::declval<Load&>()())>::type Result;
        static_assert(!std::is_void<Result>::value,
                      "SeqCounter load callback must return a value");
        static_assert(!std::is_pointer<Result>::value,
                      "SeqCounter load callback must return an owning value, not "
                      "a pointer into the payload: the pointee can be mutated by "
                      "a later writer, so it is not a consistent snapshot");
        static_assert(!IsReferenceWrapper<Result>::value,
                      "SeqCounter load callback must return an owning value, not "
                      "a std::reference_wrapper into the payload: the referent "
                      "can be mutated by a later writer, so it is not a "
                      "consistent snapshot");

        while (true) {
            // Wait for any active writer, then read on an even sequence.
            uint64_t seq;
            while ((seq = _seq.load(memory_order_acquire)) & 1) {
                cpu_relax();
            }

            Result result = load_payload();

            // Keep the payload reads above before the sequence validation
            // below; retry if a writer intervened.
            atomic_thread_fence(memory_order_acquire);
            if (_seq.load(memory_order_relaxed) == seq) {
                return result;
            }
        }
    }

    // Invoke a payload writer inside one write section. Writers must be serialized
    // externally and must not nest write sections. The section is closed via RAII
    // even if `store_payload` throws, so an exception does not leave the sequence
    // permanently odd. A throwing writer may leave the payload partially updated,
    // so callers must ensure any partial state is still safe (non-crashing) to read.
    template <typename Store>
    void store(Store&& store_payload) {
        WriteGuard guard(*this);
        store_payload();
    }

private:
    // RAII scope for a write section.
    class WriteGuard {
    public:
        explicit WriteGuard(SeqCounter& sc)
            : _seq_counter(&sc)
            , _next_seq(sc._seq.load(memory_order_relaxed) + 2) {
            // Writers are serialized, so no atomic read-modify-write is needed.
            // Keep the next even sequence for publication when the guard exits.
            _seq_counter->_seq.store(_next_seq - 1, memory_order_relaxed);
            // Order the odd-sequence store before the payload stores that
            // follow. A release fence is a StoreStore (+LoadStore) barrier: it
            // keeps any store after the fence (the payload writes) from being
            // reordered ahead of stores before it (the odd sequence), so no
            // reader can observe a payload write without also observing the odd
            // sequence. Equivalently, this release fence pairs -- through the
            // payload atomics -- with the acquire fence in load(): if a reader
            // reads a payload value published after this fence, that fence
            // synchronizes-with the reader's acquire fence, so the reader is
            // guaranteed to also see the odd sequence on its validating load
            // and retry. The acquire half of an acq_rel fence would add
            // nothing here (no prior load needs ordering), so release alone is
            // sufficient and states the intent precisely.
            atomic_thread_fence(memory_order_release);
        }

        DISALLOW_COPY_AND_ASSIGN(WriteGuard);

        ~WriteGuard() {
            // Publish the payload stores and leave the write section.
            _seq_counter->_seq.store(_next_seq, memory_order_release);
        }

    private:
        SeqCounter* _seq_counter;
        uint64_t _next_seq;
    };

    atomic<uint64_t> _seq;
};

}  // namespace internal

// A sequence lock built on top of internal::SeqCounter.
//
// Seqlock<> : single-writer, no writer mutex. Backed directly by SeqCounter;
//             the caller must serialize writers with appropriate synchronization
//             if ownership is transferred between threads.
//
// Seqlock<Mutex> : multi-writer. Owns a writer Mutex so that concurrent
//                  store() calls are serialized automatically (this is the
//                  Linux seqlock_t = SeqCounter + lock).
//
// In both forms readers do not acquire a mutex or prevent writers from entering
// a write section. Reads are NOT lock-free: they spin while the sequence is odd
// and retry if a write intervenes. A suspended writer can stall all readers, and
// continuous writes can starve readers; there is no bounded completion time.
//
// REQUIRED: write sections must not nest. A store() callback must not call load()
// or store() on the same Seqlock, or wait for work that needs to read or write it.
// In particular, a signal handler must not access the same Seqlock when it has
// interrupted a writer: the interrupted write cannot finish until the handler
// returns, so a reader in the handler would spin forever. Keep write sections
// short and avoid blocking or yielding inside them.
//
// Example: publish a (x, y) pair atomically so readers never see a torn mix.
//
//   // the caller-owned payload
//   struct Point {
//       butil::atomic<int64_t> x{0};
//       butil::atomic<int64_t> y{0};
//   };
//   Point point;
//   // single writer; use Seqlock<Mutex> if several threads may write.
//   butil::Seqlock<> seqlock;
//
//   // Writer: the whole update is published as one consistent version.
//   void set(int64_t x, int64_t y) {
//       seqlock.store([&] {
//           point.x.store(x, butil::memory_order_relaxed);
//           point.y.store(y, butil::memory_order_relaxed);
//       });
//   }
//
//   // Reader: load() retries internally until it copies out one consistent
//   // version, then returns whatever the callback returned.
//   std::pair<int64_t, int64_t> get() {
//       return seqlock.load([&] {
//           return std::make_pair(point.x.load(butil::memory_order_relaxed),
//                                 point.y.load(butil::memory_order_relaxed));
//       });
//   }
//
// REQUIRED: the payload accessed inside the load/store callbacks MUST be
// atomic (e.g. butil::atomic fields, typically read/written with
// memory_order_relaxed). This is not just a style preference -- the reader
// deliberately reads the payload while a writer may be mutating it, so the
// accesses are concurrent by design. Correctness relies on it in two ways:
//
//   1. Data race / UB. A non-atomic object read while another thread writes it
//      is a C++ data race, i.e. undefined behavior. The compiler is then free
//      to tear or fuse the access, invent extra reads, or hoist/sink it across
//      the fences below. The sequence-validation retry cannot rescue this: it
//      only tells you *whether* to retry, it cannot un-corrupt a value the
//      compiler already mangled, so you may return garbage even on a "clean"
//      (seq unchanged) read.
//   2. Ordering. The release fence on the write side pairs with the acquire fence
//      on the read side through the payload atomics (fence-fence synchronization).
//      If the payload is not atomic that pairing does not hold, so "observe a
//      payload write => observe the odd sequence and retry" is no longer guaranteed
//      and a reader can silently accept a stale or half-written snapshot.
//
// If you cannot make the payload atomic, do not use Seqlock -- use a Mutex or
// an RWLock instead.
//
// REQUIRED: the load() callback must return an OWNING value (a copy of the
// data), never a pointer, reference, or view (string_view, span,
// reference_wrapper, ...) into the payload. load() only guarantees that the
// bytes copied out before its validating load are consistent; after load()
// returns a writer may mutate the payload again, so any handle still pointing
// into it is no longer a consistent snapshot.
//
// Best fit: a small payload that is read far more often than written. The read
// path copies the whole payload out and retries the copy whenever a write races
// it, so a large payload makes both the copy and the retries expensive. For a
// large or heap-owning payload prefer a RCU/RWLock scheme instead.
template <typename Mutex = void>
class Seqlock;

// Single-writer specialization: no mutex, delegates straight to SeqCounter.
template <>
class Seqlock<void> {
public:
    Seqlock() = default;
    DISALLOW_COPY_AND_ASSIGN(Seqlock);

    // Consistent read without acquiring a mutex; may spin/retry indefinitely.
    template <typename Load>
    typename std::decay<decltype(std::declval<Load&>()())>::type
    load(Load&& load_payload) const {
        return _seq.load(std::forward<Load>(load_payload));
    }

    // Single writer only: the caller MUST ensure there is no concurrent or nested
    // store(). The write section is closed via RAII even if store_payload
    // throws.
    template <typename Store>
    void store(Store&& store_payload) {
        _seq.store(std::forward<Store>(store_payload));
    }

private:
    internal::SeqCounter _seq;
};

// Multi-writer specialization: SeqCounter + a writer Mutex.
//
// Mutex must be default-constructible and satisfy the C++ Lockable
// requirements (lock()/unlock()), e.g. butil::Mutex.
template <typename Mutex>
class Seqlock {
public:
    Seqlock() = default;
    DISALLOW_COPY_AND_ASSIGN(Seqlock);

    // Consistent read without acquiring a mutex; may spin/retry indefinitely.
    template <typename Load>
    typename std::decay<decltype(std::declval<Load&>()())>::type
    load(Load&& load_payload) const {
        return _seq.load(std::forward<Load>(load_payload));
    }

    // Serialized write: acquires the mutex, then runs the payload writer in
    // one write section.
    template <typename Store>
    void store(Store&& store_payload) {
        std::lock_guard<Mutex> lk(_mutex);
        _seq.store(std::forward<Store>(store_payload));
    }

private:
    internal::SeqCounter _seq;
    Mutex _mutex;
};

}  // namespace butil

#endif  // BUTIL_SYNCHRONIZATION_SEQLOCK_H
