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
#include <stddef.h>                             // size_t
#include "butil/atomicops.h"                     // butil::atomic
#include "bvar/bvar.h"                          // bvar::PassiveStatus
#include "bthread/task_meta.h"                  // TaskMeta
#include "butil/resource_pool.h"                 // ResourcePool
#include "bthread/work_stealing_queue.h"        // WorkStealingQueue
#include "bthread/parking_lot.h"

namespace bthread {

class TaskGroup;

// Control all task groups
class TaskControl {
    friend class TaskGroup;

public:
    TaskControl();
    ~TaskControl();

    // Must be called before using. `nconcurrency' is # of worker pthreads.
    int init(int nconcurrency);
    
    // Create a TaskGroup in this control.
    TaskGroup* create_group();

    // Steal a task from a "random" group.
    bool steal_task(bthread_t* tid, size_t* seed, size_t offset);

    // Tell other groups that `n' tasks was just added to caller's runqueue
    void signal_task(int num_task);

    // Stop and join worker threads in TaskControl.
    void stop_and_join();
    
    // Get # of worker threads.
    int concurrency() const 
    { return _concurrency.load(butil::memory_order_acquire); }

    void print_rq_sizes(std::ostream& os);

    double get_cumulated_worker_time();
    int64_t get_cumulated_switch_count();
    int64_t get_cumulated_signal_count();

    // [Not thread safe] Add more worker threads.
    // Return the number of workers actually added, which may be less than |num|
    int add_workers(int num);

    // Choose one TaskGroup (randomly right now).
    // If this method is called after init(), it never returns NULL.
    TaskGroup* choose_one_group();

private:
    // Add/Remove a TaskGroup.
    // Returns 0 on success, -1 otherwise.
    int _add_group(TaskGroup*);
    int _destroy_group(TaskGroup*);

    static void delete_task_group(void* arg);

    static void* worker_thread(void* task_control);

    bvar::LatencyRecorder& exposed_pending_time();
    bvar::LatencyRecorder* create_exposed_pending_time();

    butil::atomic<size_t> _ngroup;
    TaskGroup** _groups;
    butil::Mutex _modify_group_mutex;

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

    static const int PARKING_LOT_NUM = 4;
    ParkingLot _pl[PARKING_LOT_NUM];
};

inline bvar::LatencyRecorder& TaskControl::exposed_pending_time() {
    bvar::LatencyRecorder* pt = _pending_time.load(butil::memory_order_consume);
    if (!pt) {
        pt = create_exposed_pending_time();
    }
    return *pt;
}

}  // namespace bthread

#endif  // BTHREAD_TASK_CONTROL_H
