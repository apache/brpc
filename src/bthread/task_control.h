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

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_TASK_CONTROL_H
#define BTHREAD_TASK_CONTROL_H

#ifndef NDEBUG
#include <iostream>                             // std::ostream
#endif
#include <signal.h>
#include <stddef.h>                             // size_t
#include <vector>
#include <array>
#include <memory>
#include "butil/atomicops.h"                     // butil::atomic
#include "bvar/bvar.h"                          // bvar::PassiveStatus
#include "bthread/task_tracer.h"
#include "bthread/task_meta.h"                  // TaskMeta
#include "bthread/work_stealing_queue.h"        // WorkStealingQueue
#include "bthread/parking_lot.h"

DECLARE_int32(task_group_ntags);
namespace bthread {

class TaskGroup;

// Control all task groups
class TaskControl {
friend class TaskGroup;
friend void wait_for_butex(void*);
#ifdef BRPC_BTHREAD_TRACER
friend bthread_t init_for_pthread_stack_trace();
#endif // BRPC_BTHREAD_TRACER

public:
    TaskControl();
    ~TaskControl();

    // Must be called before using. `nconcurrency' is # of worker pthreads.
    int init(int nconcurrency);
    
    // Create a TaskGroup in this control.
    TaskGroup* create_group(bthread_tag_t tag);

    // Steal a task from a "random" group.
    bool steal_task(bthread_t* tid, size_t* seed, size_t offset);

    // Tell other groups that `n' tasks was just added to caller's runqueue
    void signal_task(int num_task, bthread_tag_t tag);

    // Stop and join worker threads in TaskControl.
    void stop_and_join();
    
    // Get # of worker threads.
    int concurrency() const 
    { return _concurrency.load(butil::memory_order_acquire); }

    int concurrency(bthread_tag_t tag) const 
    { return _tagged_ngroup[tag].load(butil::memory_order_acquire); }

    void print_rq_sizes(std::ostream& os);

    double get_cumulated_worker_time();
    double get_cumulated_worker_time_with_tag(bthread_tag_t tag);
    int64_t get_cumulated_switch_count();
    int64_t get_cumulated_signal_count();

    // [Not thread safe] Add more worker threads.
    // Return the number of workers actually added, which may be less than |num|
    int add_workers(int num, bthread_tag_t tag);

    // Choose one TaskGroup (randomly right now).
    // If this method is called after init(), it never returns NULL.
    TaskGroup* choose_one_group(bthread_tag_t tag);

#ifdef BRPC_BTHREAD_TRACER
    // A stacktrace of bthread can be helpful in debugging.
    void stack_trace(std::ostream& os, bthread_t tid);
    std::string stack_trace(bthread_t tid);
#endif // BRPC_BTHREAD_TRACER

private:
    typedef std::array<TaskGroup*, BTHREAD_MAX_CONCURRENCY> TaggedGroups;
    static const int PARKING_LOT_NUM = 4;
    typedef std::array<ParkingLot, PARKING_LOT_NUM> TaggedParkingLot;
    // Add/Remove a TaskGroup.
    // Returns 0 on success, -1 otherwise.
    int _add_group(TaskGroup*, bthread_tag_t tag);
    int _destroy_group(TaskGroup*);

    // Tag group
    TaggedGroups& tag_group(bthread_tag_t tag) { return _tagged_groups[tag]; }

    // Tag ngroup
    butil::atomic<size_t>& tag_ngroup(int tag) { return _tagged_ngroup[tag]; }

    // Tag parking slot
    TaggedParkingLot& tag_pl(bthread_tag_t tag) { return _pl[tag]; }

    static void delete_task_group(void* arg);

    static void* worker_thread(void* task_control);

    template <typename F>
    void for_each_task_group(F const& f);

    bvar::LatencyRecorder& exposed_pending_time();
    bvar::LatencyRecorder* create_exposed_pending_time();
    bvar::Adder<int64_t>& tag_nworkers(bthread_tag_t tag);
    bvar::Adder<int64_t>& tag_nbthreads(bthread_tag_t tag);

    std::vector<butil::atomic<size_t>> _tagged_ngroup;
    std::vector<TaggedGroups> _tagged_groups;
    butil::Mutex _modify_group_mutex;

    butil::atomic<bool> _init;  // if not init, bvar will case coredump
    bool _stop;
    butil::atomic<int> _concurrency;
    std::vector<pthread_t> _workers;
    butil::atomic<int> _next_worker_id;

    bvar::Adder<int64_t> _nworkers;
    butil::Mutex _pending_time_mutex;
    butil::atomic<bvar::LatencyRecorder*> _pending_time;
    bvar::PassiveStatus<double> _cumulated_worker_time;
    bvar::PerSecond<bvar::PassiveStatus<double> > _worker_usage_second;
    bvar::PassiveStatus<int64_t> _cumulated_switch_count;
    bvar::PerSecond<bvar::PassiveStatus<int64_t> > _switch_per_second;
    bvar::PassiveStatus<int64_t> _cumulated_signal_count;
    bvar::PerSecond<bvar::PassiveStatus<int64_t> > _signal_per_second;
    bvar::PassiveStatus<std::string> _status;
    bvar::Adder<int64_t> _nbthreads;

    std::vector<bvar::Adder<int64_t>*> _tagged_nworkers;
    std::vector<bvar::PassiveStatus<double>*> _tagged_cumulated_worker_time;
    std::vector<bvar::PerSecond<bvar::PassiveStatus<double>>*> _tagged_worker_usage_second;
    std::vector<bvar::Adder<int64_t>*> _tagged_nbthreads;

    std::vector<TaggedParkingLot> _pl;

#ifdef BRPC_BTHREAD_TRACER
    TaskTracer _task_tracer;
#endif // BRPC_BTHREAD_TRACER

};

inline bvar::LatencyRecorder& TaskControl::exposed_pending_time() {
    bvar::LatencyRecorder* pt = _pending_time.load(butil::memory_order_consume);
    if (!pt) {
        pt = create_exposed_pending_time();
    }
    return *pt;
}

inline bvar::Adder<int64_t>& TaskControl::tag_nworkers(bthread_tag_t tag) {
    return *_tagged_nworkers[tag];
}

inline bvar::Adder<int64_t>& TaskControl::tag_nbthreads(bthread_tag_t tag) {
    return *_tagged_nbthreads[tag];
}

template <typename F>
inline void TaskControl::for_each_task_group(F const& f) {
    if (_init.load(butil::memory_order_acquire) == false) {
        return;
    }
    for (size_t i = 0; i < _tagged_groups.size(); ++i) {
        auto ngroup = tag_ngroup(i).load(butil::memory_order_relaxed);
        auto& groups = tag_group(i);
        for (size_t j = 0; j < ngroup; ++j) {
            f(groups[j]);
        }
    }
}

}  // namespace bthread

#endif  // BTHREAD_TASK_CONTROL_H
