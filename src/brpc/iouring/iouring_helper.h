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

#ifndef BRPC_IOURING_HELPER_H
#define BRPC_IOURING_HELPER_H

#if BRPC_WITH_IOURING

#include <functional>
#include <liburing.h>
#include "bthread/types.h"
#include <gflags/gflags_declare.h>
// Fixed-buffer / slot-pool interface:
#include "brpc/iouring/iouring_block_pool.h"

namespace brpc {
namespace iouring {

// Forward declaration so IouringPollerHandle can declare IouringEndpoint as
// a friend without including iouring_endpoint.h (which itself includes this
// header, so a mutual include would create a cycle).
class IouringEndpoint;

// ---------------------------------------------------------------------------
// gflags (defined in iouring_helper.cpp)
// ---------------------------------------------------------------------------
DECLARE_int32(iouring_sq_size);
DECLARE_int32(iouring_cq_size);
DECLARE_string(iouring_polling_mode);
DECLARE_int32(iouring_sqpoll_idle_ms);
DECLARE_int32(iouring_sqpoll_cpu);
DECLARE_int32(iouring_hybrid_spin_count);

// ---------------------------------------------------------------------------
// Polling mode enum
//
// Selects the io_uring ring setup flags and the CQE-reap strategy used by
// the Poller thread.  All modes run on the Poller thread; they differ only
// in how aggressively the kernel / Poller busy-polls for completions.
//
// Defined here (not endpoint.h) to avoid a circular include.
// Explicit underlying type allows forward-declaration in endpoint.h.
// ---------------------------------------------------------------------------
enum class IouringPollingMode : int {
    NONE   = 0,  // Interrupt-driven (default).  Poller calls
                 // io_uring_wait_cqe_timeout with a 1 ms timeout; the kernel
                 // notifies via interrupt when I/O completes.  The timeout
                 // keeps the Poller loop responsive to new connections without
                 // spinning at 100 % when idle.  Suitable for most workloads.
    SQPOLL = 1,  // IORING_SETUP_SQPOLL – kernel SQ-polling thread submits I/O
                 // automatically; Poller peeks for CQEs.  Lowest latency;
                 // requires CAP_SYS_NICE (or root).
    IOPOLL = 2,  // IORING_SETUP_IOPOLL – polled block-device completion.
                 // The kernel never generates interrupts; Poller must peek
                 // after each submit.  Only valid for O_DIRECT block devices.
    HYBRID = 3,  // SQPOLL + bounded busy-spin: Poller busy-spins N times
                 // (--iouring_hybrid_spin_count), then falls back to a
                 // zero-timeout wait.  Balances latency and CPU utilisation.
};

// ---------------------------------------------------------------------------
// bRPC CQE tag
//
// bRPC marks every SQE it submits by OR-ing bit 63 into user_data:
//
//   sqe->user_data = reinterpret_cast<uint64_t>(ctx) | kBrpcCqeTag;
//
// Bit 63 is safe to use because Linux x86_64 user-space virtual addresses
// only use bits 0–46 (canonical form); bits 47–63 are always 0 for any
// pointer returned by new/malloc.  PollCq only processes CQEs where
// (user_data & kBrpcCqeTag) is set; all other CQEs are left in the ring
// for the user callback to consume.
//
// Users can set user_data to any value that has bit 63 clear – including
// raw pointers, integers, or any application-defined encoding – and bRPC
// will never touch those CQEs.
// ---------------------------------------------------------------------------
static constexpr uint64_t kBrpcCqeTag = (uint64_t(1) << 63);

// ---------------------------------------------------------------------------
// IouringPollerHandle – opaque handle to a single Poller ring
//
// An IouringPollerHandle is injected by the framework into every callback,
// init_fn, and release_fn registered with InitPollingModeWithTag.  It is
// valid only for the duration of that call and must not be stored.
//
// Design rationale
// ----------------
// The framework already knows which Poller is running when it invokes a
// callback.  Passing a handle avoids the "context-injection-then-re-query"
// anti-pattern where the user captures (tag, poller_index) in a closure
// only to pass them back to GetPollerRing / SubmitSqesWithLock.
//
// The handle exposes a single operation:
//
//   Submit()  – calls prepare_fn(ring) and a single io_uring_submit().
//               Always invoked on the Poller thread so no locking is needed.
//               The prepare_fn receives the raw io_uring* and may both
//               drain pending user CQEs and queue new SQEs.
//
// bRPC's PollCq skips any CQE whose user_data has bit 63 clear (user CQEs)
// and does NOT call io_uring_cqe_seen() on them.  The user MUST drain all
// pending user CQEs inside Submit()'s prepare_fn on every callback invocation;
// failing to do so will eventually fill the CQ and block new submissions.
//
// Typical usage:
//
//   brpc::iouring::InitPollingModeWithTag(
//       tag,
//       /*callback=*/[](brpc::iouring::IouringPollerHandle h) {
//           h.Submit([](::io_uring* r) -> int {
//               // ① Drain all pending user CQEs (bit 63 clear).
//               //   bRPC skips but does NOT mark them seen – must be done here.
//               struct io_uring_cqe* cqe = nullptr;
//               while (io_uring_peek_cqe(r, &cqe) == 0) {
//                   if (cqe->user_data & brpc::iouring::kBrpcCqeTag) break;
//                   process_my_completion(cqe->user_data, cqe->res);
//                   io_uring_cqe_seen(r, cqe);   // MUST NOT be omitted
//               }
//               // ② Queue next user SQEs.
//               ::io_uring_sqe* sqe = io_uring_get_sqe(r);
//               if (!sqe) return 0;          // SQ full – retry next round
//               io_uring_prep_nop(sqe);
//               sqe->user_data = my_token;   // bit 63 MUST be 0
//               return 1;
//           });
//       });
// ---------------------------------------------------------------------------
class IouringPollerHandle {
public:
    // SQE submission (and optional CQ drain).
    //
    // Must be called on the Poller thread (no locking needed).
    // Calls prepare_fn(ring) so the caller can queue one or more SQEs via
    // io_uring_get_sqe(), then issues a single io_uring_submit().
    //
    // prepare_fn must return:
    //   >  0  number of SQEs queued → submit is issued
    //   == 0  nothing queued        → submit is skipped
    //   <  0  abort                 → submit is skipped, errno should be set
    //
    // Returns io_uring_submit() result (>= 0) or -1 (errno set).
    //   ENODEV – ring not initialised
    //   EBUSY  – prepare_fn returned < 0
    //   other  – io_uring_submit() error
    int Submit(std::function<int(::io_uring*)> prepare_fn) const;

