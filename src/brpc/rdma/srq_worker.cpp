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


#if BRPC_WITH_RDMA
#include "brpc/rdma/srq_worker.h"
#include <algorithm>
#include <arpa/inet.h>
#include <iterator>
#include <map>
#include <mutex>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>
#include <gflags/gflags.h>
#include "bthread/bthread.h"
#include "butil/containers/flat_map.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/fd_utility.h"
#include "butil/macros.h"
#include "butil/memory/scope_guard.h"
#include "butil/object_pool.h"
#include "brpc/errno.pb.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/srq_flow_control.h"
#include "brpc/rdma/srq_receive_pool.h"

namespace brpc {
namespace rdma {

extern int (*IbvQueryDevice)(ibv_context*, ibv_device_attr*);
extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_srq* (*IbvCreateSrq)(ibv_pd*, ibv_srq_init_attr*);
extern int (*IbvDestroySrq)(ibv_srq*);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvDestroyQp)(ibv_qp*);
extern int (*IbvQueryEce)(ibv_qp*, ibv_ece*);
extern ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*);
extern int (*IbvDestroyCompChannel)(ibv_comp_channel*);
extern int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**);
extern void (*IbvAckCqEvents)(ibv_cq*, unsigned int);
// Keep verbs entry points replaceable, like resource-creation calls.
// Fault-injection tests may replace them only after all SRQ workers have
// stopped.
int (*IbvPostSrqSend)(ibv_qp*, ibv_send_wr*, ibv_send_wr**) = ibv_post_send;
int (*IbvPostSrqRecv)(ibv_srq*, ibv_recv_wr*, ibv_recv_wr**) = ibv_post_srq_recv;
int (*IbvPollSrqCq)(ibv_cq*, int, ibv_wc*) = ibv_poll_cq;
#if defined(UNIT_TEST)
// Test-only pause around the exit decision to exercise external Fail and
// event-ownership handoff races deterministically.
butil::atomic<void (*)(SrqWorker*, bool, butil::atomic<uint64_t>*, SrqReceivePool*)>
    SrqWorkerStopHookForTest{nullptr};
// Pause between the first failed try_lock and notification registration to
// exercise an unlock that precedes registration.
butil::atomic<void (*)()> SrqPostLockRetryHookForTest{nullptr};
#endif

DECLARE_bool(rdma_recv_zerocopy);
DECLARE_int32(rdma_zerocopy_min_size);
DECLARE_int32(rdma_srq_pool_grow_blocks);
DECLARE_int32(rdma_srq_pool_growth_retry_ms);

DEFINE_bool(rdma_use_srq, false,
            "Enable event-driven shared receive queues with v3 handshake");
DEFINE_int32(rdma_srq_workers, 1, "SRQ workers per bthread tag in event mode");
DEFINE_int32(rdma_srq_window, 512, "Initial and restored virtual SRQ credit window");
DEFINE_int32(rdma_srq_sq_size, 128, "Per-connection SQ depth in SRQ mode");
DEFINE_int32(rdma_srq_max_sge, 4, "Maximum SGEs per SRQ-mode send");
DEFINE_int32(rdma_srq_inline_size, 128, "Requested inline bytes in SRQ mode");
DEFINE_int32(rdma_srq_depth, 2048, "Posted receives per SRQ worker");
DEFINE_int32(rdma_srq_pool_blocks, 16384,
             "Maximum receive blocks per SRQ worker; registered on demand");
DEFINE_int32(rdma_srq_tx_cq_size, 8192, "Shared TX CQ capacity per SRQ worker");
DEFINE_int32(rdma_srq_control_reserve, 64, "TX CQ slots reserved for pure grants");

namespace {
using ConnectionPtr = std::shared_ptr<SrqConnection>;
using ProgressCallback = std::function<ProgressRound()>;
using ProgressQueue = butil::MPSCQueue<
    ProgressCallback, butil::ObjectPoolAllocator<ProgressCallback>>;

struct SocketFailure {
    // Versioned Socket ID prevents a delayed failure from targeting a reused
    // Socket.
    SocketId sid;
    // Error passed to Socket::SetFailed by the deferred task.
    int error;
};

constexpr uint32_t kControlSlots = 2;
constexpr int kBatch = 32;
butil::atomic<size_t> g_live_connections{0};
butil::atomic<size_t> g_live_workers{0};
}  // namespace

// Results local to one progress or Stop round. Notifications and delivery run
// after releasing _progress_mutex. Sets coalesce entries per connection and retain
// references, so connections need no per-round membership flags.
struct ProgressRound {
    // Writers to recheck after TX completion, grants or shared-CQ capacity
    // becomes available.
    std::unordered_set<ConnectionPtr> writers;
    // Connections with new input. Each _incoming queue preserves byte order.
    std::unordered_set<ConnectionPtr> deliveries;
    // Connections ready for normal retirement after a fresh RX drain. Retained
    // until outside the lock.
    std::vector<ConnectionPtr> retiring;
    // Callbacks dequeued this round, including pending, executed and cancelled
    // callbacks. Retains their captured contexts so final references are
    // released outside _progress_mutex.
    std::vector<ProgressCallback> callbacks;
    // Failure notifications dequeued under _progress_mutex. RunEvent and Stop
    // can dispatch their independent batches concurrently after unlocking.
    std::vector<SocketFailure> failures;
    // User payloads transferred by the owner for deferred release after
    // unlocking.
    butil::IOBuf released;
    // Whether this progress result includes a CQE. RunEvent uses only the
    // post-arm result when deciding whether another iteration is needed.
    bool completed{false};

    ProgressRound() = default;

    // IOBuf has no standard move constructor. Transfer its references
    // explicitly so moving a result cannot leave a hidden payload owner that
    // outlives deferred release and runs a user deleter on the progress task.
    ProgressRound(ProgressRound&& other) noexcept
        : writers(std::move(other.writers))
        , deliveries(std::move(other.deliveries))
        , retiring(std::move(other.retiring))
        , callbacks(std::move(other.callbacks))
        , failures(std::move(other.failures))
        , released(other.released.movable())
        , completed(other.completed) {}

    // Adopt returned results without releasing their final references under
    // the owner lock. Swap into empty containers, otherwise move or deduplicate
    // entries while keeping their contexts alive in the combined result.
    void Append(ProgressRound other) {
        if (writers.empty()) {
            writers.swap(other.writers);
        } else {
            writers.insert(other.writers.begin(), other.writers.end());
        }
        if (deliveries.empty()) {
            deliveries.swap(other.deliveries);
        } else {
            deliveries.insert(other.deliveries.begin(), other.deliveries.end());
        }
        if (retiring.empty()) {
            retiring.swap(other.retiring);
        } else {
            retiring.insert(retiring.end(),
                std::make_move_iterator(other.retiring.begin()),
                std::make_move_iterator(other.retiring.end()));
        }
        if (callbacks.empty()) {
            callbacks.swap(other.callbacks);
        } else {
            callbacks.insert(callbacks.end(),
                std::make_move_iterator(other.callbacks.begin()),
                std::make_move_iterator(other.callbacks.end()));
        }
        if (failures.empty()) {
            failures.swap(other.failures);
        } else {
            for (auto& failure : other.failures) {
                failures.push_back(failure);
            }
        }
        released.append(other.released.movable());
        completed = completed || other.completed;
    }
};

namespace {
// Actions from one receive CQE: return a connection and flags without
// allocating per-packet sets.
struct ReceiveResult {
    // Connection retained by the caller when input or a grant requires
    // notification.
    ConnectionPtr connection;
    // A grant arrived. Recheck writability after releasing the owner lock.
    bool wake_writer{false};
    // Payload was queued. Attempt to start a delivery task.
    bool deliver{false};
};

void DrainEventFd(int fd) {
    uint64_t value;
    while (read(fd, &value, sizeof(value)) == sizeof(value)) {}
}

struct DeliveryTask {
    // Keeps connection state alive until this consumer task finishes.
    ConnectionPtr connection;
    // Keeps the Socket and its parser alive during delivery.
    SocketUniquePtr socket;
    // First block dequeued by the starter, consumed before later queue entries.
    butil::IOBuf data;
};

struct DeferredTask {
    // Payload references whose user deleters must run outside transport locks.
    butil::IOBuf released;
    // Deferred Socket failures. Callbacks may reenter application or transport
    // code.
    std::vector<SocketFailure> failures;
};

void* RunDeferred(void* arg) {
    std::unique_ptr<DeferredTask> task(static_cast<DeferredTask*>(arg));
    // User IOBuf deleters, like done callbacks, may block or reenter transport
    // code. After DMA is safe, run them on a separate bthread so they do not
    // occupy the CQ progress task.
    task->released.clear();
    for (const auto& failure : task->failures) {
        SocketUniquePtr socket;
        if (Socket::Address(failure.sid, &socket) == 0) {
            socket->SetFailed(failure.error, "SRQ connection closed: %s",
                              berror(failure.error));
        }
    }
    return nullptr;
}
}  // namespace

