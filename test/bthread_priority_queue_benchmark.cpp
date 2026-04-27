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

// Microbenchmark for B2 priority queue operations:
//   1. inbound (MPSCQueue) enqueue+dequeue cost
//   2. inbound multi-producer contention
//   3. flush inbound -> wsq cost (owner side)
//   4. wsq push+pop cost (owner side)
//   5. wsq steal cost (non-owner side)
//   6. full pipeline: producer -> inbound -> flush -> pop

#include <cstdio>
#include <atomic>
#include <vector>
#include <pthread.h>
#include "butil/time.h"
#include "bthread/bthread.h"
#include "bthread/work_stealing_queue.h"
#include "bthread/remote_task_queue.h"
#include "butil/containers/mpsc_queue.h"
#include "bthread/processor.h"

using bthread::WorkStealingQueue;
using bthread::RemoteTaskQueue;

static const size_t WSQ_CAP = 1024;
static const size_t INBOUND_CAP = 4096;

// ============================================================
// Benchmark 1: wsq push+pop (owner, single thread)
// ============================================================
void bench_wsq_pop() {
    WorkStealingQueue<bthread_t> wsq;
    wsq.init(WSQ_CAP);

    const int N = 500000;
    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        wsq.push((bthread_t)i);
        bthread_t tid;
        wsq.pop(&tid);
    }
    tm.stop();
    printf("  wsq push+pop (owner):          %6.1f ns/op  (%d ops)\n",
           tm.n_elapsed() / (double)N, N);
}

// ============================================================
// Benchmark 5: wsq steal (non-owner threads + owner push)
// ============================================================
struct StealBenchArg {
    WorkStealingQueue<bthread_t>* wsq;
    std::atomic<bool>* stop;
    int stolen;
};

void* steal_bench_thread(void* arg) {
    StealBenchArg* a = static_cast<StealBenchArg*>(arg);
    a->stolen = 0;
    while (!a->stop->load(std::memory_order_relaxed)) {
        bthread_t tid;
        if (a->wsq->steal(&tid)) {
            ++a->stolen;
        } else {
            cpu_relax();
        }
    }
    return NULL;
}

void bench_wsq_steal(int nstealer) {
    WorkStealingQueue<bthread_t> wsq;
    wsq.init(WSQ_CAP);

    const int N = 200000;
    std::atomic<bool> stop(false);

    std::vector<pthread_t> threads(nstealer);
    std::vector<StealBenchArg> args(nstealer);
    for (int i = 0; i < nstealer; ++i) {
        args[i] = {&wsq, &stop, 0};
        pthread_create(&threads[i], NULL, steal_bench_thread, &args[i]);
    }

    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        while (!wsq.push((bthread_t)i)) {
            cpu_relax();
        }
    }
    stop.store(true, std::memory_order_release);
    tm.stop();

    int total_stolen = 0;
    for (int i = 0; i < nstealer; ++i) {
        pthread_join(threads[i], NULL);
        total_stolen += args[i].stolen;
    }
    bthread_t tid;
    int remaining = 0;
    while (wsq.pop(&tid)) ++remaining;

    printf("  wsq steal (%d stealers):        %6.1f ns/push  (pushed=%d stolen=%d remain=%d)\n",
           nstealer, tm.n_elapsed() / (double)N, N, total_stolen, remaining);
}

// ============================================================
// B2 Inbound (MPSCQueue, lock-free)
// ============================================================

// MPSC single-thread enqueue+dequeue
void bench_mpsc_single() {
    butil::MPSCQueue<bthread_t> q;
    const int N = 200000;
    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        q.Enqueue((bthread_t)i);
        bthread_t out;
        q.Dequeue(out);
    }
    tm.stop();
    printf("  mpsc enqueue+dequeue (single): %6.1f ns/op  (%d ops)\n",
           tm.n_elapsed() / (double)N, N);
}

// MPSC multi-producer context
struct MpscBenchCtx {
    butil::MPSCQueue<bthread_t>* q;
    std::atomic<int> produced{0};
    std::atomic<bool> go{false};
    int total;
    int per_producer;
};

void* mpsc_producer(void* arg) {
    MpscBenchCtx* ctx = static_cast<MpscBenchCtx*>(arg);
    while (!ctx->go.load(std::memory_order_acquire)) {
        cpu_relax();
    }
    for (int i = 0; i < ctx->per_producer; ++i) {
        ctx->q->Enqueue((bthread_t)i);
        ctx->produced.fetch_add(1, std::memory_order_relaxed);
    }
    return NULL;
}