    // Accessors (rarely needed).
    bthread_tag_t tag()          const { return tag_; }
    int           poller_index() const { return index_; }

private:
    // Only IouringEndpoint constructs handles.
    friend class IouringEndpoint;
    IouringPollerHandle(bthread_tag_t t, int idx) : tag_(t), index_(idx) {}

    bthread_tag_t tag_;
    int           index_;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void GlobalIouringInitializeOrDie();
void GlobalIouringRelease();

// Register per-Poller hooks for the given bthread tag.
//
// Each callback / init_fn / release_fn receives an IouringPollerHandle that
// is bound to the specific Poller ring that invoked it.  The handle is valid
// only for the duration of that invocation.
//
//   init_fn    – called once after the ring is created and registered.
//                Use h.Submit() to queue initial SQEs.
//   callback   – called after every PollCq pass (each Poller loop iteration).
//                Drain user CQEs via h.ring() then submit new ones via
//                h.Submit().
//   release_fn – called just before the ring is destroyed on shutdown.
//
// All three run on the Poller thread (safe for CQ access without extra locks).
bool InitPollingModeWithTag(
    bthread_tag_t tag,
    std::function<void(IouringPollerHandle)> callback   = nullptr,
    std::function<void(IouringPollerHandle)> init_fn    = nullptr,
    std::function<void(IouringPollerHandle)> release_fn = nullptr);

void ReleasePollingModeWithTag(bthread_tag_t tag);

// ---------------------------------------------------------------------------
// Runtime queries
// ---------------------------------------------------------------------------
bool               IsIouringAvailable();
void               GlobalDisableIouring();
IouringPollingMode GetPollingMode();
int                GetIouringSqSize();
int                GetIouringCqSize();

}  // namespace iouring
}  // namespace brpc

#else  // !BRPC_WITH_IOURING

namespace brpc {
namespace iouring {
void GlobalIouringInitializeOrDie();
}
}

#endif  // BRPC_WITH_IOURING
#endif  // BRPC_IOURING_HELPER_H