// Logical owner of shared transport resources, progressed by event-driven
// bthreads without a dedicated pthread. Handles QPN/CQ dispatch, receive refill
// and pending callbacks. Callbacks advance connection state. Protocol parsing,
// Socket callbacks and user deleters run after the relevant transport locks are
// released.
// _events elects one event-loop owner. _progress_mutex also serializes that
// owner with Add and Stop. It remains necessary even with one RunEvent task.
class SrqWorker : public RdmaAsyncEventContext,
                  public std::enable_shared_from_this<SrqWorker> {
    // Lifecycle only advances. BROKEN covers both pending destruction and
    // failed destruction awaiting retry.
    enum class State : uint8_t {
        RUNNING,
        BROKEN,
        DESTROYED,
    };

public:
    struct AsyncFailure {
        // QP number used to locate a connection in the owner table.
        uint32_t qpn;
        // Socket generation checked before applying a delayed failure.
        SocketId sid;
    };

    explicit SrqWorker(bthread_tag_t tag)
        : _tag(tag) { ++g_live_workers; }

    ~SrqWorker() override {
        CHECK(_connections.empty());
        CHECK(_srq == nullptr && _tx_cq == nullptr && _rx_cq == nullptr);
        if (_kick_fd >= 0) {
            close(_kick_fd);
        }
        --g_live_workers;
    }

    bool Initialize();
    bool Add(const ConnectionPtr& connection, uint32_t sq_size);
    void Stop();
    void OnEvent(const bthread_attr_t& attr);
    void NotifyFailure(SocketId sid, int error) {
        _notifications.Enqueue(SocketFailure{sid, error});
        Kick();
    }

    // Signal eventfd only. Do not acquire progress ownership or run callbacks
    // inline. OnEvent coalesces the notification into an existing event task or
    // starts one after the previous task has relinquished ownership.
    void Kick() {
        const uint64_t one = 1;
        if (_kick_fd >= 0) {
            butil::ignore_result(write(_kick_fd, &one, sizeof(one)));
        }
    }

    // Callbacks capture and retain their own context and are responsible for
    // requeueing or handing off progress. Each enqueue executes once. Callers
    // coalesce requests and synchronize publication with close. No publication
    // is allowed after teardown. Callbacks run under _progress_mutex, leaving
    // Socket callbacks and user deleters for execution outside the lock.
    void Schedule(ProgressCallback callback, bool wake) {
        _commands.Enqueue(std::move(callback));
        if (wake) {
            Kick();
        }
    }

    // Reserve for a batch before posting any WR. Each accepted WR retains one
    // reservation until completion/shutdown. The submitter returns only unused
    // or definitely unposted reservations. Control can use the full capacity,
    // while data must leave room for grants that unblock exhausted FC windows.
    uint32_t ReserveTx(uint32_t requested, bool control) {
        uint32_t limit = control ? _tx_capacity : DataTxLimit();
        uint32_t used = _tx_used.load(butil::memory_order_relaxed);
        while (used < limit && !broken()) {
            uint32_t reserved = std::min(requested, limit - used);
            if (_tx_used.compare_exchange_weak(used, used + reserved,
                                               butil::memory_order_acq_rel)) {
                return reserved;
            }
        }
        return 0;
    }

    // Availability snapshot only. Actual submission still requires an atomic
    // ReserveTx reservation.
    bool DataTxAvailable() const {
        return !broken() &&
            _tx_used.load(butil::memory_order_relaxed) < DataTxLimit();
    }

    void ReleaseTx(uint32_t count) {
        if (count != 0) {
            CHECK_GE(_tx_used.fetch_sub(count, butil::memory_order_release), count);
        }
    }

    void FailConnection(const ConnectionPtr& connection) {
        // Queue values rather than connection references. A late callback after
        // teardown must not create a worker -> queue -> connection -> worker
        // ownership cycle.
        _failures.Enqueue(AsyncFailure{connection->_qpn, connection->_sid});
        Kick();
    }

    void Fail() {
        MarkBroken();
        // External and mixed-context callers must always notify progress.
        // An active owner may be exiting, and repeated failures must not lose
        // their wakeup just because another caller already marked BROKEN.
        Kick();
    }

    std::function<void()> PrepareAsyncEvent(ibv_event_type) override {
        auto self = shared_from_this();
        return [self] { self->Fail(); };
    }

    bool broken() const {
        return _state.load(butil::memory_order_acquire) != State::RUNNING;
    }

    bool retired() const {
        return _state.load(butil::memory_order_acquire) == State::DESTROYED;
    }

    void Debug(std::ostream& out, butil::StringPiece connector) {
        State state = _state.load(butil::memory_order_acquire);
        out << connector << "rdma_srq_worker_broken=" << (state != State::RUNNING)
            << connector << "rdma_srq_worker_retired=" << (state == State::DESTROYED)
            << connector << "rdma_srq_tx_outstanding=" << _tx_used.load()
            << connector << "rdma_srq_tx_capacity=" << _tx_capacity;
        // Read owner state only for diagnostics. If the worker is busy, omit
        // receive counts rather than block the query. Pool availability is an
        // O(1) owner count and excludes returns not yet absorbed by refill.
        std::unique_lock<bthread::Mutex> lock(_progress_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }
        const size_t posted = RxOutstanding();
        const size_t available = _pool ? _pool->available() + _rx_reusable.size() : 0;
        const size_t refill = _rx_capacity - posted;
        out << connector << "rdma_srq_pool_free="
            << (available > refill ? available - refill : 0)
            << connector << "rdma_srq_posted=" << posted;
    }
private:
friend class SrqConnection;

    // Mark failure without scheduling another event. Use only when the caller
    // guarantees cleanup through current progress, failed initialization or
    // Stop. Holding a transport lock alone does not provide that guarantee.
    void MarkBroken() {
        State expected = State::RUNNING;
        // Async callbacks may arrive after destruction. Never move DESTROYED
        // back to BROKEN, even when a late Fail still requests a wakeup.
        _state.compare_exchange_strong(expected, State::BROKEN,
                                       butil::memory_order_release,
                                       butil::memory_order_relaxed);
    }

    // Maximum total shared TX CQ occupancy allowed for data WRs, excluding the
    // pure-grant reserve.
    uint32_t DataTxLimit() const {
        return _tx_capacity - FLAGS_rdma_srq_control_reserve;
    }

    static void* RunEvent(void* arg);
    ProgressRound ProgressLocked();
    void DetachEvents();
    // Requires _progress_mutex, shared by RunEvent and Stop. Dequeue only
    // values here, leaving all Socket access and task startup outside the lock.
    std::vector<SocketFailure> TakeNotificationsLocked();
    // Normal progress, failure exits and Stop transfer their completed result
    // here after unlocking, preserving all wakeups and deferred releases.
    void FinishRound(ProgressRound round);
    void DispatchNotifications(butil::IOBuf released,
                               std::vector<SocketFailure> failures);
    size_t RxOutstanding() const { return _rx_buffers.size() - _rx_free.size(); }

    // Both progress and Initialize check broken() after refill. Initialization
    // failure is cleaned up synchronously by Stop before returning a worker.
    void Refill();
    // Drains the CQ on normal return. Publishes failures through broken().
    ProgressRound Poll(bool receive);
    // Dequeue a batch, then invoke each callback once. Callbacks manage their
    // own requeueing.
    ProgressRound RunPending();
    // The caller has observed LAST_WQE and TX reclamation for each candidate.
    // Drain RX again before destruction. Candidates belong only to this round.
    // Return the candidate references with any RX results for release outside
    // the lock. No close phase or refill-pause state persists between rounds.
    ProgressRound RetireConnections(std::vector<ConnectionPtr> connections);
    ReceiveResult HandleReceive(const ibv_wc& wc);
    ConnectionPtr HandleSend(const ibv_wc& wc, butil::IOBuf& released);
    void ConsumeEvents();
    // Stop DMA before releasing references even if a broken CQ cannot be
    // polled. Retain the worker and memory after failed verbs destruction for
    // an explicit retry. The returned round retains notifications and final
    // references even on failure. retired() reports whether cleanup finished.
    ProgressRound DestroyAll();

    // Scheduling tag shared by event and deferred tasks.
    bthread_tag_t _tag;
    // Serializes CQ handling, refill, connection lookup and teardown. Data
    // submission uses only each connection _post_mutex, allowing parallel sends
    // across QPs. Lock order is _progress_mutex then connection _post_mutex,
    // never the reverse. Normal maintenance uses try_lock. Failure teardown may
    // wait, so both are bthread mutexes.
    bthread::Mutex _progress_mutex;
    // Multiple producers publish Socket failures. RunEvent and Stop dequeue
    // under _progress_mutex and transfer each batch to their local round for
    // dispatch outside the lock.
    butil::MPSCQueue<SocketFailure, butil::ObjectPoolAllocator<SocketFailure>> _notifications;
    // Completion-notification channel shared by both CQs.
    ibv_comp_channel* _channel{nullptr};
    // Send CQ shared by all attached QPs.
    ibv_cq* _tx_cq{nullptr};
    // Shared receive CQ. CQEs identify the QP and receive slot.
    ibv_cq* _rx_cq{nullptr};
    // Hardware receive queue shared by all attached RC QPs.
    ibv_srq* _srq{nullptr};
    // Worker reference to the receive pool. Exported zero-copy blocks retain it
    // independently.
    SrqReceivePool* _pool{nullptr};
    // Nonblocking eventfd for software work and failures.
    int _kick_fd{-1};
    // Target posted-receive depth, bounded by configured and actual SRQ/RX CQ
    // capacities.
    uint32_t _rx_capacity{0};
    // Actual TX CQ capacity returned by verbs, fixed after initialization.
    uint32_t _tx_capacity{0};
    // Shared CQ reservations for prepared or unreclaimed data/control WRs.
    // Concurrent submitters reserve atomically. Completion or rollback returns
    // each reservation exactly once.
    butil::atomic<uint32_t> _tx_used{0};
    // Any thread may publish RUNNING -> BROKEN, causing submitters to stop
    // reserving capacity once observed. The owner publishes DESTROYED after
    // reclaiming QPs, SRQ, CQs and receive accounting, allowing registry
    // replacement.
    butil::atomic<State> _state{State::RUNNING};
    // Versioned event-source ID borrowing completion, software-wakeup and
    // recycle fds.
    VRefId _event_source{INVALID_VREF_ID};
    // Event notification generation. Zero means no task owns the event loop.
    // Coalesces notifications while preserving the handoff to the next owner.
    butil::atomic<uint64_t> _events{0};
    // Multi-producer callback queue. Each callback captures its own context and
    // references.
    ProgressQueue _commands;
    // Async QP failures stored by value to avoid connection ownership cycles.
    butil::MPSCQueue<AsyncFailure, butil::ObjectPoolAllocator<AsyncFailure>> _failures;
    // QPN-to-connection ownership table. Remove entries only after safe QP
    // teardown. Pointers returned by `seek()` stay inside `_progress_mutex`
    // and must not survive a table mutation. Deferred work copies the value.
    butil::FlatMap<uint32_t, ConnectionPtr> _connections;
    // Owner-only receive slots. Nonzero wr_id encodes the index plus one.
    // Completion requires neither a hash-node allocation nor dereferencing a
    // NIC-returned pointer.
    std::vector<SrqReceivePool::Buffer*> _rx_buffers;
    // Small-message copies export no BlockRef, so retain the block lease and
    // initialized descriptor for refill. The worker owns these blocks between
    // CQ progress and refill, including failure cleanup.
    std::vector<SrqReceivePool::Buffer*> _rx_reusable;
    // Free indices in _rx_buffers available for the next receive refill.
    std::vector<uint32_t> _rx_free;
};

namespace {
// Borrows three worker fds without transferring ownership to Socket. Dispatcher
// IDs include a generation. In-flight callbacks retain the event source and
// worker instead of touching a recycled replacement.
class SrqEventSource : public VersionedRefWithId<SrqEventSource> {
public:
    explicit SrqEventSource(Forbidden f) : VersionedRefWithId(f) {}

    int OnCreated(const std::shared_ptr<SrqWorker>& worker, bthread_tag_t tag) {
        _worker = worker;
        _io.set_bthread_tag(tag);
        return _io.Init(reinterpret_cast<void*>(id()));
    }

    void BeforeRecycled() {
        _io.Reset();
        _worker.reset();
    }

    int Add(int fd) { return _io.AddConsumer(fd); }

    void Remove(int fd) { _io.RemoveConsumer(fd); }

    static int OnInputEvent(void* arg, uint32_t, const bthread_attr_t& attr) {
        VersionedRefWithIdUniquePtr<SrqEventSource> source;
        if (Address(reinterpret_cast<uintptr_t>(arg), &source) != 0) {
            return -1;
        }
        source->_worker->OnEvent(attr);
        return 0;
    }

