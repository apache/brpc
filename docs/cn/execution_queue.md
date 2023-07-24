# 概述

类似于kylin的ExecMan, [ExecutionQueue](https://github.com/apache/brpc/blob/master/src/bthread/execution_queue.h)提供了异步串行执行的功能。ExecutionQueue的相关技术最早使用在RPC中实现[多线程向同一个fd写数据](io.md#发消息). 在r31345之后加入到bthread。 ExecutionQueue 提供了如下基本功能:

- 异步有序执行: 任务在另外一个单独的线程中执行, 并且执行顺序严格和提交顺序一致.
- Multi Producer: 多个线程可以同时向一个ExecutionQueue提交任务
- 支持cancel一个已经提交的任务
- 支持stop
- 支持高优任务插队

和ExecMan的主要区别:
- ExecutionQueue的任务提交接口是[wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)的, ExecMan依赖了lock, 这意味着当机器整体比较繁忙的时候，使用ExecutionQueue不会因为某个进程被系统强制切换导致所有线程都被阻塞。
- ExecutionQueue支持批量处理: 执行线程可以批量处理提交的任务, 获得更好的locality. ExecMan的某个线程处理完某个AsyncClient的AsyncContext之后下一个任务很可能是属于另外一个AsyncClient的AsyncContex, 这时候cpu cache会在不同AsyncClient依赖的资源间进行不停的切换。
- ExecutionQueue的处理函数不会被绑定到固定的线程中执行, ExecMan中是根据AsyncClient hash到固定的执行线程，不同的ExecutionQueue之间的任务处理完全独立，当线程数足够多的情况下，所有非空闲的ExecutionQueue都能同时得到调度。同时也意味着当线程数不足的时候，ExecutionQueue无法保证公平性, 当发生这种情况的时候需要动态增加bthread的worker线程来增加整体的处理能力.
- ExecutionQueue运行线程为bthread, 可以随意的使用一些bthread同步原语而不用担心阻塞pthread的执行. 而在ExecMan里面得尽量避免使用较高概率会导致阻塞的同步原语.

# 背景

在多核并发编程领域， [Message passing](https://en.wikipedia.org/wiki/Message_passing)作为一种解决竞争的手段得到了比较广泛的应用，它按照业务依赖的资源将逻辑拆分成若干个独立actor，每个actor负责对应资源的维护工作，当一个流程需要修改某个资源的时候，
就转化为一个消息发送给对应actor，这个actor(通常在另外的上下文中)根据命令内容对这个资源进行相应的修改，之后可以选择唤醒调用者(同步)或者提交到下一个actor(异步)的方式进行后续处理。

![img](http://web.mit.edu/6.005/www/fa14/classes/20-queues-locks/figures/producer-consumer.png)

# ExecutionQueue Vs Mutex

ExecutionQueue和mutex都可以用来在多线程场景中消除竞争. 相比较使用mutex,
使用ExecutionQueue有着如下几个优点:

- 角色划分比较清晰, 概念理解上比较简单, 实现中无需考虑锁带来的问题(比如死锁)
- 能保证任务的执行顺序，mutex的唤醒顺序不能得到严格保证.
- 所有线程各司其职，都能在做有用的事情，不存在等待.
- 在繁忙、卡顿的情况下能更好的批量执行，整体上获得较高的吞吐.

但是缺点也同样明显:

- 一个流程的代码往往散落在多个地方，代码理解和维护成本高。
- 为了提高并发度， 一件事情往往会被拆分到多个ExecutionQueue进行流水线处理，这样会导致在多核之间不停的进行切换，会付出额外的调度以及同步cache的开销, 尤其是竞争的临界区非常小的情况下， 这些开销不能忽略.
- 同时原子的操作多个资源实现会变得复杂, 使用mutex可以同时锁住多个mutex, 用了ExeuctionQueue就需要依赖额外的dispatch queue了。
- 由于所有操作都是单线程的，某个任务运行慢了就会阻塞同一个ExecutionQueue的其他操作。
- 并发控制变得复杂，ExecutionQueue可能会由于缓存的任务过多占用过多的内存。

不考虑性能和复杂度，理论上任何系统都可以只使用mutex或者ExecutionQueue来消除竞争.
但是复杂系统的设计上，建议根据不同的场景灵活决定如何使用这两个工具:

- 如果临界区非常小，竞争又不是很激烈，优先选择使用mutex, 之后可以结合[contention profiler](contention_profiler.md)来判断mutex是否成为瓶颈。
- 需要有序执行，或者无法消除的激烈竞争但是可以通过批量执行来提高吞吐， 可以选择使用ExecutionQueue。

总之，多线程编程没有万能的模型，需要根据具体的场景，结合丰富的profliling工具，最终在复杂度和性能之间找到合适的平衡。

**特别指出一点**，Linux中mutex无竞争的lock/unlock只有需要几条原子指令，在绝大多数场景下的开销都可以忽略不计.

# 使用方式

### 实现执行函数

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

### 启动一个ExecutionQueue:

```
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
```

创建的返回值是一个64位的id, 相当于ExecutionQueue实例的一个[弱引用](https://en.wikipedia.org/wiki/Weak_reference), 可以wait-free的在O(1)时间内定位一个ExecutionQueue, 你可以到处拷贝这个id， 甚至可以放在RPC中，作为远端资源的定位工具。
你必须保证meta的生命周期，在对应的ExecutionQueue真正停止前不会释放.

### 停止一个ExecutionQueue:

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

stop和join都可以多次调用， 都会有合理的行为。stop可以随时调用而不用当心线程安全性问题。

和fd的close类似，如果stop不被调用, 相应的资源会永久泄露。

安全释放meta的时机: 可以在execute函数中收到iter.is_queue_stopped()==true的任务的时候释放，也可以等到join返回之后释放. 注意不要double-free

### 提交任务

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
// If |options| is NULL, we will use default options (normal task)
// If |handle| is not NULL, we will assign it with the handler of this task.
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

high_priority的task之间的执行顺序也会**严格按照提交顺序**, 这点和ExecMan不同, ExecMan的QueueExecEmergent的AsyncContex执行顺序是undefined. 但是这也意味着你没有办法将任何任务插队到一个high priority的任务之前执行.

开启inplace_if_possible, 在无竞争的场景中可以省去一次线程调度和cache同步的开销. 但是可能会造成死锁或者递归层数过多(比如不停的ping-pong)的问题，开启前请确定你的代码中不存在这些问题。

### 取消一个已提交任务

```
/// [Thread safe and ABA free] Cancel the corresponding task.
// Returns:
//  -1: The task was executed or h is an invalid handle
//  0: Success
//  1: The task is executing
int execution_queue_cancel(const TaskHandle& h);
```

返回非0仅仅意味着ExecutionQueue已经将对应的task递给过execute, 真实的逻辑中可能将这个task缓存在另外的容器中，所以这并不意味着逻辑上的task已经结束，你需要在自己的业务上保证这一点.
