We all know that locks are needed in multi-thread programming to avoid potential [race condition](http://en.wikipedia.org/wiki/Race_condition) when modifying the same data. But In practice, it is difficult to write correct codes using atomic instructions. It is hard to understand race condition, [ABA problem](https://en.wikipedia.org/wiki/ABA_problem), [memory fence](https://en.wikipedia.org/wiki/Memory_barrier). This artical is to help you get started by introducing atomic instructions under [SMP](http://en.wikipedia.org/wiki/Symmetric_multiprocessing). [Atomic instructions](http://en.cppreference.com/w/cpp/atomic/atomic) are formally introduced in C++11.

As the name suggests, atomic instructions cannot be divided into sub-instructions. For example, `x.fetch(n)` atomically adds n to x, any internal state will not be observed. Common atomic instructions include:

| Atomic Instructions(type of x is std::atomic<int>)               | effect                                       |
| ---------------------------------------- | ---------------------------------------- |
| x.load()                                 | return the value of x.                                   |
| x.store(n)                               |                             |
| x.exchange(n)                            | set x to n, and return the previous value                          |
| x.compare_exchange_strong(expected_ref, desired) | If x is equal to expected_ref, x is set to desired and true is returned. Otherwise write current value to expected_ref and false is returned. |
| x.compare_exchange_weak(expected_ref, desired) | When compared to compare_exchange_strong, it may suffer from [spurious wakeup](http://en.wikipedia.org/wiki/Spurious_wakeup)。 |
| x.fetch_add(n), x.fetch_sub(n), x.fetch_xxx(n) | x += n, x-= n(or more instructions)，the value before modification is returned.           |

You can already use these instructions to do atomic counting, such as multiple threads at the same time accumulate an atomic variable to count the number of operation on some resources by these threads. But this may cause two problems:

- The operation is not as fast as you expect.
- If you try to control some of the resources through seemingly simple atomic operations, your program has a lot of chance to crash.

# Cacheline

An atomic instruction is relatively fast when there is not contention or only one thread accessing it. Contention happens when there are multiple threads accessing the same [cacheline](https://en.wikipedia.org/wiki/CPU_cache#Cache_entries). Modern CPU extensively use cache and divide cache into multi-level to get high performance at a low price. The widely used cpu in Baidu which is Intel E5-2620  has 32K L1 dcache and icache, 256K L2 cache and 15M L3 cache. L1 and L2 cache is owned by each core, while L3 cache is shared by all cores. Although it is fast for one core to write data into its own L1 cache(4 cycles, 2ns), the data in L1 cache should be also seen by another core when it needs writing or reading from corresponding address. To application, this process is atomic and no instructions can be interleaved. Application must wait for the completion of [cache coherence](https://en.wikipedia.org/wiki/Cache_coherence), which takes longer time compared to other operations. It involves a complicated algorithm which takes approximately 700ns in E5-2620 when highly contented. So it is slow to access the memory shared by multiple threads.

In order to improve performance, we need to avoid synchronizing cacheline in CPU. This is not only related to the performance of the atomic instruction itself, but also affect the overall performance of the program. For example, the effect of using spinlock is still poor in some small critical area scenarios. The problem is that the instruction of exchange, fetch_add and other instructions used to implement spinlock must be executed after the latest cacheline has been synchronized. Although it involves only a few instructions, it is not surprising that these instructions spend a few microseconds. The most effective solution is straightforward: **avoid sharing as possible as you can**. Avoiding contention from the beginning is the best.

- A program using a global multiple-producer-multiple-consumer(MPMC) queue is hard to have multi-core scalability, since the limit throughput of this queue depends on the delay of cpu cache synchronization, rather than the number of cores. It is a best practice to use multiple SPMC or multiple MPSC queue, or even multiple SPSC queue instead, avoid contention at the beginning.
- Another example is global counter. If all threads modify a global variable frequently, the performance would be poor because all cores are busy synchronizing the same cacheline. If the counter is only used to print logs or something like that, we can let each thread modify thread-local variables and combine all the data when need. This may cause performance difference several times.

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

More examples can be found [here]((http://www.boost.org/doc/libs/1_56_0/doc/html/atomic/usage_examples.html) in boost.atomic. The official description of atomic can be found [here](http://en.cppreference.com/w/cpp/atomic/atomic).

# wait-free & lock-free

Atomic instructions can provide us two important properties: [wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)和[lock-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom). Wait-free means no matter how os(operating system) schedules threads, every thread is doing useful work; lock-free is weaker than wait-free, which means no matter how os schedules threads, at least one thread is doing useful work. If locks are used in the program, then os may schedule out the thread holding the lock in which case all threads trying to hold the same lock is waiting. So Using locks are not lock-free and wait-free. To make sure one task is done always within a determined time, the critical path in real-time os is at least lock-free. In our extensive and diverse online service, the property of real-time is demanded eagerly. If the most critical path in brpc satisfies wait-free of lock-free, we can provide a more stable quality of service. For example, since [fd](https://en.wikipedia.org/wiki/File_descriptor) is only suitable for being manipulated by a single thread, atomic instructions are used in brpc to maximize the concurrency of read and write of fd which is discussed more deeply in [here](io.md).

please notice that it is common to think that algorithms using wait-free or lock-free could be faster, but the truth may be the opposite, because:

- Race condition and ABA problem must be handled in lock-free and wait-free algorithms, which means the code may be more complex than that using locks when doing the same task.
- Using mutex has an effect of backoff. Backoff means that when contention happens, it will find another way to avoid fierce contention. The thread getting an locked mutex will be put into sleep state to avoid cacheline synchronization, making the thread holding that mutex can complete the task quickly, which may increase the overall throughput.

The low performance caused by mutex is because either the critical area is too big(limits the concurrency), or the critical area is too small(the overhead of context switch becomes prominent, adaptive mutex should be considered to use). The value of lock-free/wait-free is that it guarantees one thread or all threads always doing useful work, not for absolute high performance. But algorithms using lock-free/wait-free may probably have better performance in the situation where algorithm itself can be implemented using a few atomic instructions. Atomic instructions are also used to implement mutex, so if the algorithm can be done just using one or two atomic instructions, it could be faster than that using mutex. 