    static int OnOutputEvent(void*, uint32_t, const bthread_attr_t&) { return 0; }

private:
    // Retains the worker while fd callbacks may still access this event source.
    std::shared_ptr<SrqWorker> _worker;
    // EventDispatcher registration context shared by the three borrowed fds.
    IOEvent<SrqEventSource> _io;
};

struct WorkerRegistry {
    // Protects worker lookup, generation replacement and failed-destruction
    // quarantine.
    bthread::Mutex mutex;
    // Current worker for each (bthread tag, configured shard).
    std::map<std::pair<bthread_tag_t, int>, std::shared_ptr<SrqWorker>> workers;
    // Workers retaining DMA resources after failed destruction, retried by
    // explicit Stop.
    std::vector<std::shared_ptr<SrqWorker>> quarantine;
};

// The registry outlives static destruction and late application IOBuf releases.
WorkerRegistry& Registry() {
    static auto* registry = new WorkerRegistry;
    return *registry;
}

std::shared_ptr<SrqWorker> GetWorker(bthread_tag_t tag, int index) {
    auto& registry = Registry();
    std::unique_lock<bthread::Mutex> lock(registry.mutex);
    auto key = std::make_pair(tag, index);
    auto it = registry.workers.find(key);
    if (it != registry.workers.end()) {
        if (!it->second->retired()) {
            return it->second;
        }
        // Replace only after all old-generation DMA resources have been
        // reclaimed. Late endpoint/event references may retain the old context
        // without affecting lookup of the replacement.
        registry.workers.erase(it);
    }
    auto worker = std::make_shared<SrqWorker>(tag);
    if (!worker->Initialize()) {
        const int saved_errno = errno;
        lock.unlock();
        worker->Stop();
        worker.reset();
        errno = saved_errno;
        return {};
    }
    registry.workers.emplace(key, worker);
    return worker;
}
}  // namespace

bool ValidateSrqOptions() {
    if (!FLAGS_rdma_use_srq) {
        return true;
    }
    if (FLAGS_rdma_use_polling) {
        LOG(ERROR) << "rdma_use_srq requires event mode (rdma_use_polling=false)";
        return false;
    }
    auto check = [](bool valid, const char* name, int value,
                    const std::string& range) {
        if (!valid) {
            LOG(ERROR) << name << '=' << value << " is invalid; expected " << range;
        }
        return valid;
    };
    return check(SrqFlowControl::ValidWindow(FLAGS_rdma_srq_window),
                 "rdma_srq_window", FLAGS_rdma_srq_window, "[4, 65535]") &&
           check(FLAGS_rdma_srq_workers > 0,
                 "rdma_srq_workers", FLAGS_rdma_srq_workers, "> 0") &&
           check(FLAGS_rdma_srq_sq_size >= 16 && FLAGS_rdma_srq_sq_size <= 4096,
                 "rdma_srq_sq_size", FLAGS_rdma_srq_sq_size, "[16, 4096]") &&
           check(FLAGS_rdma_srq_max_sge > 0 && FLAGS_rdma_srq_max_sge <= 32,
                 "rdma_srq_max_sge", FLAGS_rdma_srq_max_sge, "[1, 32]") &&
           check(FLAGS_rdma_srq_inline_size >= 0 && FLAGS_rdma_srq_inline_size <= 236,
                 "rdma_srq_inline_size", FLAGS_rdma_srq_inline_size, "[0, 236]") &&
           check(FLAGS_rdma_srq_depth >= 32,
                 "rdma_srq_depth", FLAGS_rdma_srq_depth, ">= 32") &&
           check(FLAGS_rdma_srq_pool_blocks > FLAGS_rdma_srq_depth,
                 "rdma_srq_pool_blocks", FLAGS_rdma_srq_pool_blocks,
                 "> rdma_srq_depth=" + std::to_string(FLAGS_rdma_srq_depth)) &&
           check(FLAGS_rdma_srq_pool_grow_blocks > 0,
                 "rdma_srq_pool_grow_blocks", FLAGS_rdma_srq_pool_grow_blocks, "> 0") &&
           check(FLAGS_rdma_srq_pool_growth_retry_ms > 0 &&
                 FLAGS_rdma_srq_pool_growth_retry_ms <= 60000,
                 "rdma_srq_pool_growth_retry_ms", FLAGS_rdma_srq_pool_growth_retry_ms,
                 "[1, 60000]") &&
           check(FLAGS_rdma_srq_control_reserve > 0,
                 "rdma_srq_control_reserve", FLAGS_rdma_srq_control_reserve, "> 0") &&
           check(FLAGS_rdma_srq_tx_cq_size > FLAGS_rdma_srq_control_reserve,
                 "rdma_srq_tx_cq_size", FLAGS_rdma_srq_tx_cq_size,
                 "> rdma_srq_control_reserve=" + std::to_string(FLAGS_rdma_srq_control_reserve));
}

// Check reported capabilities independently of vendor and device name.
// Actual CQ/SRQ/RC QP creation remains the final
// resource-availability check.
bool ValidateSrqDevice(ibv_context* context) {
    ibv_device_attr attr = {};
    const int rc = IbvQueryDevice(context, &attr);
    if (rc != 0) {
        errno = rc;
        PLOG(ERROR) << "Fail to query RDMA device for SRQ";
        return false;
    }

    if (attr.max_srq <= 0 || attr.max_srq_sge < 1) {
        LOG(ERROR) << "RDMA device does not support SRQ receives: max_srq="
                   << attr.max_srq << ", max_srq_sge=" << attr.max_srq_sge;
        errno = ENOTSUP;
        return false;
    }

    if (FLAGS_rdma_srq_depth > attr.max_srq_wr ||
        FLAGS_rdma_srq_depth > attr.max_cqe ||
        FLAGS_rdma_srq_tx_cq_size > attr.max_cqe) {
        LOG(ERROR) << "SRQ configuration exceeds RDMA device capacity: depth="
                   << FLAGS_rdma_srq_depth << ", tx_cq_size="
                   << FLAGS_rdma_srq_tx_cq_size << ", max_srq_wr="
                   << attr.max_srq_wr << ", max_cqe=" << attr.max_cqe;
        errno = EINVAL;
        return false;
    }
    return true;
}

bool SrqWorker::Initialize() {
    std::lock_guard<bthread::Mutex> owner(_progress_mutex);
    if (!ValidateSrqDevice(GetRdmaContext())) {
        return false;
    }

    _kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (_kick_fd < 0) {
        return false;
    }

    _channel = IbvCreateCompChannel(GetRdmaContext());
    if (!_channel || butil::make_non_blocking(_channel->fd) < 0 ||
        butil::make_close_on_exec(_channel->fd) < 0) {
        return false;
    }

    auto* context = static_cast<RdmaAsyncEventContext*>(this);
    _tx_cq = IbvCreateCq(GetRdmaContext(), FLAGS_rdma_srq_tx_cq_size,
                        context, _channel, GetRdmaCompVector());
    if (!_tx_cq) {
        return false;
    }

    RegisterRdmaAsyncEventContext(_tx_cq, context);
    _rx_cq = IbvCreateCq(GetRdmaContext(), FLAGS_rdma_srq_depth,
                        context, _channel, GetRdmaCompVector());
    if (!_rx_cq) {
        return false;
    }

    RegisterRdmaAsyncEventContext(_rx_cq, context);
    _tx_capacity = _tx_cq->cqe;
    ibv_srq_init_attr attr = {};
    attr.srq_context = context;
    attr.attr.max_wr = FLAGS_rdma_srq_depth;
    attr.attr.max_sge = 1;
    _srq = IbvCreateSrq(GetRdmaPd(), &attr);
    if (!_srq) {
        return false;
    }

    RegisterRdmaAsyncEventContext(_srq, context);
    _rx_capacity = std::min(static_cast<uint32_t>(_rx_cq->cqe),
                           std::min(attr.attr.max_wr,
                                       static_cast<uint32_t>(FLAGS_rdma_srq_depth)));
    uint32_t limit = FLAGS_rdma_srq_pool_blocks;
    uint32_t grow_blocks = std::min(limit,
        static_cast<uint32_t>(FLAGS_rdma_srq_pool_grow_blocks));
    // Retain spare blocks beyond the posted depth so the first zero-copy
    // receive need not wait for registration. Initialize with one MR. Runtime
    // growth has its own, smaller granularity.
    uint32_t spare = std::min(limit - _rx_capacity,
                              std::max(grow_blocks, (_rx_capacity + 9) / 10));
    _pool = SrqReceivePool::Create(GetRdmaPd(), GetRdmaBlockSize(), limit,
                                   grow_blocks, _rx_capacity + spare);
    if (_pool == nullptr) {
        return false;
    }

    _rx_buffers.resize(_rx_capacity, nullptr);
    _rx_free.reserve(_rx_capacity);
    _rx_reusable.reserve(_rx_capacity);
    for (uint32_t i = 0; i < _rx_capacity; ++i) {
        _rx_free.push_back(i);
    }
    Refill();
    if (broken() || RxOutstanding() == 0) {
        return false;
    }

    if (SrqEventSource::Create(&_event_source, shared_from_this(), _tag) != 0) {
        return false;
    }

    VersionedRefWithIdUniquePtr<SrqEventSource> source;
    CHECK_EQ(0, SrqEventSource::Address(_event_source, &source));
    if (source->Add(_channel->fd) != 0 || source->Add(_kick_fd) != 0 ||
        source->Add(_pool->recycle_fd()) != 0) {
        return false;
    }

    Kick();
    return true;
}

bool SrqWorker::Add(const ConnectionPtr& connection, uint32_t sq_size) {
    std::lock_guard<bthread::Mutex> owner(_progress_mutex);
    if (broken()) {
        errno = ERDMA;
        return false;
    }

    ibv_qp_init_attr attr = {};
    // Store the RdmaAsyncEventContext base address expected by async dispatch.
    // With multiple inheritance, it may differ from the most-derived object
    // address.
    attr.qp_context = static_cast<RdmaAsyncEventContext*>(connection.get());
    attr.send_cq = _tx_cq;
    attr.recv_cq = _rx_cq;
    attr.srq = _srq;
    attr.qp_type = IBV_QPT_RC;
    attr.cap.max_send_wr = sq_size;
    attr.cap.max_send_sge = std::min(GetRdmaMaxSge(), FLAGS_rdma_srq_max_sge);
    attr.cap.max_inline_data = FLAGS_rdma_srq_inline_size;
    connection->_qp = IbvCreateQp(GetRdmaPd(), &attr);
    // Inline support is optional. Retry without requesting it if the device
    // rejects the requested size.
    if (!connection->_qp && attr.cap.max_inline_data) {
        attr.cap.max_inline_data = 0;
        connection->_qp = IbvCreateQp(GetRdmaPd(), &attr);
    }
    if (!connection->_qp) {
        return false;
    }

    RegisterRdmaAsyncEventContext(connection->_qp,
        static_cast<RdmaAsyncEventContext*>(connection.get()));
    connection->_qpn = connection->_qp->qp_num;
    connection->_sq_size = std::min(sq_size, attr.cap.max_send_wr);
    connection->_max_sge = std::min(
        attr.cap.max_send_sge, static_cast<uint32_t>(FLAGS_rdma_srq_max_sge));
    connection->_inline_size = std::min(
        static_cast<uint32_t>(FLAGS_rdma_srq_inline_size),
        attr.cap.max_inline_data);
    connection->_records =
        std::make_unique<SrqConnection::TxRecord[]>(connection->_sq_size);
    CHECK(_connections.seek(connection->_qpn) == nullptr);
    _connections[connection->_qpn] = connection;
    ++g_live_connections;
    return true;
}

