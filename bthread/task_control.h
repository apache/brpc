// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_TASK_CONTROL_H
#define BAIDU_BTHREAD_TASK_CONTROL_H

#ifndef NDEBUG
#include <iostream>                             // std::ostream
#endif
#include <stddef.h>                             // size_t
#include "base/atomicops.h"                     // base::atomic
#include "bvar/bvar.h"                          // bvar::PassiveStatus
#include "bthread/task_meta.h"                  // TaskMeta
#include "base/resource_pool.h"                 // ResourcePool
#include "bthread/work_stealing_queue.h"        // WorkStealingQueue

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

    // Suspend caller pthread until a task is stolen.
    int wait_task_once(bthread_t* tid, size_t* seed, size_t offset);

    // Stop and join worker threads in TaskControl.
    void stop_and_join();
    
    // Get # of worker threads.
    int concurrency() const 
    { return _concurrency.load(base::memory_order_acquire); }

    void print(std::ostream& os);  // for debugging

    double get_cumulated_worker_time();
    int64_t get_cumulated_switch_count();
    int64_t get_cumulated_signal_count();

    // [Not thread safe] Add more worker threads.
    // Return the number of workers actually added, which may be less then |num|
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
    
    base::atomic<size_t> _ngroup;
    TaskGroup** _groups;
    base::Mutex _modify_group_mutex;

    bool _stop;
    base::atomic<int> _concurrency;
    std::vector<pthread_t> _workers;

    bvar::Adder<int64_t> _nworkers;
    base::Mutex _pending_time_mutex;
    base::atomic<bool> _pending_time_exposed;
    bvar::LatencyRecorder _pending_time;
    bvar::PassiveStatus<double> _cumulated_worker_time;
    bvar::PerSecond<bvar::PassiveStatus<double> > _worker_usage;
    bvar::PassiveStatus<int64_t> _cumulated_switch_count;
    bvar::PerSecond<bvar::PassiveStatus<int64_t> > _switch_per_second;
    bvar::PassiveStatus<int64_t> _cumulated_signal_count;
    bvar::PerSecond<bvar::PassiveStatus<int64_t> > _signal_per_second;
    bvar::PassiveStatus<std::string> _status;
    bvar::Adder<int64_t> _nbthreads;

    // higher 31 bits count futex, lowest bit stands for stopping.
    base::atomic<int> BAIDU_CACHELINE_ALIGNMENT _pending_signal;
};

inline bvar::LatencyRecorder & TaskControl::exposed_pending_time() {
    if (!_pending_time_exposed.load(base::memory_order_consume)) {
        _pending_time_mutex.lock();
        if (!_pending_time_exposed.load(base::memory_order_consume)) {
            _pending_time.expose("bthread_creation");
            _pending_time_exposed.store(true, base::memory_order_release);
        }
        _pending_time_mutex.unlock();
    }
    return _pending_time;
}

}  // namespace bthread

#endif  // BAIDU_BTHREAD_TASK_CONTROL_H