void bench_mpsc_multi(int nproducer) {
    butil::MPSCQueue<bthread_t> q;
    const int PER_PRODUCER = 50000;
    MpscBenchCtx ctx;
    ctx.q = &q;
    ctx.total = nproducer * PER_PRODUCER;
    ctx.per_producer = PER_PRODUCER;

    std::vector<pthread_t> threads(nproducer);
    for (int i = 0; i < nproducer; ++i) {
        pthread_create(&threads[i], NULL, mpsc_producer, &ctx);
    }

    butil::Timer tm;
    tm.start();
    ctx.go.store(true, std::memory_order_release);

    int drained = 0;
    while (drained < ctx.total) {
        bthread_t tid;
        if (q.Dequeue(tid)) {
            ++drained;
        } else {
            sched_yield();
        }
    }

    for (int i = 0; i < nproducer; ++i) {
        pthread_join(threads[i], NULL);
    }
    tm.stop();

    printf("  mpsc push (%d producers):      %6.1f ns/op  (%d ops)\n",
           nproducer, tm.n_elapsed() / (double)ctx.total, ctx.total);
}

// MPSC flush -> wsq (same pattern as B2 but with MPSCQueue)
void bench_mpsc_flush(int batch_size) {
    butil::MPSCQueue<bthread_t> q;
    WorkStealingQueue<bthread_t> wsq;
    wsq.init(WSQ_CAP);

    const int ROUNDS = 20000;
    butil::Timer tm;
    tm.start();
    for (int r = 0; r < ROUNDS; ++r) {
        for (int i = 0; i < batch_size; ++i) {
            q.Enqueue((bthread_t)i);
        }
        bthread_t tid;
        for (int i = 0; i < batch_size; ++i) {
            if (!q.Dequeue(tid)) break;
            wsq.push(tid);
        }
        while (wsq.pop(&tid)) {}
    }
    tm.stop();
    int total_ops = ROUNDS * batch_size;
    printf("  mpsc flush->wsq (batch=%2d):    %6.1f ns/op  (%d ops)\n",
           batch_size, tm.n_elapsed() / (double)total_ops, total_ops);
}

// MPSC full pipeline (same as bench_pipeline but with MPSCQueue)
void* mpsc_pipeline_producer(void* arg) {
    MpscBenchCtx* ctx = static_cast<MpscBenchCtx*>(arg);
    while (!ctx->go.load(std::memory_order_acquire)) {
        cpu_relax();
    }
    for (int i = 0; i < ctx->per_producer; ++i) {
        ctx->q->Enqueue((bthread_t)i);
    }
    return NULL;
}

void bench_mpsc_pipeline(int nproducer) {
    butil::MPSCQueue<bthread_t> q;
    WorkStealingQueue<bthread_t> wsq;
    wsq.init(WSQ_CAP);

    const int PER_PRODUCER = 50000;
    const int TOTAL = nproducer * PER_PRODUCER;
    const int FLUSH_BATCH = 8;

    MpscBenchCtx ctx;
    ctx.q = &q;
    ctx.total = TOTAL;
    ctx.per_producer = PER_PRODUCER;

    std::vector<pthread_t> producers(nproducer);
    for (int i = 0; i < nproducer; ++i) {
        pthread_create(&producers[i], NULL, mpsc_pipeline_producer, &ctx);
    }

    butil::Timer tm;
    tm.start();
    ctx.go.store(true, std::memory_order_release);

    int consumed = 0;
    while (consumed < TOTAL) {
        bthread_t tid;
        for (int i = 0; i < FLUSH_BATCH; ++i) {
            if (!q.Dequeue(tid)) break;
            if (!wsq.push(tid)) {
                ++consumed;
                break;
            }
        }
        while (wsq.pop(&tid)) {
            ++consumed;
        }
        if (consumed < TOTAL) {
            cpu_relax();
        }
    }

    for (int i = 0; i < nproducer; ++i) {
        pthread_join(producers[i], NULL);
    }
    tm.stop();

    printf("  mpsc pipeline (%d prod -> flush%d -> pop): %6.1f ns/task  (%d tasks)\n",
           nproducer, FLUSH_BATCH, tm.n_elapsed() / (double)TOTAL, TOTAL);
}

// ============================================================
// Baseline: normal bthread scheduling path primitives
// ============================================================

// Baseline A: _rq push+pop (local worker path: WSQ, same as owner wsq)
// This is identical to bench_wsq_pop — included for clarity in comparison.
void bench_baseline_rq() {
    WorkStealingQueue<bthread_t> rq;
    rq.init(WSQ_CAP);

    const int N = 500000;
    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        rq.push((bthread_t)i);
        bthread_t tid;
        rq.pop(&tid);
    }
    tm.stop();
    printf("  _rq push+pop (local worker):   %6.1f ns/op  (%d ops)\n",
           tm.n_elapsed() / (double)N, N);
}

// Baseline B: _remote_rq push+pop (non-worker path: mutex + BoundedQueue)
struct InboundBenchCtx {
    RemoteTaskQueue* inbound;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> go{false};
    int total;
    int per_producer;
};