void SrqWorker::Refill() {
    if (broken() || !_pool) {
        return;
    }

    _pool->Prepare(_rx_reusable.size());

    while (!_rx_free.empty()) {
        ibv_recv_wr wr[kBatch] = {};
        ibv_sge sge[kBatch] = {};
        uint32_t slots[kBatch];
        int count = 0;
        while (count < kBatch && !_rx_free.empty()) {
            SrqReceivePool::Buffer* buffer;
            if (!_rx_reusable.empty()) {
                buffer = _rx_reusable.back();
                _rx_reusable.pop_back();
            } else {
                buffer = _pool->Acquire();
            }
            if (buffer == nullptr) {
                break;
            }
            const uint32_t slot = _rx_free.back();
            _rx_free.pop_back();
            CHECK(_rx_buffers[slot] == nullptr);
            _rx_buffers[slot] = buffer;
            slots[count] = slot;
            sge[count].addr = reinterpret_cast<uintptr_t>(buffer->data());
            sge[count].length = buffer->capacity();
            sge[count].lkey = buffer->lkey();
            wr[count].wr_id = slot + 1;
            wr[count].sg_list = &sge[count];
            wr[count].num_sge = 1;
            if (count) {
                wr[count - 1].next = &wr[count];
            }
            ++count;
        }
        if (!count) {
            break;
        }
        ibv_recv_wr* bad = nullptr;
        const int rc = IbvPostSrqRecv(_srq, wr, &bad);
        int posted = count;
        if (rc != 0) {
            // bad_wr identifies the first unposted WR. Only the preceding
            // prefix belongs to hardware. An unrecognized pointer leaves all
            // possibly submitted blocks retained until DMA stops.
            posted = 0;
            while (posted < count && bad != &wr[posted]) {
                ++posted;
            }
            MarkBroken();
        }
        for (int i = posted; i < count; ++i) {
            const uint32_t slot = slots[i];
            auto* buffer = _rx_buffers[slot];
            _rx_buffers[slot] = nullptr;
            _rx_free.push_back(slot);
            buffer->clear();
        }
        if (rc) {
            break;
        }
    }
}

ConnectionPtr SrqWorker::HandleSend(const ibv_wc& wc, butil::IOBuf& released) {
    auto* connection = _connections.seek(wc.qp_num);
    if (connection == nullptr) {
        MarkBroken();
        return {};
    }

    if ((*connection)->CompleteSend(wc, released)) {
        return *connection;
    }
    return {};
}

ReceiveResult SrqWorker::HandleReceive(const ibv_wc& wc) {
    ReceiveResult result;
    if (wc.wr_id == 0 || wc.wr_id > _rx_buffers.size()) {
        MarkBroken();
        return result;
    }

    uint32_t slot = wc.wr_id - 1;
    auto* buffer = _rx_buffers[slot];
    if (!buffer) {
        MarkBroken();
        return result;
    }

    // Completion releases the receive slot independently of the physical block.
    // Zero-copy delivery can retain this block in an application IOBuf while
    // Refill posts a different pool block into the same slot. Only the final
    // BlockRef permits the old memory to return to the pool.
    _rx_buffers[slot] = nullptr;
    _rx_free.push_back(slot);
    auto* found = _connections.seek(wc.qp_num);
    if (found == nullptr ||
        (*found)->_closing.load(butil::memory_order_acquire)) {
        _rx_reusable.push_back(buffer);
        return result;
    }

    auto connection = *found;
    result.connection = connection;
    if (wc.status != IBV_WC_SUCCESS || (wc.wc_flags & IBV_WC_WITH_IMM) == 0 ||
        wc.byte_len > buffer->capacity()) {
        _rx_reusable.push_back(buffer);
        connection->RequestClose(EPROTO);
        result.wake_writer = true;
        return result;
    }

    uint32_t flags = ntohl(wc.imm_data);
    bool valid = connection->ReceiveControl(flags, wc.byte_len);
    if (!valid) {
        _rx_reusable.push_back(buffer);
        connection->RequestClose(EPROTO);
        result.wake_writer = true;
        return result;
    }

    result.wake_writer = flags & SrqFlowControl::GRANT;
    if (wc.byte_len == 0) {
        // Pure grants consume an SRQ slot but export no block reference. Keep
        // the existing lease, just as for a completed receive copied below.
        _rx_reusable.push_back(buffer);
        return result;
    }

    bool zero_copy = FLAGS_rdma_recv_zerocopy &&
        wc.byte_len >= static_cast<uint32_t>(FLAGS_rdma_zerocopy_min_size);
    auto received = zero_copy ? buffer->TakeReceived(wc.byte_len) : butil::IOBuf();
    if (!zero_copy) {
        int rc = received.append(buffer->data(), wc.byte_len);
        // DMA is complete and no BlockRef was exported. Reuse this block and
        // descriptor directly, avoiding registry lookup, cross-thread return
        // and Block reconstruction.
        _rx_reusable.push_back(buffer);
        if (rc != 0) {
            // Ordinary registered-pool exhaustion may leave append with only a
            // partial copy. A reliable byte stream cannot deliver that prefix
            // as a complete receive.
            connection->RequestClose(ENOMEM);
            result.wake_writer = true;
            return result;
        }
    }
    result.deliver = connection->EnqueueReceived(received.movable());
    return result;
}

ProgressRound SrqWorker::Poll(bool receive) {
    ProgressRound round;
    ibv_wc wc[kBatch];
    while (!broken()) {
        int count = IbvPollSrqCq(receive ? _rx_cq : _tx_cq, kBatch, wc);
        if (count < 0) {
            MarkBroken();
            break;
        }
        if (count == 0) {
            break;
        }

        round.completed = true;
        uint32_t released = 0;
        for (int i = 0; i < count; ++i) {
            if (receive) {
                auto result = HandleReceive(wc[i]);
                if (result.wake_writer) {
                    round.writers.insert(result.connection);
                }
                if (result.deliver) {
                    round.deliveries.insert(result.connection);
                }
            } else {
                auto connection = HandleSend(wc[i], round.released);
                if (connection != nullptr) {
                    ++released;
                    round.writers.insert(std::move(connection));
                }
            }
        }
        // SQ records have already been published as reusable individually.
        // Return CQ reservations once per batch. Delaying this return only
        // underuses capacity temporarily. It cannot allow CQ overcommit.
        ReleaseTx(released);
    }
    return round;
}

ProgressRound SrqWorker::RunPending() {
    ProgressRound round;
    // Fix the execution range before invoking callbacks. Requeued callbacks
    // stay in _commands until a later RunPending call. Executing them again in
    // this batch would spin on unavailable resources and monopolize CQ
    // progress. Keep all dequeued callbacks in round so captured contexts are
    // released outside _progress_mutex.
    ProgressCallback callback;
    while (_commands.Dequeue(callback)) {
        round.callbacks.push_back(std::move(callback));
    }
    size_t end = round.callbacks.size();
    for (size_t i = 0; i < end; ++i) {
        round.Append(round.callbacks[i]());
    }
    return round;
}

ProgressRound SrqWorker::RetireConnections(
    std::vector<ConnectionPtr> connections) {
    ProgressRound round;
    round.retiring = std::move(connections);
    if (round.retiring.empty() || broken()) {
        return round;
    }

    // Collect ready candidates before draining RX: an empty poll from before
    // observing LAST_WQE is not a teardown barrier. Poll handles all
    // connections and retains normal deliveries in this round. No Refill occurs
    // during this drain, so the posted receive set is finite and no persistent
    // refill-pause state is needed.
    round.Append(Poll(true));
    if (broken()) {
        return round;
    }
    // Each ProgressLocked call passes only its own candidates. Results from
    // earlier progress remain in RunEvent and are never retired twice.
    for (auto& candidate : round.retiring) {
        auto* connection = candidate.get();
        SrqConnection::PostLock lock(connection, std::try_to_lock);
        if (!lock.owns_lock()) {
            // Queries or close may acquire the submit lock after candidate
            // collection. Requeue and repeat the RX barrier on the next
            // retirement attempt.
            connection->EnqueueProgressLocked(false);
            continue;
        }
        if (!connection->DestroyQpLocked()) {
            // The QPN table retains the undestroyed connection for worker-wide
            // failure cleanup.
            MarkBroken();
            break;
        }
        _connections.erase(connection->_qpn);
        --g_live_connections;
    }
    return round;
}

void SrqWorker::ConsumeEvents() {
    if (!_channel) {
        return;
    }
    ibv_cq* cq;
    void* context;
    unsigned int tx_events = 0;
    unsigned int rx_events = 0;
    while (IbvGetCqEvent(_channel, &cq, &context) == 0) {
        if (cq == _tx_cq) {
            ++tx_events;
        } else {
            CHECK_EQ(cq, _rx_cq);
            ++rx_events;
        }
    }
    // Preserve the get error before batching acknowledgements. Acknowledge
    // every retrieved event even on failure, or later CQ destruction may wait
    // forever for its acknowledgement.
    int error = errno;
    if (tx_events) {
        IbvAckCqEvents(_tx_cq, tx_events);
    }
    if (rx_events) {
        IbvAckCqEvents(_rx_cq, rx_events);
    }
    if (error != EAGAIN) {
        MarkBroken();
    }
}

ProgressRound SrqWorker::ProgressLocked() {
    ProgressRound round;
    State state = _state.load(butil::memory_order_acquire);
    if (state == State::DESTROYED) {
        return round;
    }
    if (state == State::BROKEN) {
        return DestroyAll();
    }

    AsyncFailure failure;
    while (_failures.Dequeue(failure)) {
        auto* connection = _connections.seek(failure.qpn);
        if (connection != nullptr && (*connection)->_sid == failure.sid) {
            (*connection)->RequestClose(ERDMA);
            round.writers.insert(*connection);
        }
    }
    round.Append(Poll(false));
    round.Append(Poll(true));
    if (broken()) {
        round.Append(DestroyAll());
        return round;
    }
    auto pending = RunPending();
    round.Append(RetireConnections(std::move(pending.retiring)));
    round.Append(std::move(pending));
    Refill();
    if (broken()) {
        round.Append(DestroyAll());
    }
    return round;
}

void SrqWorker::OnEvent(const bthread_attr_t& thread_attr) {
    // Only the zero-to-one transition starts an event task. Other notifications
    // change the running owner's exit snapshot, forcing it to check again.
    // This counter tracks notifications, not CQEs or pending callbacks.
    if (_events.fetch_add(1, butil::memory_order_acq_rel) != 0) {
        return;
    }

    auto task = std::make_unique<std::shared_ptr<SrqWorker>>(shared_from_this());
    bthread_attr_t attr = thread_attr;
    attr.tag = _tag;
    bthread_t tid;
    int rc = EventDispatcherUnsched() ?
        bthread_start_background(&tid, &attr, RunEvent, task.get()) :
        bthread_start_urgent(&tid, &attr, RunEvent, task.get());
    if (rc != 0) {
        _events.store(0, butil::memory_order_release);
        Fail();
        return;
    }
    task.release();
}

