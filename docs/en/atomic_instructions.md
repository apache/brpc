[中文版](../cn/atomic_instructions.md)

We know that locks are extensively used in multi-threaded programming to avoid [race conditions](http://en.wikipedia.org/wiki/Race_condition) when modifying shared data. When the lock becomes a bottleneck, we try to walk around it by using atomic instructions. But it is difficult to write correct code with atomic instructions in generally and it is even hard to understand race conditions, [ABA problems](https://en.wikipedia.org/wiki/ABA_problem) and [memory fences](https://en.wikipedia.org/wiki/Memory_barrier). This article tries to introduce basics on atomic instructions(under [SMP](http://en.wikipedia.org/wiki/Symmetric_multiprocessing)). Since [Atomic instructions](http://en.cppreference.com/w/cpp/atomic/atomic) are formally introduced in C++11, we use the APIs directly.

As the name implies, atomic instructions cannot be divided into more sub-instructions. For example, `x.fetch(n)` atomically adds n to x, any internal state is not observable **to software**. Common atomic instructions are listed below:

| Atomic Instructions(type of x is std::atomic\<int\>) | Descriptions                             |
| ---------------------------------------- | ---------------------------------------- |
| x.load()                                 | return the value of x.                   |
| x.store(n)                               | store n to x and return nothing.         |
| x.exchange(n)                            | set x to n and return the value just before the modification |
| x.compare_exchange_strong(expected_ref, desired) | If x is equal to expected_ref, set x to desired and return true. Otherwise write current x to expected_ref and return false. |
| x.compare_exchange_weak(expected_ref, desired) | may have [spurious wakeup](http://en.wikipedia.org/wiki/Spurious_wakeup) comparing to compare_exchange_strong |
| x.fetch_add(n), x.fetch_sub(n)           | do x += n, x-= n atomically. Return the value just before the modification. |

You can already use these instructions to count something atomically, such as counting number of operations from multiple threads. However two problems may arise:

- The operation is not as fast as expected.
- Even if multi-threaded accesses to some resources are controlled by a few atomic instructions that seem to be correct, the program still has great chance to crash.

# Cacheline

An atomic instruction is fast when there's no contentions or it's accessed only by one thread. "Contentions" happen when multiple threads access the same [cacheline](https://en.wikipedia.org/wiki/CPU_cache#Cache_entries). Modern CPU extensively use caches and divide caches into multiple levels to get high performance with a low price. The Intel E5-2620 widely used in Baidu has 32K L1 dcache and icache, 256K L2 cache and 15M L3 cache. L1 and L2 cache is owned by each core, while L3 cache is shared by all cores. Although it is very fast for one core to write data into its own L1 cache(4 cycles, ~2ns), make the data in L1 cache seen by other cores is not, because cachelines touched by the data need to be synchronized to other cores. This process is atomic and transparent to software and no instructions can be interleaved between. Applications have to wait for the completion of [cache coherence](https://en.wikipedia.org/wiki/Cache_coherence), which takes much longer time than writing local cache. It involves a complicated hardware algorithm and makes atomic instructions slow under high contentions. A single fetch_add may take more than 700ns in E5-2620 when a few threads are highly contented on the instruction. Accesses to the memory frequently shared and modified by multiple threads are not fast generally. For example, even if a critical section looks small, the spinlock protecting it may still not perform well. The cause is that the instructions used in spinlock such as exchange, fetch_add etc, need to wait for latest cachelines. It's not surprising to see that one or two instructions take several microseconds.

In order to improve performance, we need to avoid frequently synchronizing cachelines, which not only affects performance of the atomic instruction itself, but also overall performance of the program. The most effective solution is straightforward: **avoid sharing as much as possible**.

- A program relying on a global multiple-producer-multiple-consumer(MPMC) queue is hard to scale well on many CPU cores, because throughput of the queue is limited by delays of cache coherence, rather than number of cores. It would be better to use multiple SPMC or MPSC queues, or even SPSC queues instead, to avoid contentions from the very beginning.
- Another example is counters. If all threads modify a counter frequently, the performance will be poor because all cores are busy synchronizing the same cacheline. If the counter is only used for printing logs periodically or something low-priority like that, we can let each thread modify its own thread-local variables and combine all thread-local data before reading, yielding [much better performance](bvar.md).

A related programming trap is false sharing: Accesses to infrequently updated or even read-only variables are significantly slowed down because other variables in the same cacheline are frequently updated. Variables used in multi-threaded environment should be grouped by accessing frequencies or patterns, variables that are modified by that other threads frequently should be put into separated cachelines. To align a variable or struct by cacheline, `include <butil/macros.h>` and tag it with macro `BAIDU_CACHELINE_ALIGNMENT`, grep source code of brpc for examples.

# Memory fence

Just atomic counting cannot synchronize accesses to resources, simple structures like [spinlock](https://en.wikipedia.org/wiki/Spinlock) or [reference counting](https://en.wikipedia.org/wiki/Reference_counting) that seem correct may crash as well. The key is **instruction reordering**, which may change the order of read/write and cause instructions behind to be reordered to front if there are no dependencies. [Compiler](http://preshing.com/20120625/memory-ordering-at-compile-time/) and [CPU](https://en.wikipedia.org/wiki/Out-of-order_execution) both may reorder.

The motivation is natural: CPU wants to fill each cycle with instructions and execute as many as possible instructions within given time. As said in above section, an instruction for loading memory may cost hundreds of nanoseconds due to cacheline synchronization. A efficient solution for synchronizing multiple cachelines is to move them simultaneously rather than one-by-one. Thus modifications to multiple variables by a thread may be visible to another thread in a different order. On the other hand, different threads need different data, synchronizing on-demand is reasonable and may also change order between cachelines.

For example: the first variable plays the role of switch, controlling accesses to following variables. When these variables are synchronized to other CPU cores, new values may be visible in a different order, and the first variable may not be the first updated, which causes other threads to think that the following variables are still valid, which are actually not, causing undefined behavior. Check code snippet below:

```c++
// Thread 1
// bool ready was initialized to false
p.init();
ready = true;
```

```c++
// Thread2
if (ready) {
    p.bar();
}
```
From a human perspective, the code is correct because thread2 only accesses `p` when `ready` is true which means p is initialized according to the logic in thread1. But the code may not run as expected on multi-core machines:

- `ready = true` in thread1 may be reordered before `p.init()` by compiler or CPU, making thread2 see uninitialized `p` when  `ready` is true. The same reordering may happen in thread2 as well. Some instructions in `p.bar()` may be reordered before checking `ready`.
- Even if above reorderings do not happen, cachelines of `ready` and `p` may be synchronized independently to the CPU core that thread2 runs, making thread2 see unitialized `p` when `ready` is true.

Note: On x86/x64, `load` has acquire semantics and `store` has release semantics by default, the code above may run correctly provided that reordering by compiler is turned off.

With this simple example, you may get a glimpse of the complexity of atomic instructions. In order to solve the reordering issue, CPU and compiler offer [memory fences](http://en.wikipedia.org/wiki/Memory_barrier) to let programmers decide the visibility order between instructions. boost and C++11 conclude memory fence into following types:

| memory order         | Description                              |
| -------------------- | ---------------------------------------- |
| memory_order_relaxed | there are no synchronization or ordering constraints imposed on other reads or writes, only this operation's atomicity is guaranteed |
| memory_order_consume | no reads or writes in the current thread dependent on the value currently loaded can be reordered before this load. |
| memory_order_acquire | no reads or writes in the current thread can be reordered before this load. |
| memory_order_release | no reads or writes in the current thread can be reordered after this store. |
| memory_order_acq_rel | No memory reads or writes in the current thread can be reordered before or after this store. |
| memory_order_seq_cst | Any operation with this memory order is both an acquire operation and a release operation, plus a single total order exists in which all threads observe all modifications in the same order. |

Above example can be modified as follows:

```c++
// Thread1
// std::atomic<bool> ready was initialized to false
p.init();
ready.store(true, std::memory_order_release);
```

```c++
// Thread2
if (ready.load(std::memory_order_acquire)) {
    p.bar();
}
```

The acquire fence in thread2 matches the release fence in thread1, making thread2 see all memory operations before the release fence in thread1 when thread2 sees `ready` being set to true.

Note that memory fence does not guarantee visibility. Even if thread2 reads `ready` just after thread1 sets it to true, thread2 is not guaranteed to see the new value, because cache synchronization takes time. Memory fence guarantees ordering between visibilities: "If I see the new value of a, I should see the new value of b as well".

A related problem: How to know whether a value is updated or not? Two cases in generally:

- The value is special. In above example, `ready=true` is a special value. Once `ready` is true, `p` is ready. Reading special values or not both mean something.
- Increasing-only values. Some situations do not have special values, we can use instructions like `fetch_add` to increase variables. As long as the value range is large enough, new values are different from old ones for a long period of time, so that we can distinguish them from each other.

More examples can be found in [boost.atomic](http://www.boost.org/doc/libs/1_56_0/doc/html/atomic/usage_examples.html). Official descriptions of atomics can be found [here](http://en.cppreference.com/w/cpp/atomic/atomic).

# wait-free & lock-free

Atomic instructions provide two important properties: [wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom) and [lock-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom). Wait-free means no matter how OS schedules, all threads are doing useful jobs; lock-free, which is weaker than wait-free, means no matter how OS schedules, at least one thread is doing useful jobs. If locks are used, the thread holding the lock might be paused by OS, in which case all threads trying to hold the lock are blocked. So code using locks are neither lock-free nor wait-free. To make tasks done within given time, critical paths in real-time OS is at least lock-free. Miscellaneous online services inside Baidu also pose serious restrictions on running time. If the critical path in brpc is wait-free or lock-free, many services are benefited from better and stable QoS. Actually, both read(in the sense of even dispatching) and write in brpc are wait-free, check [IO](io.md) for details.

Note that it is common to think that wait-free or lock-free algorithms are faster, which may not be true, because:

- More complex race conditions and ABA problems must be handled in lock-free and wait-free algorithms, which means the code is often much more complicated than the one using locks. More code, more running time.
- Mutex solves contentions by backoff, which means that when contention happens, another branch is entered to avoid the contention temporarily. Threads failed to lock a mutex are put into sleep, making the thread holding the mutex complete the task or even follow-up tasks exclusively, which may increase the overall throughput.

Low performance caused by mutex is either because of too large critical sections (which limit the concurrency), or too heavy contentions (overhead of context switches becomes dominating). The real value of lock-free/wait-free algorithms is that they guarantee progress of the system, rather than absolutely high performance. Of course lock-free/wait-free algorithms perform better in some situations: if an algorithm is implemented by just one or two atomic instructions, it's probably faster than the one using mutex which needs more atomic instructions in total.
