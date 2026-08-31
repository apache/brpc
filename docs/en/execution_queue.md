[中文版](../cn/execution_queue.md)

# Overview

Like kylin's ExecMan, [ExecutionQueue](https://github.com/apache/brpc/blob/master/src/bthread/execution_queue.h) runs tasks asynchronously and serially. The technique was first used in RPC to [write to the same fd from multiple threads](io.md#sending-messages). It was added to bthread after r31345. ExecutionQueue provides:

- Async ordered execution: tasks run on a separate thread, strictly in submit order.
- Multi producer: several threads can submit to one ExecutionQueue at the same time.
- Cancel a submitted task
- Stop
- High-priority tasks that jump the queue

Main differences from ExecMan:

- Submit is [wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom). ExecMan uses a lock, so when the machine is busy, one process being descheduled can block every thread. ExecutionQueue does not.
- Batching: the consumer can process submitted tasks in batches for better locality. After ExecMan finishes one AsyncClient's AsyncContext, the next task is often another client's, so the CPU cache keeps bouncing between those resources.
- The execute function is not pinned to a fixed thread. ExecMan hashes AsyncClient onto a fixed worker. Different ExecutionQueues are independent; with enough threads, every non-idle queue can run at once. With too few threads, ExecutionQueue cannot guarantee fairness — then you need to add bthread workers to raise overall capacity.
- The consumer runs as a bthread, so you can use bthread sync primitives without blocking a pthread. In ExecMan you should avoid primitives that are likely to block.

# Background