void* SrqWorker::RunEvent(void* arg) {
    std::unique_ptr<std::shared_ptr<SrqWorker>> task(
        static_cast<std::shared_ptr<SrqWorker>*>(arg));
    SrqWorker* worker = task->get();
    for (;;) {
        // Keep per-round results outside the lock scope so early exits also
        // release references after unlocking. Membership deduplication stays
        // local. No connection or worker flags need resetting between rounds.
        ProgressRound round;
        uint64_t events = worker->_events.load(butil::memory_order_acquire);
        bool more = false;
        bool stopped = false;
        {
            std::lock_guard<bthread::Mutex> owner(worker->_progress_mutex);
            if (!worker->retired()) {
                worker->ConsumeEvents();
                DrainEventFd(worker->_kick_fd);
                DrainEventFd(worker->_pool->recycle_fd());
                worker->_pool->DisarmRecyclingNotification();
                round.Append(worker->ProgressLocked());
            }
            if (!worker->broken()) {
                // Arm before rechecking. CQEs or returned blocks arriving as
                // the worker prepares to sleep must either be observed by the
                // recheck or leave a notification.
                if (ibv_req_notify_cq(worker->_tx_cq, 0) != 0 ||
                    ibv_req_notify_cq(worker->_rx_cq, 0) != 0) {
                    worker->MarkBroken();
                    round.Append(worker->ProgressLocked());
                } else {
                    // A full SRQ needs no return notification. Blocks can stay
                    // on the return stack until later CQ progress needs them.
                    bool recyclable = !worker->_rx_free.empty() &&
                                      !worker->_pool->ArmRecyclingNotification();
                    // Pre-arm completions have already been handled and alone
                    // do not justify another round. An RX drain during the
                    // recheck still sets completed on its own result,
                    // preserving any follow-up progress it requires.
                    ProgressRound recheck = worker->ProgressLocked();
                    if (!worker->broken() && worker->_rx_free.empty()) {
                        worker->_pool->DisarmRecyclingNotification();
                        recyclable = false;
                    }
                    // RX completions in the recheck may create a shortage
                    // after we skipped arming. `completed` forces another
                    // round to subscribe before the worker can sleep.
                    more = recyclable || recheck.completed;
                    round.Append(std::move(recheck));
                }
            }
#if defined(UNIT_TEST)
            // Retired-generation tasks no longer access test observers, making
            // Stop a barrier for observer removal.
            auto stop_hook = worker->retired() ? nullptr :
                SrqWorkerStopHookForTest.load(butil::memory_order_acquire);
            if (stop_hook != nullptr) {
                stop_hook(worker, false, &worker->_events, worker->_pool);
            }
#endif
            // Use one state snapshot for cleanup and the exit decision. A
            // second load could observe BROKEN and exit without running
            // DestroyAll.
            State state = worker->_state.load(butil::memory_order_acquire);
            stopped = state != State::RUNNING;
#if defined(UNIT_TEST)
            if (stop_hook != nullptr) {
                stop_hook(worker, true, &worker->_events, worker->_pool);
            }
#endif
            if (state == State::BROKEN) {
                // External Fail may race after the last check in
                // ProgressLocked, with its Kick already coalesced into this
                // task. Complete destruction before FinishRound outside the
                // lock. Failure after this snapshot is handled by the _events
                // CAS or a new event task. DestroyAll detaches events first.
                // Failed destruction retains resources for Stop to retry.
                round.Append(worker->DestroyAll());
            }
            // Include notifications from teardown before releasing ownership.
            // A later producer calls Kick, preserving the event exit handoff.
            round.failures = worker->TakeNotificationsLocked();
        }
        // Finalize even on failure exits. Never leave the last wakeup or
        // user-reference release inside the owner lock.
        worker->FinishRound(std::move(round));
        // A concurrent OnEvent before this CAS changes the snapshot and keeps
        // this owner running. After a successful CAS, OnEvent sees zero and
        // starts a new owner. A Kick not yet dispatched remains in eventfd.
        // Stopped workers have detached their event source in DestroyAll, so
        // they do not need to hand ownership back to another event task.
        if (stopped || (!more && worker->_events.compare_exchange_strong(
                events, 0, butil::memory_order_acq_rel))) {
            break;
        }
    }
    return nullptr;
}

std::vector<SocketFailure> SrqWorker::TakeNotificationsLocked() {
    std::vector<SocketFailure> failures;
    SocketFailure failure;
    while (_notifications.Dequeue(failure)) {
        failures.push_back(failure);
    }
    return failures;
}

void SrqWorker::FinishRound(ProgressRound round) {
    DispatchNotifications(round.released.movable(), std::move(round.failures));
    for (auto& connection : round.writers) {
        connection->WakeWriter();
    }
    for (auto& connection : round.deliveries) {
        connection->StartDelivery(true);
    }
}

void SrqWorker::DispatchNotifications(
    butil::IOBuf released, std::vector<SocketFailure> failures) {
    if (released.empty() && failures.empty()) {
        return;
    }

    auto task = std::make_unique<DeferredTask>();
    task->released.swap(released);
    task->failures = std::move(failures);
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.tag = _tag;
    bthread_t tid;
    // Socket failure may synchronously invoke application done callbacks. No
    // transport lock is held here, including the synchronous fallback after
    // bthread creation fails.
    if (bthread_start_background(&tid, &attr, RunDeferred, task.get()) == 0) {
        task.release();
        return;
    }
    RunDeferred(task.release());
}

void SrqWorker::DetachEvents() {
    if (_event_source == INVALID_VREF_ID) {
        return;
    }

    VersionedRefWithIdUniquePtr<SrqEventSource> source;
    if (SrqEventSource::Address(_event_source, &source) == 0) {
        if (_channel) {
            source->Remove(_channel->fd);
        }
        source->Remove(_kick_fd);
        if (_pool) {
            source->Remove(_pool->recycle_fd());
        }
        source->SetFailed();
    }
    _event_source = INVALID_VREF_ID;
}

ProgressRound SrqWorker::DestroyAll() {
    ProgressRound round;
    // Worker failure/Stop cannot rely on healthy CQs or normal LAST_WQE/TX/RX
    // progress. The caller holds _progress_mutex. Detach events and stop
    // submission on every connection. Destroy QPs, the SRQ and CQs before
    // reclaiming remaining records and receive blocks. This path may block on
    // _post_mutex to wait for in-flight post_send calls to return.
    //
    // On any verbs destruction failure, keep State::BROKEN and all remaining
    // DMA references. Successfully destroyed handles stay null for a later
    // Stop retry. Only complete cleanup publishes DESTROYED and permits a new
    // worker generation. Queued application notifications run outside the lock.
    if (retired()) {
        return round;
    }

    DetachEvents();
    std::vector<ConnectionPtr> connections;
    for (auto& entry : _connections) {
        connections.push_back(entry.second);
    }
    for (auto& connection : connections) {
        connection->RequestClose(ERDMA);
        round.writers.insert(connection);
    }

    bool stopped = true;
    for (auto& connection : connections) {
        SrqConnection::PostLock lock(connection.get());
        if (!connection->DestroyQpLocked()) {
            stopped = false;
        }
    }
    if (!stopped) {
        return round;
    }

    // QP destruction may discard CQEs. Stop the entire SRQ before reclaiming
    // receive accounting. Discarded CQEs cannot be reliably matched to receive
    // blocks.
    if (_srq != nullptr) {
        UnregisterRdmaAsyncEventContext(_srq);
        if (IbvDestroySrq(_srq) != 0) {
            return round;
        }
        _srq = nullptr;
    }
    ConsumeEvents();
    if (_tx_cq != nullptr) {
        UnregisterRdmaAsyncEventContext(_tx_cq);
        if (IbvDestroyCq(_tx_cq) != 0) {
            return round;
        }
        _tx_cq = nullptr;
    }
    if (_rx_cq != nullptr) {
        UnregisterRdmaAsyncEventContext(_rx_cq);
        if (IbvDestroyCq(_rx_cq) != 0) {
            return round;
        }
        _rx_cq = nullptr;
    }
    if (_channel != nullptr) {
        if (IbvDestroyCompChannel(_channel) != 0) {
            return round;
        }
        _channel = nullptr;
    }
    for (auto& connection : connections) {
        SrqConnection::PostLock lock(connection.get());
        connection->RetireSendsLocked(round.released);
        --g_live_connections;
    }
    _connections.clear();
    for (auto& connection : connections) {
        round.retiring.push_back(std::move(connection));
    }
    // All QPs are closed and progress registrations cleared, so publishers can
    // no longer enqueue connection work. Retain cancelled callbacks in round:
    // releasing their final captured references under _progress_mutex could
    // reenter transport or application code.
    ProgressCallback callback;
    while (_commands.Dequeue(callback)) {
        round.callbacks.push_back(std::move(callback));
    }
    AsyncFailure failure;
    while (_failures.Dequeue(failure)) {}
    for (auto* buffer : _rx_buffers) {
        if (buffer != nullptr) {
            buffer->clear();
        }
    }
    _rx_buffers.clear();
    for (auto* buffer : _rx_reusable) {
        buffer->clear();
    }
    _rx_reusable.clear();
    _rx_free.clear();
    if (_pool != nullptr) {
        // Exported blocks and a pending growth task retain the pool. Release
        // stops new growth. An in-flight registration can finish and clean up
        // independently after this worker detaches its event source.
        _pool->Release();
        _pool = nullptr;
    }
    _state.store(State::DESTROYED, butil::memory_order_release);
    return round;
}

void SrqWorker::Stop() {
    // Stop performs cleanup itself, including when no event owner is running.
    MarkBroken();
    ProgressRound round;
    {
        std::lock_guard<bthread::Mutex> owner(_progress_mutex);
        round.Append(DestroyAll());
        if (!retired()) {
            // Retain typed async contexts, MRs and every reference that may
            // still be used by DMA.
            auto& registry = Registry();
            std::lock_guard<bthread::Mutex> lock(registry.mutex);
            registry.quarantine.push_back(shared_from_this());
            LOG(ERROR) << "Retaining SRQ worker after failed verbs destruction";
        }
        // DestroyAll can enqueue failures even when verbs cleanup fails.
        // Detach them before unlocking, just as the event owner does.
        round.failures = TakeNotificationsLocked();
    }
    FinishRound(std::move(round));
}

SrqConnection::SrqConnection(const std::shared_ptr<SrqWorker>& worker,
                             Socket* socket, bthread_tag_t tag)
    : _worker(worker)
    , _sid(socket->id())
    , _tag(tag) {}

SrqConnection::~SrqConnection() {
    CHECK(_qp == nullptr);
}

SrqConnection::PostLock::PostLock(const SrqConnection* connection)
    : _connection(connection), _locked(true) {
    _connection->_post_mutex.lock();
}

