// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/10/23 18:16:16

#ifndef  BTHREAD_EXECUTION_QUEUE_H
#define  BTHREAD_EXECUTION_QUEUE_H

#include "bthread/bthread.h"
#include "butil/type_traits.h"

namespace bthread {

// ExecutionQueue is a special wait-free MPSC queue of which the consumer thread
// is auto started by the execute operation and auto quits if there are no more 
// tasks, in another word there isn't a daemon bthread waiting to consume tasks

template <typename T> struct ExecutionQueueId;
template <typename T> class ExecutionQueue;
struct TaskNode;
class ExecutionQueueBase;

class TaskIteratorBase {
DISALLOW_COPY_AND_ASSIGN(TaskIteratorBase);
friend class ExecutionQueueBase;
public:
    // Returns true when the ExecutionQueue is stopped and there will never be
    // more tasks and you can safely release all the related resources ever 
    // after.
    bool is_queue_stopped() const { return _is_stopped; }
    operator bool() const;
protected:
    TaskIteratorBase(TaskNode* head, ExecutionQueueBase* queue,
                     bool is_stopped, bool high_priority)
        : _cur_node(head)
        , _head(head)
        , _q(queue)
        , _is_stopped(is_stopped)
        , _high_priority(high_priority)
        , _should_break(false)
        , _num_iterated(0)
    { operator++(); }
    ~TaskIteratorBase();
    void operator++();
    TaskNode* cur_node() const { return _cur_node; }
private:
    int num_iterated() const { return _num_iterated; }
    bool should_break_for_high_priority_tasks();

    TaskNode*               _cur_node;
    TaskNode*               _head;
    ExecutionQueueBase*     _q;
    bool                    _is_stopped;
    bool                    _high_priority;
    bool                    _should_break;
    int                     _num_iterated;
};

// Iterate over the given tasks
// 
// Examples:
// int demo_execute(void* meta, TaskIterator<T>& iter) {
//     if (iter.is_stopped()) {
//         // destroy meta and related resources
//         return 0;
//     }
//     for (; iter; ++iter) {
//         // do_something(*iter)
//         // or do_something(iter->a_member_of_T)
//     }
//     return 0;
// }
template <typename T>
class TaskIterator : public TaskIteratorBase {
    TaskIterator();
public:
    typedef T*          pointer;
    typedef T&          reference;

    reference operator*() const;
    pointer operator->() const { return &(operator*()); }
    TaskIterator& operator++();
    void operator++(int);
};

struct TaskHandle {
    TaskHandle();
    TaskNode* node;
    int64_t version;
};

struct TaskOptions {
    TaskOptions();
    TaskOptions(bool high_priority, bool in_place_if_possible);

    // Executor would execute high-priority tasks in the FIFO order but before 
    // all pending normal-priority tasks.
    // NOTE: We don't guarantee any kind of real-time as there might be tasks still
    // in process which are uninterruptible.
    //
    // Default: false 
    bool high_priority;

    // If |in_place_if_possible| is true, execution_queue_execute would call 
    // execute immediately instead of starting a bthread if possible
    //
    // Note: Running callbacks in place might cause the dead lock issue, you
    // should be very careful turning this flag on.
    //
    // Default: false
    bool in_place_if_possible;
};

const static TaskOptions TASK_OPTIONS_NORMAL = TaskOptions(false, false);
const static TaskOptions TASK_OPTIONS_URGENT = TaskOptions(true, false);
const static TaskOptions TASK_OPTIONS_INPLACE = TaskOptions(false, true);

class Executor {
public:
    virtual ~Executor() {}

    // Return 0 on success.
    virtual int submit(void * (*fn)(void*), void* args) = 0;
};

struct ExecutionQueueOptions {
    ExecutionQueueOptions();
    // Attribute of the bthread which execute runs on
    // default: BTHREAD_ATTR_NORMAL
    bthread_attr_t bthread_attr;

    // Executor that tasks run on. bthread will be used when executor = NULL.
    // Note that TaskOptions.in_place_if_possible = false will not work, if implementation of
    // Executor is in-place(synchronous).
    Executor * executor;
};

// Start a ExecutionQueue. If |options| is NULL, the queue will be created with
// the default options. 
// Returns 0 on success, errno otherwise
// NOTE: type |T| can be non-POD but must be copy-constructible
template <typename T>
int execution_queue_start(
        ExecutionQueueId<T>* id, 
        const ExecutionQueueOptions* options,
        int (*execute)(void* meta, TaskIterator<T>& iter),
        void* meta);

// Stop the ExecutionQueue.
// After this function is called:
//  - All the following calls to execution_queue_execute would fail immediately.
//  - The executor will call |execute| with TaskIterator::is_queue_stopped() being 
//    true exactly once when all the pending tasks have been executed, and after
//    this point it's ok to release the resource referenced by |meta|.
// Returns 0 on success, errno othrwise
template <typename T>
int execution_queue_stop(ExecutionQueueId<T> id);

// Wait until the the stop task (Iterator::is_queue_stopped() returns true) has
// been executed
template <typename T>
int execution_queue_join(ExecutionQueueId<T> id);

// Thread-safe and Wait-free.
// Execute a task with defaut TaskOptions (normal task);
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id, 
                            typename butil::add_const_reference<T>::type task);

// Thread-safe and Wait-free.
// Execute a task with options. e.g
// bthread::execution_queue_execute(queue, task, &bthread::TASK_OPTIONS_URGENT)
// If |options| is NULL, we will use default options (normal task)
// If |handle| is not NULL, we will assign it with the hanlder of this task.
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id, 
                            typename butil::add_const_reference<T>::type task,
                            const TaskOptions* options);
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id, 
                            typename butil::add_const_reference<T>::type task,
                            const TaskOptions* options,
                            TaskHandle* handle);

// [Thread safe and ABA free] Cancel the corrosponding task.
// Returns:
//  -1: The task was executed or h is an invalid handle
//  0: Success
//  1: The task is executing 
int execution_queue_cancel(const TaskHandle& h);

// Thread-safe and Wait-free
// Address a reference of ExecutionQueue if |id| references to a valid 
// ExecutionQueue
//
// |execution_queue_execute| internally fetches a reference of ExecutionQueue at
// the begining and releases it at the end, which makes 2 additional cache
// updates. In some critical situation where the overhead of
// execution_queue_execute matters, you can avoid this by addressing the 
// reference at the begining of every producer, and execute tasks execatly 
// through the reference instead of id.
//
// Note: It makes |execution_queue_stop| a little complicated in the user level,
// as we don't pass the `stop task' to |execute| until no one holds any reference.
// If you are not sure about the ownership of the return value (which releasees
// the reference of the very ExecutionQueue in the destructor) and don't that
// care the overhead of ExecutionQueue, DON'T use this function
template <typename T>
typename ExecutionQueue<T>::scoped_ptr_t 
execution_queue_address(ExecutionQueueId<T> id);

}  // namespace bthread

#include "bthread/execution_queue_inl.h"

#endif  //BTHREAD_EXECUTION_QUEUE_H