In multi-core programming, [message passing](https://en.wikipedia.org/wiki/Message_passing) is a common way to remove races. Logic is split by the resources it depends on into independent actors. Each actor owns its resource. Changing a resource becomes a message to that actor. The actor (usually in another context) applies the command, then either wakes the caller (sync) or submits to the next actor (async).

![img](http://web.mit.edu/6.005/www/fa14/classes/20-queues-locks/figures/producer-consumer.png)

# ExecutionQueue vs mutex

Both ExecutionQueue and mutex can remove races among threads. Compared with a mutex, ExecutionQueue has these advantages:

- Roles are clear, the idea is simple, and you do not have to reason about lock problems (such as deadlock).
- Task order is guaranteed; mutex wakeup order is not.
- Every thread is doing useful work; nobody waits.
- Under load or stalls, batching gives higher overall throughput.

The drawbacks are equally real:

- One flow's code is often scattered, so it is harder to read and maintain.
- To raise concurrency, one job is often pipelined across several ExecutionQueues, which hops between cores and pays extra scheduling and cache-sync cost. When the critical section is tiny, that cost is not negligible.
- Operating on several resources atomically gets harder. With mutexes you can lock several of them; with ExecutionQueue you need an extra dispatch queue.
- Everything is single-threaded on that queue, so a slow task blocks every other task on the same ExecutionQueue.
- Flow control is harder. A queue that caches too many tasks can use too much memory.

Ignoring performance and complexity, any system can theoretically use only mutexes or only ExecutionQueues to remove races. For a complex system, pick per scenario:

- If the critical section is tiny and contention is light, prefer a mutex, then use the [contention profiler](contention_profiler.md) to see whether it becomes a bottleneck.
- If you need ordered execution, or contention you cannot remove but can batch for throughput, choose ExecutionQueue.

There is no universal multi-thread model. Combine profiling with the actual workload and balance complexity against performance.

**One extra note**: an uncontended Linux mutex lock/unlock is only a few atomic instructions, and the cost is negligible in most cases.

# Usage

### Implement the execute function

```
// Iterate over the given tasks
//
// Example:
//
// #include <bthread/execution_queue.h>
//
// int demo_execute(void* meta, TaskIterator<T>& iter) {
//     if (iter.is_queue_stopped()) {
//         // destroy meta and related resources
//         return 0;
//     }
//     for (; iter; ++iter) {
//         // do_something(meta, *iter)
//         // or do_something(meta, iter->a_member_of_T)
//     }
//     return 0;
// }
template <typename T>
class TaskIterator;
```

### Start an ExecutionQueue

```
struct ExecutionQueueOptions {
    ExecutionQueueOptions();

    // Execute in resident pthread instead of bthread. default: false.
    bool use_pthread;

    // Attribute of the bthread which execute runs on. default: BTHREAD_ATTR_NORMAL
    // Bthread will be used when executor = nullptr and use_pthread == false.
    bthread_attr_t bthread_attr;

    // Executor that tasks run on. default: nullptr
    // Note that TaskOptions.in_place_if_possible = false will not work, if implementation of
    // Executor is in-place(synchronous).
    Executor * executor;
};

// Start a ExecutionQueue. If |options| is nullptr, the queue will be created with
// default options.
// Returns 0 on success, errno otherwise
// NOTE: type |T| can be non-POD but must be copy-constructible
template <typename T>
int execution_queue_start(
        ExecutionQueueId<T>* id,
        const ExecutionQueueOptions* options,
        int (*execute)(void* meta, TaskIterator<T>& iter),
        void* meta);
```

The return value is a 64-bit id, a [weak reference](https://en.wikipedia.org/wiki/Weak_reference) to the ExecutionQueue instance. You can locate the queue wait-free in O(1). You can copy the id freely, even send it in an RPC as a handle to a remote resource.
You must keep `meta` alive until the ExecutionQueue has really stopped.

### Stop an ExecutionQueue

```
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
```

`stop` and `join` can be called more than once and still behave reasonably. `stop` can be called at any time without worrying about thread safety.

Like `close` on an fd, if `stop` is never called, the resource leaks forever.

Safe time to free `meta`: when `execute` sees `iter.is_queue_stopped() == true`, or after `join` returns. Do not double-free.

### Submit a task

```
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
 
const static TaskOptions TASK_OPTIONS_NORMAL = TaskOptions(/*high_priority=*/ false, /*in_place_if_possible=*/ false);
const static TaskOptions TASK_OPTIONS_URGENT = TaskOptions(/*high_priority=*/ true, /*in_place_if_possible=*/ false);
const static TaskOptions TASK_OPTIONS_INPLACE = TaskOptions(/*high_priority=*/ false, /*in_place_if_possible=*/ true);
 
// Thread-safe and Wait-free.
// Execute a task with defaut TaskOptions (normal task);
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            typename butil::add_const_reference<T>::type task);
 
// Thread-safe and Wait-free.
// Execute a task with options. e.g
// bthread::execution_queue_execute(queue, task, &bthread::TASK_OPTIONS_URGENT)
// If |options| is nullptr, we will use default options (normal task)
// If |handle| is not nullptr, we will assign it with the handler of this task.
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            typename butil::add_const_reference<T>::type task,
                            const TaskOptions* options);
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            typename butil::add_const_reference<T>::type task,
                            const TaskOptions* options,
                            TaskHandle* handle);
                            
template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            T&& task);

template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            T&& task,
                            const TaskOptions* options);

template <typename T>
int execution_queue_execute(ExecutionQueueId<T> id,
                            T&& task,
                            const TaskOptions* options,
                            TaskHandle* handle);
                            
```

High-priority tasks also run **strictly in submit order**, unlike ExecMan, where `QueueExecEmergent` AsyncContext order is undefined. That also means you cannot jump ahead of an already-submitted high-priority task.

`in_place_if_possible` skips one thread schedule and cache sync when there is no contention. It can deadlock or recurse too deep (for example endless ping-pong). Turn it on only if your code does not have those problems.

### Cancel a submitted task

```
/// [Thread safe and ABA free] Cancel the corresponding task.
// Returns:
//  -1: The task was executed or h is an invalid handle
//  0: Success
//  1: The task is executing
int execution_queue_cancel(const TaskHandle& h);
```

A non-zero return only means ExecutionQueue has already handed the task to `execute`. The real logic may still cache that task in another container, so it does not mean the logical task is done. You have to guarantee that in your own code.