SrqConnection::PostLock::PostLock(const SrqConnection* connection,
                                  std::try_to_lock_t)
    : _connection(connection), _locked(connection->_post_mutex.try_lock()) {
    if (!_locked) {
#if defined(UNIT_TEST)
        auto hook = SrqPostLockRetryHookForTest.load(butil::memory_order_acquire);
        if (hook) {
            hook();
        }
#endif
        // Pair registration with the unlocker exchange. If registration happens
        // first, an unlocker takes the request and calls Kick. If the holder
        // already unlocked and checked the flag, this acquire follows that
        // check and the second try_lock can acquire the now-free mutex. Without
        // this retry, no holder may remain to send a notification.
        //
        // If the second attempt still fails, keep the request for a subsequent
        // unlock path. Other events can run this callback while the same or a
        // new sender still holds the lock. Retrying need not succeed. No
        // separate waiter task is needed.
        _connection->_post_retry.exchange(true, butil::memory_order_acq_rel);
        _locked = _connection->_post_mutex.try_lock();
    }
    if (_locked) {
        // This worker can now progress, so cancel the stale unlock request.
        // _progress_mutex serializes worker attempts and the mutex already
        // synchronizes connection state. Relaxed is enough for this
        // notification flag. Ordinary blocking holders must not clear it:
        // acquiring the mutex for a send does not discharge worker progress.
        _connection->_post_retry.store(false, butil::memory_order_relaxed);
    }
}

SrqConnection::PostLock::~PostLock() {
    if (_locked) {
        _connection->_post_mutex.unlock();
        // Unlock before atomically taking the notification. Checking first
        // would miss a worker that registers and fails its retry before unlock.
        // Keep the exchange paired with registration. Do not split taking the
        // request into independent load/store operations. Kick only asks the
        // worker to retry. Another sender may acquire the mutex first.
        if (_connection->_post_retry.exchange(false, butil::memory_order_acq_rel)) {
            // The guard may be unwinding an error return from Send. Writing
            // to eventfd must not overwrite the caller-visible errno.
            int saved_errno = errno;
            _connection->_worker->Kick();
            errno = saved_errno;
        }
    }
}

std::shared_ptr<SrqConnection>
SrqConnection::Create(Socket* socket, bthread_tag_t tag, uint32_t sq_size) {
    const int count = FLAGS_rdma_srq_workers;
    if (!ValidateSrqOptions() || count <= 0) {
        errno = EINVAL;
        return {};
    }
    auto worker = GetWorker(tag, butil::fmix32(socket->id()) % count);
    if (!worker) {
        return {};
    }
    ConnectionPtr connection(new SrqConnection(worker, socket, tag));
    if (!worker->Add(connection, sq_size)) {
        return {};
    }
    return connection;
}

ibv_qp* SrqConnection::qp() const { return _qp; }

void SrqConnection::GetLocalInfo(RdmaLocalInfo* info) const {
    PostLock lock(this);
    *info = DefaultRdmaLocalInfo(RdmaQueueMode::SHARED_RQ);
    info->sq_size = _sq_size;
    info->qp_num = _closing.load() ? 0 : _qpn;
}

int SrqConnection::QueryEce(ibv_ece* ece) {
    PostLock lock(this);
    return _qp && !_closing.load() && IbvQueryEce ?
        IbvQueryEce(_qp, ece) : ENOTCONN;
}

bool SrqConnection::worker_retired() const { return _worker->retired(); }

int SrqConnection::Activate(const ParsedHello& peer, bool is_server,
                            butil::optional<ibv_ece>* outgoing_ece) {
    PostLock lock(this);
    if (!_qp || _closing.load()) {
        errno = ENOTCONN;
        return -1;
    }
    _remote_block_size = peer.block_size;
    if (peer.block_size < 1024 || !_fc.Init(peer.fc_window)) {
        errno = EINVAL;
        return -1;
    }
    bool use_ece = false;
    if (InitializeRcQp(_qp, peer, &use_ece) != 0) {
        return -1;
    }
    _activation_stage = ConnectRcQp(_qp, peer, is_server, 13, 7);
    if (_activation_stage != QpActivationStage::RTS) {
        return -1;
    }
    QueryNegotiatedEce(_qp, peer, is_server, use_ece, outgoing_ece);
    return 0;
}

void SrqConnection::ScheduleProgressLocked(bool wake) {
    // Publishers hold either _post_mutex or _progress_mutex. QP destruction
    // holds both, so _qp cannot disappear during publication. The winner of
    // false -> true enqueues. Others merge into the existing responsibility.
    // A requested wake is still needed when a callback is already queued but
    // the worker is asleep waiting for a resource or close condition.
    if (_qp &&
        !_progress_scheduled.exchange(true, butil::memory_order_acq_rel)) {
        EnqueueProgressLocked(wake);
    } else if (wake) {
        _worker->Kick();
    }
}

void SrqConnection::EnqueueProgressLocked(bool wake) {
    // Capture a connection reference on enqueue. Executed or cancelled
    // callbacks remain in the round until release outside the lock. The
    // connection itself stores no callback, avoiding a self-owning shared_ptr
    // cycle.
    auto self = shared_from_this();
    _worker->Schedule([self] { return self->Progress(); }, wake);
}

ProgressRound SrqConnection::Progress() {
    ProgressRound round;
    bool keep = false;
    bool wake = false;
    int error = 0;
    {
        PostLock lock(this, std::try_to_lock);
        if (!lock.owns_lock()) {
            // Retain progress responsibility and requeue without waking
            // ourselves. PostLock has registered an unlock notification, so
            // progress can resume without a new CQE. A later callback may still
            // find the mutex busy and repeat this path. _progress_scheduled
            // remains set throughout.
            EnqueueProgressLocked(false);
            return round;
        }

        if (!_qp) {
            _progress_scheduled.store(false, butil::memory_order_release);
            return round;
        }

        if (_closing.load(butil::memory_order_acquire)) {
            if (ReadyToDestroyLocked()) {
                // Submission has stopped, so readiness remains true. The worker
                // performs a fresh RX drain and destroys the QP. Keep progress
                // responsibility registered until destruction succeeds.
                round.retiring.push_back(shared_from_this());
                return round;
            }
            keep = true;
        } else {
            error = SendPureGrantLocked();
            // TODO: Add fair capacity allocation for shared TX CQ waiters. A
            // wakeup only reports observed availability. It reserves nothing.
            // Resumed connections still compete in ReserveTx(), and losers
            // register again. Strict fairness is not guaranteed.
            if (_waiting_cq && _worker->DataTxAvailable()) {
                _waiting_cq = false;
                wake = true;
            }
            keep = error || _fc.hard_request_pending() || _waiting_cq;
        }
        // Receive-side publishers hold _progress_mutex and submit-side
        // publishers hold _post_mutex. Holding both here prevents a new
        // progress request between checking pending work and clearing its
        // registration.
        if (keep) {
            // Keep progress responsibility by directly requeueing, bypassing
            // first-registration deduplication. Do not Kick while waiting for
            // capacity or close conditions. CQ/async events trigger the next
            // check.
            EnqueueProgressLocked(false);
        } else {
            _progress_scheduled.store(false, butil::memory_order_release);
        }
    }
    if (error) {
        RequestClose(error);
    }
    if (wake || error) {
        round.writers.insert(shared_from_this());
    }
    return round;
}

void SrqConnection::WakeWriter() {
    SocketUniquePtr socket;
    if (Socket::Address(_sid, &socket) == 0) {
        socket->WakeAsEpollOut();
    }
}

bool SrqConnection::IsWritable() {
    PostLock lock(this);
    if (_closing.load(butil::memory_order_acquire)) {
        return true;
    }
    // A grant may be published but not yet applied. Restore the send window
    // before testing writability.
    _fc.ApplyReceived();
    // Use the same CQ snapshot for writability and wait registration. If
    // another QP returns capacity between two separate reads, this could
    // otherwise report unwritable without registering a later wakeup.
    bool cq_available = _worker->DataTxAvailable();
    bool writable = _activation_stage == QpActivationStage::RTS && _fc.can_send() &&
                    DataSqAvailableLocked() != 0 && cq_available;
    _waiting_cq = !writable && !cq_available;
    // SQ completions and grants identify this connection directly. Shared-CQ
    // capacity waits need worker progress registration.
    if (_waiting_cq) {
        ScheduleProgressLocked(true);
    }
    return writable;
}

bool SrqConnection::CompleteSend(const ibv_wc& wc, butil::IOBuf& released) {
    if (wc.wr_id == 0 || wc.wr_id > _sq_size) {
        // WR IDs encode slot index plus one. Validate before indexing records.
        // An inconsistent ID requires stopping the worker before recovering CQ
        // accounting.
        _worker->MarkBroken();
        return false;
    }

    uint32_t slot = wc.wr_id - 1;
    auto* record = &_records[slot];
    uint64_t completed = _sq_completed.load(butil::memory_order_relaxed);
    // SEND completions for an RC QP arrive in submission order. Acquiring
    // active pairs with publication before post_send, making the complete
    // record visible even when post_send has not returned.
    if (slot != completed % _sq_size ||
        !record->active.load(butil::memory_order_acquire)) {
        _worker->MarkBroken();
        return false;
    }

    if (record->control) {
        CHECK_GT(_control_outstanding, 0u);
        --_control_outstanding;
    }
    ReleasePayload(*record, released);
    record->active.store(false, butil::memory_order_relaxed);
    // Return the SQ slot last. After this release publication, the submitter
    // may immediately overwrite payload and every other record field.
    _sq_completed.store(completed + 1, butil::memory_order_release);
    if (wc.status != IBV_WC_SUCCESS &&
        !_closing.load(butil::memory_order_acquire)) {
        RequestClose(ERDMA);
    }
    // Do not inspect submit-side wait flags: a writer may register just after
    // capacity is returned. The worker coalesces notifications per connection
    // and wakes writers outside _progress_mutex.
    return true;
}

bool SrqConnection::ReceiveControl(uint32_t flags, size_t bytes) {
    if (!_fc.Receive(flags, bytes)) {
        return false;
    }
    // The caller holds _progress_mutex, serializing this registration with
    // progress completion. Schedule based only on the newly received hard
    // event, without reading submit-owned pending state. PURE_GRANT carries no
    // request.
    if (flags != SrqFlowControl::PURE_GRANT && (flags & SrqFlowControl::HARD_REQUEST) != 0) {
        ScheduleProgressLocked(false);
    }
    return true;
}

bool SrqConnection::EnqueueReceived(butil::IOBuf received) {
    if (_closing.load(butil::memory_order_acquire)) {
        return false;
    }
    _incoming.Emplace(received.movable());
    return true;
}

uint32_t SrqConnection::OutstandingLocked() const {
    return _sq_head - _sq_completed.load(butil::memory_order_acquire);
}

uint32_t SrqConnection::DataSqLimit() const {
    return _sq_size - kControlSlots;
}

uint32_t SrqConnection::DataSqAvailableLocked() const {
    uint32_t limit = DataSqLimit();
    uint32_t outstanding = OutstandingLocked();
    return outstanding < limit ? limit - outstanding : 0;
}

bool SrqConnection::ReadyToDestroyLocked() const {
    // _closing was set under _post_mutex, so no new WR can extend the SQ.
    // LAST_WQE is acknowledged before _last_wqe is published, but says neither
    // that TX records are reclaimed nor that RX CQEs have been consumed. Wait
    // for TX here. RetireConnections must then poll RX to empty after observing
    // this readiness. A QP that never reached RTR needs no LAST_WQE event.
    return (_activation_stage == QpActivationStage::NONE ||
            _last_wqe.load(butil::memory_order_acquire)) &&
            OutstandingLocked() == 0;
}

