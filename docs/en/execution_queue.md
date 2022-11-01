# Overview

Similar to kylin's ExecMan, [ExecutionQueue](https://github.com/brpc/brpc/blob/master/src/bthread/execution_queue.h) provides the function of asynchronous serial execution. The related technology of ExecutionQueue was first used in RPC to realize [multithreading to write data to the same fd] (io.md#send message). It was added to bthread after r31345. ExecutionQueue provides the following basic functions:

-Asynchronous and orderly execution: The task is executed in a separate thread, and the execution order is strictly the same as the submission order.
-Multi Producer: Multiple threads can submit tasks to an ExecutionQueue at the same time
-Support cancel a submitted task
-Support stop
-Support high-quality tasks to jump in the queue

The main difference with ExecMan:
-ExecutionQueue's task submission interface is [wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom), ExecMan relies on lock, which means that when the machine as a whole is busy , The use of ExecutionQueue will not cause all threads to be blocked because a process is forced to switch by the system.
-ExecutionQueue supports batch processing: The execution thread can batch the submitted tasks to obtain better locality. After a thread of ExecMan has processed the AsyncContext of an AsyncClient, the next task is likely to belong to the AsyncContex of another AsyncClient. At this time, the cpu The cache will constantly switch between the resources that different AsyncClients depend on.
-The processing function of ExecutionQueue will not be bound to a fixed thread for execution. In ExecMan, it is executed according to the AsyncClient hash. The task processing between different ExecutionQueues is completely independent. When the number of threads is sufficient, all Non-idle ExecutionQueue can be scheduled at the same time. It also means that when the number of threads is insufficient, ExecutionQueue cannot guarantee fairness. When this happens, it is necessary to dynamically increase the bthread worker threads to increase the overall processing capacity.
-ExecutionQueue running thread is bthread, you can use some bthread synchronization primitives at will without worrying about blocking the execution of pthread. In ExecMan, try to avoid using synchronization primitives that have a higher probability of causing blocking.

# background

In the field of multi-core concurrent programming, [Message passing](https://en.wikipedia.org/wiki/Message_passing) has been widely used as a means of resolving competition. It splits the logic into Several independent actors, each actor is responsible for the maintenance of corresponding resources, when a process needs to modify a certain resource,
It is converted into a message and sent to the corresponding actor. This actor (usually in another context) modifies the resource according to the content of the command, and then can choose to wake up the caller (synchronous) or submit to the next actor (asynchronous) Method for subsequent processing.

![img](http://web.mit.edu/6.005/www/fa14/classes/20-queues-locks/figures/producer-consumer.png)

# ExecutionQueue Vs Mutex

Both ExecutionQueue and mutex can be used to eliminate competition in multi-threaded scenarios. Compared to using mutex,
Using ExecutionQueue has the following advantages:

-The role division is relatively clear, the conceptual understanding is relatively simple, and there is no need to consider the problems caused by the lock in the implementation (such as deadlock)
-The execution order of tasks can be guaranteed, and the wake-up order of mutex cannot be strictly guaranteed.
-All threads perform their duties and can do useful things, there is no waiting.
-Better batch execution under busy and stuck conditions, and overall higher throughput.

But the disadvantages are also obvious:

-The code of a process is often scattered in multiple places, and the cost of code understanding and maintenance is high.
-In order to improve concurrency, a thing is often split into multiple ExecutionQueues for pipeline processing, which will result in constant switching between multiple cores, additional scheduling and cache synchronization overhead, especially competing When the critical section is very small, these overheads cannot be ignored.
-At the same time, atomic operation of multiple resources will become complicated. Using mutex can lock multiple mutexes at the same time, and using ExecutionQueue requires additional dispatch queue.
-Since all operations are single-threaded, a task running slow will block other operations of the same ExecutionQueue.
-Concurrency control becomes complicated, ExecutionQueue may occupy too much memory due to too many cached tasks.

Regardless of performance and complexity, theoretically any system can use only mutex or ExecutionQueue to eliminate competition.
However, in the design of complex systems, it is recommended to flexibly decide how to use these two tools according to different scenarios:

-If the critical area is very small and the competition is not very fierce, use mutex first, then you can combine with [contention profiler](contention_profiler.md) to determine whether mutex becomes a bottleneck.
-Need for orderly execution, or fierce competition that cannot be eliminated, but batch execution can be used to increase throughput, and you can choose to use ExecutionQueue.

In short, there is no omnipotent model for multi-threaded programming. It is necessary to combine a wealth of profliling tools according to specific scenarios to finally find a suitable balance between complexity and performance.

**Special point**, mutex-free lock/unlock in Linux requires only a few atomic instructions, and the overhead is negligible in most scenarios.

# How to use

### Implement execution function

``
// Iterate over the given tasks
//
// Example:
//
// #include <bthread/execution_queue.h>
//
// int demo_execute(void* meta, TaskIterator<T>& iter) {
//     if (iter.is_stopped()) {
//         // destroy meta and related resources
//         return 0;
//}
//     for (; iter; ++iter) {
//         // do_something(meta, *iter)
//         // or do_something(meta, iter->a_member_of_T)
//}
//     return 0;
//}
template <typename T>
class TaskIterator;
``

### Start an ExecutionQueue:

``
// Start a ExecutionQueue. If |options| is NULL, the queue will be created with
// default options.
// Returns 0 on success, errno otherwise
// NOTE: type |T| can be non-POD but must be copy-constructible
template <typename T>
int execution_queue_start(
ExecutionQueueId<T>* id,
const ExecutionQueueOptions* options,
int (*execute)(void* meta, TaskIterator<T>& iter),
void* meta);
``

The created return value is a 64-bit id, which is equivalent to a [weak reference](https://en.wikipedia.org/wiki/Weak_reference) of an ExecutionQueue instance, which can be wait-free to locate in O(1) time An ExecutionQueue, you can copy this id everywhere, or even put it in RPC, as a remote resource location tool.
You must ensure that the meta's life cycle will not be released before the corresponding ExecutionQueue is actually stopped.

### Stop an ExecutionQueue:

``
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
``

Both stop and join can be called multiple times, and both will behave reasonably. stop can be called at any time without worrying about thread safety issues.

Similar to fd's close, if stop is not called, the corresponding resources will be permanently leaked.

Time to safely release meta: It can be released when iter.is_queue_stopped()==true is received in the execute function, or it can be released after the join returns. Be careful not to double-free

### Submit task

``
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
``

The execution order between high_priority tasks will also be strictly in accordance with the submission order. This is different from ExecMan. The execution order of ExecMan's QueueExecEmergent's AsyncContex is undefined. But this also means that you can't queue any tasks to a high Priority tasks are executed before.

Turn on inplace_if_possible, in a non-competitive scenario, you can save a thread scheduling and cache synchronization overhead. But it may cause deadlock or too many recursive layers (such as non-stop ping-pong) problems, please make sure you open it These problems do not exist in the code of.

### Cancel a submitted task

``
/// [Thread safe and ABA free] Cancel the corresponding task.
// Returns:
//  -1: The task was executed or h is an invalid handle
//  0: Success
//  1: The task is executing
int execution_queue_cancel(const TaskHandle& h);
``

Returning non-zero only means that ExecutionQueue has passed the corresponding task to execute. In the real logic, this task may be cached in another container, so this does not mean that the logical task has ended. You need to do it in your own Business guarantees this.