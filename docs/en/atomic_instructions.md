We all know that locks are needed in multi-thread programming to avoid potential [race condition](http://en.wikipedia.org/wiki/Race_condition) when modifying the same data. But In practice, it is difficult to write correct codes using atomic instructions. It is hard to understand race condition, [ABA problem]((https://en.wikipedia.org/wiki/ABA_problem), [memory fence](https://en.wikipedia.org/wiki/Memory_barrier). This artical is to help you get started by introducing atomic instructions under [SMP](http://en.wikipedia.org/wiki/Symmetric_multiprocessing). [Atomic instructions](http://en.cppreference.com/w/cpp/atomic/atomic) are formally introduced in C++11.

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

An atomic instruction is relatively fast when there is not contention or only one thread accessing it. Contention happens when there are multiple threads accessing the same [cacheline](https://en.wikipedia.org/wiki/CPU_cache#Cache_entries). Modern CPU extensively use cache and divide cache into multi-level to get high performance at a low price. The widely used cpu in Baidu which is Intel E5-2620  has 32K L1 dcache and icache, 256K L2 cache and 15M L3 cache. L1 and L2 cache is owned by each core, while L3 cache is shared by all cores. Althouth it is fast for one core to write data into its own L1 cache(4 cycles, 2ns), the data in L1 cache should be also seen by another core when it needs writing or reading from corresponding address. To application, this process is atomic and no instructions can be interleaved. Application must wait for the completion of [cache coherence](https://en.wikipedia.org/wiki/Cache_coherence), which takes longer time compared to other operations. It involves a complicated algorithm which takes approximately 700ns in E5-2620 when highly contented. So it is slow to access the memory shared by multiple threads.

In order to improve performance, we need to avoid synchronizing cacheline in CPU. This is not only related to the performance of the atomic instruction itself, but also affect the overall performance of the program. For example, the effect of using spinlock is still poor in some small critical area scenarios. The problem is that the instruction of exchange, fetch_add and other instructions used to implement spinlock must be executed after the latest cacheline has been synchronized. Although it involves only a few instructions, it is not surprising that these instructions spend a few microseconds.