bool SrqConnection::DestroyQpLocked() {
    if (_qp) {
        // Unregister waits for callback capture, not callback execution. Async
        // callbacks retain their context and only publish state or queue work,
        // so late callbacks need not touch this QP or acquire _post_mutex.
        UnregisterRdmaAsyncEventContext(_qp);
        if (IbvDestroyQp(_qp) != 0) {
            return false;
        }
        _qp = nullptr;
    }
    _progress_scheduled.store(false, butil::memory_order_release);
    return true;
}

int SrqConnection::SendPureGrantLocked() {
    _fc.ApplyReceived();

    // Reply promptly to hard requests and piggyback soft-request grants on
    // data. A grant does not mean the application released physical
    // receive blocks and is not delayed by pool watermarks. Check only the
    // QP/SQ/CQ resources required to submit. Retain pending requests when those
    // resources are unavailable.
    if (_closing.load(butil::memory_order_acquire) ||
        _activation_stage != QpActivationStage::RTS || !_fc.NeedPureGrant() ||
        _control_outstanding >= kControlSlots || OutstandingLocked() == _sq_size ||
        !_worker->ReserveTx(1, true)) {
        return 0;
    }

    uint32_t slot = _sq_head % _sq_size;
    auto* record = &_records[slot];
    ibv_send_wr wr = {};
    wr.wr_id = slot + 1;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.imm_data = htonl(SrqFlowControl::PURE_GRANT);
    wr.send_flags = IBV_SEND_SIGNALED;
    record->control = true;
    record->has_user_data = false;
    ++_sq_head;
    ++_control_outstanding;
    record->active.store(true, butil::memory_order_release);
    ibv_send_wr* bad = nullptr;
    int rc = IbvPostSrqSend(_qp, &wr, &bad);
    if (rc != 0 && bad == &wr) {
        // This WR was definitely not posted and cannot produce a CQE. Roll back
        // the ring tail and its reservation.
        record->active.store(false, butil::memory_order_relaxed);
        --_sq_head;
        --_control_outstanding;
        _worker->ReleaseTx(1);
    } else {
        if (rc == 0) {
            _fc.PureGrantPosted();
        } else {
            // The post outcome is unknown. Leave the record and its CQ
            // reservation for completion or worker-wide DMA shutdown.
            _worker->MarkBroken();
        }
    }

    // Account for this post before consuming new requests, so clearing the
    // posted grant cannot erase an event it did not cover.
    _fc.ApplyReceived();
    return rc == 0 ? 0 : ERDMA;
}

void SrqConnection::ReleasePayload(TxRecord& record, butil::IOBuf& released) {
    if (record.has_user_data) {
        // Move payload references into the owner round before making the SQ
        // slot reusable. The round hands them to a separate task after
        // unlocking. User deleters must not run during CQ progress.
        released.append(record.payload.movable());
    } else {
        record.payload.clear();
    }
}

void SrqConnection::RetireSendsLocked(butil::IOBuf& released) {
    // Worker-wide destruction has stopped all DMA under _progress_mutex and
    // holds _post_mutex, excluding both completion and submission. Active
    // records are exactly those not reclaimed by CQ handling or post rollback.
    // Return their reservations once even if destruction discarded their CQEs.
    uint32_t count = 0;
    for (uint32_t i = 0; i < _sq_size; ++i) {
        if (_records[i].active.load(butil::memory_order_acquire)) {
            ReleasePayload(_records[i], released);
            _records[i].active.store(false, butil::memory_order_relaxed);
            ++count;
        }
    }
    _worker->ReleaseTx(count);
    _sq_completed.store(_sq_head, butil::memory_order_release);
    _control_outstanding = 0;
    _progress_scheduled.store(false, butil::memory_order_release);
}

struct SrqConnection::SendInput {
    SendInput(butil::IOBuf** buffers, size_t num_buffers)
        : from(buffers), count(num_buffers) {
        for (size_t i = 0; i < count; ++i) {
            remaining_bytes += from[i]->size();
        }
    }

    // Caller-owned buffers, consumed in order across all verbs batches.
    butil::IOBuf** from;
    size_t count;
    // First buffer that may still contain bytes to prepare.
    size_t index{0};
    // Bytes not yet assigned to a WR, used to estimate the next reservation.
    size_t remaining_bytes{0};
};

struct SrqConnection::SendBatch {
    // WR chain and per-WR SGEs for one post_send call. These arrays own no
    // payload references.
    ibv_send_wr wr[kBatch];
    ibv_sge sges[kBatch][32];
    // Records used for pre-post publication and rollback of the unposted
    // suffix. Do not access accepted-prefix records after posting.
    TxRecord* records[kBatch];
    // CQEs may precede post_send return. Account for accepted WRs using only
    // these local flags and lengths.
    uint32_t flags[kBatch];
    size_t lengths[kBatch];
    // Shared CQ credits reserved for this batch, including unused capacity.
    uint32_t reserved{0};
    // Fully prepared WRs, each already counted in _sq_head.
    uint32_t prepared{0};
};

struct SrqConnection::SendPostResult {
    // Preparation or verbs error. A preparation error leaves posted at zero.
    int error{0};
    // Prefix owned by completion or shutdown. Includes all prepared WRs when
    // bad_wr is unrecognized, since no suffix is safe to roll back.
    uint32_t posted{0};
    // Unknown post outcome. Retain records without committing FC state.
    bool indeterminate{false};
};

int SrqConnection::PrepareSendBatchLocked(SendInput& input, SendBatch& batch,
                                        butil::IOBuf& unposted) {
    // Only plan on the snapshot. Preparation must not consume real FC credits
    // or drain received events before the accepted prefix has been accounted.
    auto planned = _fc.Snapshot();
    // Estimate demand from bytes so small messages reserve only what they
    // need. SGE fragmentation may require more WRs in a subsequent batch.
    uint32_t requested = std::min<size_t>(kBatch,
        1 + (input.remaining_bytes - 1) / _remote_block_size);
    requested = std::min(requested, planned.window());
    requested = std::min(requested, DataSqAvailableLocked());
    batch.reserved = requested != 0 ? _worker->ReserveTx(requested, false) : 0;
    batch.prepared = 0;
    while (input.index < input.count && batch.prepared < batch.reserved) {
        if (input.from[input.index]->empty()) {
            ++input.index;
            continue;
        }

        // The reservation respects both connection SQ and shared CQ control
        // capacity. Cut payload references into the record without copying
        // large payloads, and retain them until completion or rollback.
        uint32_t slot = _sq_head % _sq_size;
        auto* record = &_records[slot];
        uint32_t prepared = batch.prepared;
        size_t nsg = 0;
        size_t length = 0;
        bool in_pool = true;
        bool has_user_data = false;
        while (input.index < input.count && nsg < _max_sge && length < _remote_block_size) {
            ssize_t n = CutRdmaSges(input.from[input.index], batch.sges[prepared], &nsg,
                                    &record->payload, _max_sge, _remote_block_size - length,
                                    &in_pool, &has_user_data);
            if (n < 0) {
                int error = errno;
                // This incomplete record is not yet counted in _sq_head.
                // Keep its payload alive until Send releases _post_mutex,
                // just like the staged WRs that will be rolled back.
                unposted.append(record->payload);
                record->payload.clear();
                return error;
            }
            length += n;
            if (input.from[input.index]->empty()) {
                ++input.index;
            }
        }

        batch.wr[prepared] = {};
        batch.wr[prepared].wr_id = slot + 1;
        batch.wr[prepared].opcode = IBV_WR_SEND_WITH_IMM;
        batch.flags[prepared] = planned.PrepareData();
        batch.wr[prepared].imm_data = htonl(batch.flags[prepared]);
        batch.wr[prepared].sg_list = batch.sges[prepared];
        batch.wr[prepared].num_sge = nsg;
        batch.wr[prepared].send_flags = IBV_SEND_SIGNALED;
        if (ShouldPostInline(in_pool, length, _inline_size)) {
            batch.wr[prepared].send_flags |= IBV_SEND_INLINE;
        }
        if (prepared != 0) {
            batch.wr[prepared - 1].next = &batch.wr[prepared];
        }
        batch.records[prepared] = record;
        batch.lengths[prepared] = length;
        record->control = false;
        record->has_user_data = has_user_data;
        ++_sq_head;
        input.remaining_bytes -= length;
        planned.DataPosted(batch.flags[prepared]);
        ++batch.prepared;
    }
    return 0;
}

SrqConnection::SendPostResult SrqConnection::PostSendBatchLocked(SendBatch& batch) {
    SendPostResult result;
    if (batch.prepared == 0) {
        return result;
    }

    // Publish complete records before posting. Completion may reclaim the
    // accepted prefix before verbs returns. Later accounting must use only
    // batch flags and lengths, never those records.
    for (uint32_t i = 0; i < batch.prepared; ++i) {
        batch.records[i]->active.store(true, butil::memory_order_release);
    }
    ibv_send_wr* bad = nullptr;
    result.error = IbvPostSrqSend(_qp, batch.wr, &bad);
    result.posted = batch.prepared;
    if (result.error != 0) {
        result.posted = 0;
        while (result.posted < batch.prepared &&
               bad != &batch.wr[result.posted]) {
            ++result.posted;
        }
        if (result.posted == batch.prepared) {
            // With an unrecognized bad_wr, no suffix is known to be unposted.
            // Leave all prepared records and their reservations to completion
            // or worker shutdown. Only unused capacity can be returned now.
            result.indeterminate = true;
            _worker->Fail();
        }
    }
    return result;
}

ssize_t SrqConnection::FinishSendBatchLocked(SendBatch& batch,
                                           const SendPostResult& result,
                                           butil::IOBuf& unposted) {
    ssize_t bytes = 0;
    // Accepted-prefix records may already be reclaimed. Account using only
    // batch metadata, and touch records only in the known unposted suffix.
    for (uint32_t i = 0; i < result.posted; ++i) {
        if (!result.indeterminate) {
            _fc.DataPosted(batch.flags[i]);
        }
        bytes += batch.lengths[i];
    }
    for (uint32_t i = result.posted; i < batch.prepared; ++i) {
        auto* record = batch.records[i];
        record->active.store(false, butil::memory_order_relaxed);
        unposted.append(record->payload);
        record->payload.clear();
    }

    // Completion or shutdown owns the accepted or indeterminate prefix.
    // Return every other reservation here. These sets stay disjoint even when
    // CQEs arrive before post_send returns.
    _worker->ReleaseTx(batch.reserved - result.posted);
    _sq_head -= batch.prepared - result.posted;
    // RX only publishes events during preparation and posting. Apply them
    // after the entire prefix is accounted, including on preparation failure
    // and partial submission. The next batch sees the restored window and new
    // requests without clearing events that the old batch did not cover.
    _fc.ApplyReceived();
    return bytes;
}

