We know that locks are extensively used in multi-thread programming to avoid [race conditions](http://en.wikipedia.org/wiki/Race_condition) when modifying the same data. When a lock becomes a bottleneck, we try to walk around it by using atomic instructions. But it is difficult to write correct code with atomic instructions in practice and it is hard to understand race conditions, [ABA problems](https://en.wikipedia.org/wiki/ABA_problem) and [memory fences](https://en.wikipedia.org/wiki/Memory_barrier). This artical tries to introduce some basics on atomic instructions(under [SMP](http://en.wikipedia.org/wiki/Symmetric_multiprocessing)). Since [Atomic instructions](http://en.cppreference.com/w/cpp/atomic/atomic) are formally introduced in C++11, we use the APIs directly.

As the name implies, atomic instructions cannot be divided into sub-instructions. For example, `x.fetch(n)` atomically adds n to x, any internal state is not observable **to software**. Common atomic instructions are listed below:

| Atomic Instructions(type of x is std::atomic\<int\>) | Descriptions                             |
| ---------------------------------------- | ---------------------------------------- |
| x.load()                                 | return the value of x.                   |
| x.store(n)                               | store n to x and return nothing.         |
| x.exchange(n)                            | set x to n and return the value just before the atomical set |
| x.compare_exchange_strong(expected_ref, desired) | If x is equal to expected_ref, set x to desired and return true. Otherwise write current x to expected_ref and return false. |
| x.compare_exchange_weak(expected_ref, desired) | may have [spurious wakeup](http://en.wikipedia.org/wiki/Spurious_wakeup) comparing to compare_exchange_strong |
| x.fetch_add(n), x.fetch_sub(n)           | do x += n, x-= n atomically. Return the value just before the modification. |

You can already use these instructions to count stuff atomically, such as counting number of operations on resources used by multiple threads simultaneously. However two problems may arise:

- The operation is not as fast as expected.
- If multi-threaded accesses to some resources are controlled by a few atomic operations that seem to be correct, the program still has great chance to crash.

# Cacheline

An atomic instruction is fast when there's no contentions or accessed by only one thread. "Contentions" happen when multiple threads access the same [cacheline](https://en.wikipedia.org/wiki/CPU_cache#Cache_entries). Modern CPU extensively use caches and divide caches into multiple levels to get high performance with a low price. The Intel E5-2620 widely used in Baidu has 32K L1 dcache and icache, 256K L2 cache and 15M L3 cache. L1 and L2 cache is owned by each core, while L3 cache is shared by all cores. Although it is very fast for one core to write data into its own L1 cache(4 cycles, ~2ns), let the data in L1 cache be seen by other cores is not, because cachelines touched by the data need to be synchronized to other cores. This process is atomic and transparent to software and no instructions can be interleaved between. Applications have wait for the completion of [cache coherence](https://en.wikipedia.org/wiki/Cache_coherence), which takes much longer time than writing local cache. It involves a complicated algorithm and makes atomic instructions slow under high contentions. A single fetch_add may take more than 700ns in E5-2620 when a few threads are highly contented on the instruction. Accesses to the memory frequently shared and modified by multiple threads are not fast generally. For example, even if the critical section is small, using a spinlock may still not work well. The cause is that the instructions used in spinlock such as exchange, fetch_add etc, need to wait for latest cachelines. It's not surprising to see that one or two instructions take several microseconds.

In order to improve performance, we need to avoid synchronizing cachelines frequently, which not only affects performance of the atomic instruction itself, but also overall performance of the program. The most effective solution is straightforward: **avoid sharing as much as possible**. Avoiding contentions from the very beginning is the best strategy:

- A program relying on a global multiple-producer-multiple-consumer(MPMC) queue is hard to scale well on many cpu cores, since throughput of the queue is limited by delays of cache coherence, rather than the number of cores. It would be better to use multiple SPMC or MPSC queues, or even SPSC queues instead, to avoid contentions from the beginning.
- Another example is global counter. If all threads modify a global variable frequently, the performance will be poor because all cores are busy at synchronizing the same cacheline. If the counter is only used for printing logs periodically or something like that, we can let each thread modify its own thread-local variables and combine all thread-local data for a read, resulting a [much better performance](bvar.md).

# Memory fence

Only using atomic addition cannot achieve access control to resources, codes that seem correct may crash as well even for simple [spinlocks](https://en.wikipedia.org/wiki/Spinlock) or [reference count](https://en.wikipedia.org/wiki/Reference_counting). The key point here is that **instruction reorder** change the order of write and read. The instructions(including visiting memory) behind can be reordered to front if there are no dependencies. [Compiler](http://preshing.com/20120625/memory-ordering-at-compile-time/) and [CPU](https://en.wikipedia.org/wiki/Out-of-order_execution) both may do this reordering. The motivation is very natural: cpu should be filled with instructions in every cycle to execute as many as possible instructions in unit time. For example,

```c++
// Thread 1
// ready was initialized to false
p.init();
ready = true;
```

```c++
// Thread2
if (ready) {
    p.bar();
}
```
From the view of human, this code seems correct because thread2 will access `p` only when `ready` is true and that happens after the initilization of p according to thread1. But for multi-core machines, this code may not run as expected:

- `ready = true` in thread1 may be reordered to the position before `p.init()` by compiler or cpu, then when thread2 has seen that `ready` is true, `p` is still not initialized.
- Even if there is no reordering, the value of `ready` and `p` may be synchronized to the cache of core that thread2 runs independently, thread2 may still call `p.bar()` but `p` is not initialized when `ready` is true. The same situation may happens for thread2 as well. For example, some instructions may be reordered to the position before checking `ready`.

Note: In x86, `load` has acquire semantic, and `store` release semantic, so the code above can run correctly if not considering the reordering done by compiler and cpu.

With this simple example, you can get a glimpse of the complexity of programming using atomic instructions. In order to solve this problem, you can use [memory fence](http://en.wikipedia.org/wiki/Memory_barrier) to let programmer decide the visibility relationship between instructions. Boost and C++11 makes an abstraction of memory fence, which can be concluded as following several memory order: 

| memory order         | 作用                                       |
| -------------------- | ---------------------------------------- |
| memory_order_relaxed | there are no synchronization or ordering constraints imposed on other reads or writes, only this operation's atomicity is guaranteed |
| memory_order_consume | no reads or writes in the current thread dependent on the value currently loaded can be reordered before this load. |
| memory_order_acquire | no reads or writes in the current thread can be reordered before this load. |
| memory_order_release | no reads or writes in the current thread can be reordered after this store. |
| memory_order_acq_rel | No memory reads or writes in the current thread can be reordered before or after this store. |
| memory_order_seq_cst | Any operation with this memory order is both an acquire operation and a release operation, plus a single total order exists in which all threads observe all modifications in the same order. |

Using memory order, above example can be modified as such:

```c++
// Thread1
// ready was initialized to false
p.init();
ready.store(true, std::memory_order_release);
```

```c++
// Thread2
if (ready.load(std::memory_order_acquire)) {
    p.bar();
}
```

The acquire in thread2 is matched with the release in thread1, making sure that thread2 can see all memory operations before release operation in thread1 when `ready` is equal to true in thread2.

Notice that memory fence is not equal to visibility. Even though thread2 read the value of ready just after thread1 set it to true, it cannot be guaranteed that thread2 read the newest value, which is caused by the delay of cache synchronization. Memory fence only guarantees the order of visibility: if the program can read the newest value of a, then it can also read the newest value of b. Why does cpu not notify the program when the newest data is ready? First, the delay of read will be increased. Second, read is busy synchronizing when there are plenty of write, making read starve. What's more, Even if the program has read the newest value, this value could be changed when modification instruction is issued, making this policy meaningless.  

Another problem is that if the program read the old value, then nothing should be done, but how does the program know whether it is the old value or the new value? Generally there are two cases:

- Value is special. In above example, `ready=true` is a special value. Reading true from ready means that it is the new value. In this case, every special value has a meaning.
- Always Accumulate. In some situations, there are no special values, we can use instructions like `fetch_add` to accumulate variables. As long as the range of value is big enough, the new value is different from the old value in a long period of time so that we can distinguish them from each other.

More examples can be found [here](http://www.boost.org/doc/libs/1_56_0/doc/html/atomic/usage_examples.html) in boost.atomic. The official description of atomic can be found [here](http://en.cppreference.com/w/cpp/atomic/atomic).

# wait-free & lock-free

Atomic instructions can provide us two important properties: [wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)和[lock-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom). Wait-free means no matter how os(operating system) schedules threads, every thread is doing useful work; lock-free is weaker than wait-free, which means no matter how os schedules threads, at least one thread is doing useful work. If locks are used in the program, then os may schedule out the thread holding the lock in which case all threads trying to hold the same lock is waiting. So Using locks are not lock-free and wait-free. To make sure one task is done always within a determined time, the critical path in real-time os is at least lock-free. In our extensive and diverse online service, the property of real-time is demanded eagerly. If the most critical path in brpc satisfies wait-free of lock-free, we can provide a more stable quality of service. For example, since [fd](https://en.wikipedia.org/wiki/File_descriptor) is only suitable for being manipulated by a single thread, atomic instructions are used in brpc to maximize the concurrency of read and write of fd which is discussed more deeply in [here](io.md).

please notice that it is common to think that algorithms using wait-free or lock-free could be faster, but the truth may be the opposite, because:

- Race condition and ABA problem must be handled in lock-free and wait-free algorithms, which means the code may be more complex than that using locks when doing the same task.
- Using mutex has an effect of backoff. Backoff means that when contention happens, it will find another way to avoid fierce contention. The thread getting an locked mutex will be put into sleep state to avoid cacheline synchronization, making the thread holding that mutex can complete the task quickly, which may increase the overall throughput.

The low performance caused by mutex is because either the critical area is too big(which limits the concurrency), or the critical area is too small(the overhead of context switch becomes prominent, adaptive mutex should be considered to use). The value of lock-free/wait-free is that it guarantees one thread or all threads always doing useful work, not for absolute high performance. But algorithms using lock-free/wait-free may probably have better performance in the situation where algorithm itself can be implemented using a few atomic instructions. Atomic instructions are also used to implement mutex, so if the algorithm can be done just using one or two atomic instructions, it could be faster than that using mutex. 
