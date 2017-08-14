// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_TASK_GROUP_H
#define BAIDU_BTHREAD_TASK_GROUP_H

#include "base/time.h"                             // cpuwide_time_ns
#include "bthread/task_meta.h"                     // bthread_t, TaskMeta
#include "bthread/work_stealing_queue.h"           // WorkStealingQueue
#include "base/resource_pool.h"                    // ResourceId

namespace bthread {

class ExitException : public std::exception {
public:
    explicit ExitException(void* value) : _value(value) {}
    ~ExitException() throw() {}
    const char* what() const throw() {
        return "ExitException";
    }
    void* value() const {
        return _value;
    }
private:
    void* _value;
};

class TaskControl;

// Thread-local group of tasks.
// Notice that most methods involving context switching are static otherwise
// pointer `this' may change after wakeup. The **pg parameters in following
// function are updated before returning.
class TaskGroup {
    friend class TaskControl;

public:
    static const int STOP_INDEX = 1000000;
    static const int NOHINT_INDEX = -1;
    static const int IDLE_INDEX = -2;

    // Create task `fn(arg)' with attributes `attr' in TaskGroup *pg and put
    // the identifier into `tid'. Switch to the new task and schedule old task
    // to run.
    // Return 0 on success, errno otherwise.
    static int start_foreground(TaskGroup** pg,
                                bthread_t* __restrict tid,
                                const bthread_attr_t* __restrict attr,
                                void * (*fn)(void*),
                                void* __restrict arg);

    // Create task `fn(arg)' with attributes `attr' in this TaskGroup, put the
    // identifier into `tid'. Schedule the new thread to run.
    // Return 0 on success, errno otherwise.
    int start_background(bthread_t* __restrict tid,
                         const bthread_attr_t* __restrict attr,
                         void * (*fn)(void*),
                         void* __restrict arg);

    // Suspend caller and run next bthread in TaskGroup *pg.
    static void sched(TaskGroup** pg);
    static void ending_sched(TaskGroup** pg);

    // Suspend caller and run bthread `next_tid' in TaskGroup *pg.
    // Purpose of this function is to avoid pushing `next_tid' to local
    // runqueue and then calling sched(pg), which has similar effect but
    // slower.
    static void sched_to(TaskGroup** pg, TaskMeta* next_meta);
    inline static void sched_to(TaskGroup** pg, bthread_t next_tid);
    
    inline static void exchange(TaskGroup** pg, bthread_t next_tid);

    inline void set_remained(void (*last_context_remained)(void*), void* arg);

    // Suspend caller for at least |timeout_us| microseconds.
    // If |timeout_us| is 0, this function does nothing.
    // If |group| is NULL or current thread is non-bthread, call usleep(3)
    // instead. This function does not create thread-local TaskGroup.
    // Returns: 0 when successful, -1 otherwise and errno is set.
    static int usleep(TaskGroup** pg, uint64_t timeout_us);

    static int yield(TaskGroup** pg);

    // Suspend caller until bthread `tid' terminates.
    static int join(bthread_t tid, void** return_value);

    // Returns true iff the bthread `tid' still exists. Notice that it is
    // just the result at this very moment which may change soon.
    // Don't use this function unless you have to. Never write code like this:
    //    if (exists(tid)) {
    //        Wait for events of the thread.   // Racy, may block indefinitely.
    //    }
    static bool exists(bthread_t tid);

    static int get_attr(bthread_t tid, bthread_attr_t* attr);
    
    static int stopped(bthread_t tid);

    // Identifier/statistics of the bthread associated with
    // TaskControl::worker_thread
    bthread_t main_tid() const { return _main_tid; }
    TaskStatistics main_stat() const;
    // Routine of the main task which should be called from a dedicated pthread.
    void run_main_task();

    // Meta/Identifier of current bthread in this group.
    TaskMeta* current_task() const { return _cur_meta; }
    bthread_t current_tid() const { return _cur_meta->tid; }

    inline bool is_current_main_task() const;
    inline bool is_current_pthread_task() const;

    inline int64_t current_uptime_ns() const;
    inline int64_t cumulated_cputime_ns() const { return _cumulated_cputime_ns; }

    void ready_to_run(bthread_t tid);
    void ready_to_run_nosignal(bthread_t tid);

    // Flush tasks in rq but signalled.
    // Returns #tasks flushed.
    int flush_nosignal_tasks();
    
    TaskControl* control() const { return _control; }

    // Call this instead of delete.
    void destroy_self();

    int stop_usleep(bthread_t tid);

    inline static TaskMeta* address_meta(bthread_t tid);

    // The pthread where this TaskGroup is constructed. With current
    // implementation, the pthread is also the worker pthread or the user
    // pthread launching bthreads. butex_wait() uses this fact to get
    // pthread of TaskGroup without calling pthread_self() repeatly.
    pthread_t creation_pthread() const { return _creation_pthread; }

private:
    // You shall use TaskControl::create_group to create new instance.
    explicit TaskGroup(TaskControl*);

    int init(size_t runqueue_capacity);

    // You shall call destroy_self() instead of destructor because deletion
    // of groups are postponed to avoid race.
    ~TaskGroup();

    static void task_runner(intptr_t skip_remained);

    static void _release_last_context(void*);

    static void _add_sleep_event(void* arg);

    static void ready_to_run_in_worker(void*);
    static void ready_to_run_in_worker_nosignal(void*);

    bool wait_task(bthread_t* tid, size_t* seed, size_t offset);

#ifndef NDEBUG
    int _sched_recursive_guard;
#endif

    TaskMeta* _cur_meta;
    
    // the control that this group belongs to
    TaskControl* _control;
    int _num_nosignal;
    int _nsignaled;
    // last scheduling time
    int64_t _last_run_ns;
    int64_t _cumulated_cputime_ns;

    size_t _nswitch;
    void (*_last_context_remained)(void*);
    void* _last_context_remained_arg;

    size_t _steal_seed;
    size_t _steal_offset;
    StackContainer* _main_stack_container;
    bthread_t _main_tid;
    pthread_t _creation_pthread;
    // NOTE: _rq_mutex is not in the same cacheline of _rq because in some
    // scenarios _rq_mutex is mostly locked/unlocked by this thread only.
    // As a contrast, _rq is always stolen by other threads frequently.
    base::Mutex _rq_mutex;

    WorkStealingQueue<bthread_t> BAIDU_CACHELINE_ALIGNMENT _rq;
};

}  // namespace bthread

#include "task_group_inl.h"

#endif  // BAIDU_BTHREAD_TASK_GROUP_H