ssize_t SrqConnection::Send(butil::IOBuf** from, size_t count) {
    // Keep unposted payloads outside the lock scope so every return unlocks
    // before releasing user references.
    butil::IOBuf unposted;
    ssize_t bytes = 0;
    int error = 0;
    {
        PostLock lock(this);
        if (_closing.load(butil::memory_order_acquire) ||
            _activation_stage != QpActivationStage::RTS) {
            errno = ENOTCONN;
            return -1;
        }

        _fc.ApplyReceived();
        SendInput input(from, count);
        bool blocked = false;
        // Borrow temporary arrays per active Send call. Preserve support for 32
        // SGEs without charging small bthread stacks or idle connections for
        // these arrays.
        auto return_batch = [](SendBatch* batch) { butil::return_object(batch); };
        std::unique_ptr<SendBatch, decltype(return_batch)> batch(
            butil::get_object<SendBatch>(), return_batch);
        if (batch == nullptr) {
            errno = ENOMEM;
            return -1;
        }

        while (input.index < input.count && input.remaining_bytes != 0 && error == 0) {
            SendPostResult result;
            result.error = PrepareSendBatchLocked(input, *batch, unposted);
            if (batch->reserved == 0) {
                blocked = true;
                break;
            }
            if (result.error == 0) {
                result = PostSendBatchLocked(*batch);
            }
            // Always settle the reservation, even if preparation failed before
            // posting. In that case the entire prepared batch is unposted.
            bytes += FinishSendBatchLocked(*batch, result, unposted);
            error = result.error;
        }
        _waiting_cq = blocked && error == 0 && !_worker->DataTxAvailable();
        if (_waiting_cq) {
            ScheduleProgressLocked(true);
        }
    }
    // Report errors and release references only after unlocking. A
    // successful short write lets Socket continue with the remaining input.
    if (error != 0) {
        Close(error);
        errno = error;
        return -1;
    }
    if (bytes == 0) {
        errno = EAGAIN;
        return -1;
    }
    // Resource exhaustion may still return a successful prefix. Socket sends
    // the remaining IOBufs later.
    return bytes;
}

void SrqConnection::Close(int error) {
    RequestClose(error);
    WakeWriter();
}

void SrqConnection::RequestClose(int error) {
    // Normal close is a request, not synchronous QP destruction:
    // 1. Under _post_mutex, publish _closing and move a used QP to ERR.
    // 2. Keep a progress callback pending until TX completion/flush and
    //    acknowledged LAST_WQE barriers are satisfied. Do not wait here.
    // 3. Progress observes both barriers. RetireConnections drains RX again,
    //    destroys the QP and removes its QPN entry under _progress_mutex.
    // 4. Release the round's retained references outside transport locks.
    // Socket notification does not wait for QP destruction: public Close wakes
    // the writer after RequestClose returns, and errors are reported separately
    // through the deferred notification queue, also outside transport locks.
    // A QP that never reached RTR skips the LAST_WQE wait. Worker failure uses
    // DestroyAll instead, since broken completion queues cannot prove drainage.
    {
        PostLock lock(this);
        // Writes are serialized by _post_mutex. Atomic access also allows
        // completion, receive and delivery paths to observe close without
        // taking this lock.
        if (!_closing.load(butil::memory_order_relaxed)) {
            _closing.store(true, butil::memory_order_release);
            // Holding _post_mutex ensures in-flight submissions have returned
            // and prevents new data or grants after this transition. Move a
            // used QP to ERR at close entry. The event thread never
            // waits for this lock. A QP that never reached RTR has no in-flight
            // I/O and can be destroyed directly. RESET -> ERR is not a valid
            // transition and must not turn an incomplete handshake into a
            // worker-wide failure.
            if (_qp && _activation_stage != QpActivationStage::NONE) {
                ibv_qp_attr attr = {};
                attr.qp_state = IBV_QPS_ERR;
                if (IbvModifyQp(_qp, &attr, IBV_QP_STATE) != 0) {
                    // RequestClose also runs outside worker progress. Keep the
                    // wakeup so an external close cannot strand a broken worker.
                    _worker->Fail();
                }
            }
            ScheduleProgressLocked(true);
        }
        if (error && !_failure_reported) {
            _failure_reported = true;
            _worker->NotifyFailure(_sid, error);
        }
    }
    // Close never dequeues the MPSC queue. An active delivery task discards
    // remaining input. If delivery never started, connection destruction
    // releases the nodes after both producer and consumer have exited.
}

std::function<void()> SrqConnection::PrepareAsyncEvent(ibv_event_type type) {
    auto self = shared_from_this();
    return [self, type] {
        if (type == IBV_EVENT_QP_LAST_WQE_REACHED) {
            // DispatchRdmaAsyncEvent has already acknowledged this event.
            // Publish before Kick so the owner can observe the close barrier
            // without waiting on this event-reader thread or _post_mutex.
            self->_last_wqe.store(true, butil::memory_order_release);
            self->_worker->Kick();
        } else if (type == IBV_EVENT_QP_FATAL || type == IBV_EVENT_QP_REQ_ERR ||
                   type == IBV_EVENT_QP_ACCESS_ERR) {
            // The async event thread must not wait for the QP lock: its holder
            // may be destroying a verbs object and waiting for this thread to
            // acknowledge another event. Queue the failure for worker progress
            // instead.
            self->_worker->FailConnection(self);
        }
    };
}

int SrqConnection::EnableReceive() {
    _delivery_enabled.store(true, butil::memory_order_release);
    // Wake input queued during the handshake even if no new CQE arrives.
    StartDelivery();
    return 0;
}

bool SrqConnection::StartDelivery(bool urgent) {
    // Publish input before incrementing the notification count. If a consumer
    // exists, its exit CAS observes the new notification and it continues. If
    // it already exited, this call acquires consumer ownership.
    if (_delivery_events.fetch_add(1, butil::memory_order_acq_rel) != 0) {
        return false;
    }

    // The old consumer may have drained input between enqueue and notification.
    // After acquiring ownership, dequeue the first block before allocating a
    // task, avoiding an empty bthread for a late notification. If delivery is
    // not enabled yet, leave input queued for EnableReceive to notify again.
    uint64_t events = 1;
    butil::IOBuf data;
    while (true) {
        if ((_delivery_enabled.load(butil::memory_order_acquire) ||
             _closing.load(butil::memory_order_acquire)) && _incoming.Dequeue(data)) {
            break;
        }
        // Do not store zero directly: a notification racing with the
        // empty/disabled check must make CAS fail and force a recheck.
        if (_delivery_events.compare_exchange_strong(
                events, 0, butil::memory_order_acq_rel, butil::memory_order_acquire)) {
            return false;
        }
    }

    SocketUniquePtr socket;
    DeliveryTask* task = nullptr;
    int error = 0;
    bool started = false;
    // The first block is dequeued but this call still owns consumption. On any
    // task-start failure, close, discard input and release consumer ownership
    // through one cleanup path. All references are released outside transport
    // locks.
    BUTIL_SCOPE_EXIT {
        if (!started) {
            if (error || !_closing.load(butil::memory_order_acquire)) {
                Close(error);
            }
            if (task) {
                data.append(task->data.movable());
            }
            Consume(nullptr, data.movable());
            if (task) {
                task->socket.reset();
                task->connection.reset();
                butil::return_object(task);
            }
            if (error) {
                _worker->Fail();
            }
        }
    };

    if (_closing.load(butil::memory_order_acquire)) {
        return false;
    }
    // Even a failed Address may release the final Socket reference and
    // synchronously enter RdmaEndpoint::Reset -> Close. Acquire the Socket
    // reference without holding the connection lock.
    if (Socket::Address(_sid, &socket) != 0) {
        return false;
    }
    task = butil::get_object<DeliveryTask>();
    if (!task) {
        error = ENOMEM;
        return false;
    }

    task->connection = shared_from_this();
    task->socket = std::move(socket);
    task->data.swap(data);
    bthread_attr_t attr = FLAGS_rdma_disable_bthread ? BTHREAD_ATTR_PTHREAD :
                                                    BTHREAD_ATTR_NORMAL;
    attr.tag = _tag;
    attr.keytable_pool = task->socket->keytable_pool();
    bthread_t tid;
    error = urgent && !EventDispatcherUnsched() ?
        bthread_start_urgent(&tid, &attr, RunDelivery, task) :
        bthread_start_background(&tid, &attr, RunDelivery, task);
    // Success transfers the task and consumer ownership. An urgent task may
    // already have finished, so do not access task again.
    started = (error == 0);
    return started;
}

void* SrqConnection::RunDelivery(void* arg) {
    auto* task = static_cast<DeliveryTask*>(arg);
    task->connection->Consume(task->socket.get(), task->data.movable());
    task->socket.reset();
    task->connection.reset();
    butil::return_object(task);
    return nullptr;
}

void SrqConnection::Consume(Socket* socket, butil::IOBuf data) {
    // The last message callback runs on this bthread, but consumer ownership
    // must be released first so later input can be dequeued and parsed. Keep
    // the closure across drain passes. Its destructor runs only after the exit
    // CAS succeeds.
    InputMessageClosure last_msg;
    uint64_t events = _delivery_events.load(butil::memory_order_acquire);
    do {
        // Leave input queued until handshake parsing finishes. EnableReceive
        // notifies again. Only this consumer may dequeue after close, and
        // closed input must not reach the protocol parser.
        if (_delivery_enabled.load(butil::memory_order_acquire) ||
            _closing.load(butil::memory_order_acquire)) {
            butil::IOBuf received;
            while (_incoming.Dequeue(received)) {
                data.append(received);
                received.clear();
            }
            if (!data.empty() && !_closing.load(butil::memory_order_acquire) &&
                socket) {
                auto* transport = static_cast<RdmaTransport*>(socket->_transport.get());
                auto* ep = transport->GetRdmaEp();
                if (ep->Deliver(this, &data, data.size(), last_msg) < 0) {
                    Close(EPROTO);
                }
            }
            // The prefetched block and this pass of queued input are handled.
            // The next pass appends only new input.
            data.clear();
        }
        // All queue/parser access for this pass is complete. After releasing
        // ownership, only the last_msg callback remains. Failed CAS updates
        // events. Retry to observe newly published input or the enable
        // transition.
    } while (!_delivery_events.compare_exchange_strong(events, 0,
                                                       butil::memory_order_acq_rel,
                                                       butil::memory_order_acquire));
}

void SrqConnection::Debug(std::ostream& out, butil::StringPiece connector) const {
    {
        PostLock lock(this);
        out << "rdma_srq=ON" << connector << "rdma_srq_qpn=" << _qpn
            << connector << "rdma_srq_closing=" << _closing.load()
            << connector << "rdma_srq_window=" << _fc.window()
            << connector << "rdma_srq_sq_outstanding=" << OutstandingLocked();
    }
    // Release the connection lock before querying the worker to preserve the
    // _progress_mutex -> _post_mutex lock order.
    _worker->Debug(out, connector);
}

void StopAllSrqWorkers() {
    std::vector<std::shared_ptr<SrqWorker>> workers;
    auto& registry = Registry();
    {
        std::lock_guard<bthread::Mutex> lock(registry.mutex);
        // Retry workers whose previous destruction failed. Stop quarantines
        // them again if the provider still does not confirm destruction.
        workers.swap(registry.quarantine);
        for (const auto& entry : registry.workers) {
            workers.push_back(entry.second);
        }
        registry.workers.clear();
    }
    for (const auto& worker : workers) {
        worker->Stop();
    }
}

size_t SrqLiveConnections() { return g_live_connections.load(); }

size_t SrqWorkerCount() { return g_live_workers.load(); }

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