void* inbound_producer(void* arg) {
    InboundBenchCtx* ctx = static_cast<InboundBenchCtx*>(arg);
    while (!ctx->go.load(std::memory_order_acquire)) {
        cpu_relax();
    }
    for (int i = 0; i < ctx->per_producer; ++i) {
        while (!ctx->inbound->push((bthread_t)i)) {
            sched_yield();
        }
        ctx->produced.fetch_add(1, std::memory_order_relaxed);
    }
    return NULL;
}
void bench_baseline_remote_rq_single() {
    RemoteTaskQueue remote_rq;
    remote_rq.init(INBOUND_CAP);

    const int N = 200000;
    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        remote_rq.push((bthread_t)i);
        bthread_t out;
        remote_rq.pop(&out);
    }
    tm.stop();
    printf("  _remote_rq push+pop (single):  %6.1f ns/op  (%d ops)\n",
           tm.n_elapsed() / (double)N, N);
}

// Baseline C: _remote_rq multi-producer (same as inbound multi-producer)
void bench_baseline_remote_rq_multi(int nproducer) {
    RemoteTaskQueue remote_rq;
    remote_rq.init(INBOUND_CAP);

    const int PER_PRODUCER = 50000;
    InboundBenchCtx ctx;
    ctx.inbound = &remote_rq;
    ctx.total = nproducer * PER_PRODUCER;
    ctx.per_producer = PER_PRODUCER;

    std::vector<pthread_t> threads(nproducer);
    for (int i = 0; i < nproducer; ++i) {
        pthread_create(&threads[i], NULL, inbound_producer, &ctx);
    }

    butil::Timer tm;
    tm.start();
    ctx.go.store(true, std::memory_order_release);

    int drained = 0;
    while (drained < ctx.total) {
        bthread_t tid;
        if (remote_rq.pop(&tid)) {
            ++drained;
        } else {
            sched_yield();
        }
    }

    for (int i = 0; i < nproducer; ++i) {
        pthread_join(threads[i], NULL);
    }
    tm.stop();

    printf("  _remote_rq push (%d producers): %6.1f ns/op  (%d ops)\n",
           nproducer, tm.n_elapsed() / (double)ctx.total, ctx.total);
}

// Baseline D: _rq steal (non-owner steals from another worker's _rq)
void bench_baseline_rq_steal(int nstealer) {
    WorkStealingQueue<bthread_t> rq;
    rq.init(WSQ_CAP);

    const int N = 200000;
    std::atomic<bool> stop(false);

    std::vector<pthread_t> threads(nstealer);
    std::vector<StealBenchArg> args(nstealer);
    for (int i = 0; i < nstealer; ++i) {
        args[i] = {&rq, &stop, 0};
        pthread_create(&threads[i], NULL, steal_bench_thread, &args[i]);
    }

    butil::Timer tm;
    tm.start();
    for (int i = 0; i < N; ++i) {
        while (!rq.push((bthread_t)i)) {
            cpu_relax();
        }
    }
    stop.store(true, std::memory_order_release);
    tm.stop();

    int total_stolen = 0;
    for (int i = 0; i < nstealer; ++i) {
        pthread_join(threads[i], NULL);
        total_stolen += args[i].stolen;
    }
    bthread_t tid;
    int remaining = 0;
    while (rq.pop(&tid)) ++remaining;

    printf("  _rq steal (%d stealers):        %6.1f ns/push  (pushed=%d stolen=%d remain=%d)\n",
           nstealer, tm.n_elapsed() / (double)N, N, total_stolen, remaining);
}

// ============================================================
int main() {
    printf("=== B2 Priority Queue Microbenchmark ===\n\n");

    printf("--- B2: Inbound (MPSCQueue, lock-free) ---\n");
    bench_mpsc_single();
    bench_mpsc_multi(2);
    bench_mpsc_multi(4);
    bench_mpsc_multi(8);

    printf("\n--- B2: Flush inbound -> wsq ---\n");
    bench_mpsc_flush(4);
    bench_mpsc_flush(8);
    bench_mpsc_flush(16);
    bench_mpsc_flush(32);

    printf("\n--- B2: WSQ owner push+pop ---\n");
    bench_wsq_pop();

    printf("\n--- B2: WSQ steal (with concurrent push) ---\n");
    bench_wsq_steal(1);
    bench_wsq_steal(2);
    bench_wsq_steal(4);
    bench_wsq_steal(8);

    printf("\n--- B2: Full pipeline (producer -> mpsc -> flush -> pop) ---\n");
    bench_mpsc_pipeline(1);
    bench_mpsc_pipeline(2);
    bench_mpsc_pipeline(4);
    bench_mpsc_pipeline(8);

    printf("\n");
    printf("=== Baseline: Normal bthread scheduling path ===\n\n");

    printf("--- Baseline: _rq push+pop (local worker, WSQ) ---\n");
    bench_baseline_rq();

    printf("\n--- Baseline: _remote_rq push+pop (non-worker, mutex+BoundedQueue) ---\n");
    bench_baseline_remote_rq_single();
    bench_baseline_remote_rq_multi(2);
    bench_baseline_remote_rq_multi(4);
    bench_baseline_remote_rq_multi(8);

    printf("\n--- Baseline: _rq steal (non-owner steals from worker's WSQ) ---\n");
    bench_baseline_rq_steal(1);
    bench_baseline_rq_steal(2);
    bench_baseline_rq_steal(4);
    bench_baseline_rq_steal(8);

    return 0;
}
