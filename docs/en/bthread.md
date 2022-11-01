[bthread](https://github.com/brpc/brpc/tree/master/src/bthread) is the M:N thread library used by brpc. The purpose is to improve the concurrency of the program while reducing the coding difficulty, and Provides better scalability and cache locality on CPUs with an increasing number of cores. "M:N" means that M bthreads are mapped to N pthreads, and M is generally much larger than N. Since the current implementation of pthread in Linux ([NPTL](http://en.wikipedia.org/wiki/Native_POSIX_Thread_Library)) is 1:1, M bthreads are also equivalent to mapping to N [LWP](http:// en.wikipedia.org/wiki/Light-weight_process). The predecessor of bthread is the fiber in Distributed Process (DP), an N:1 cooperative thread library, which is equivalent to the event-loop library, but it writes synchronous code.

# Goals

-Users can continue the synchronous programming mode, can build bthreads in hundreds of nanoseconds, and can use multiple primitives to synchronize.
-All interfaces of bthread can be called in pthread and have reasonable behavior. The code using bthread can be executed normally in pthread.
-Can make full use of multi-core.
-better cache locality, supporting NUMA is a plus.

# NonGoals

-Provide a compatible interface of pthread, which can be used only by linking. **Rejection reasons**: bthread has no priority and is not suitable for all scenarios. The method of linking may easily cause users to misuse bthread without their knowledge and cause bugs.
-Cover all kinds of glibc functions and system calls that may block, and change the function that originally blocked the system thread to block bthread. **Rejection reasons**:
-bthread blocking may switch system threads, and the behavior of functions that rely on system TLS is undefined.
-It may deadlock when mixed with functions that block pthreads.
-The efficiency of this kind of hook function itself is generally worse, because it often requires additional system calls, such as epoll. But this kind of coverage has a certain meaning for the N:1 cooperative thread library (fiber): although the function itself is slow, it will be slower if it is not covered (system thread blocking will cause all fibers to block).
-Modify the kernel so that pthread supports fast switching with the same core. **Rejection reason**: After having a large number of pthreads, the resource requirements of each thread are diluted, and the effect of the code based on thread-local cache will be poor, such as tcmalloc. The independent bthread does not have this problem, because it is eventually mapped to a small number of pthreads. A large part of bthread's performance improvement over pthread comes from more concentrated thread resources. Another consideration is portability, bthread prefers pure user mode code.

# FAQ

##### Q: Is bthread a coroutine?

no. The coroutine we often refer to specifically refers to the N:1 thread library, that is, all coroutines run in a system thread, and the computing power is equivalent to various eventloop libraries. Since it is not cross-threaded, the switch between coroutines does not require system calls, which can be very fast (100ns-200ns), and it is less affected by cache consistency. But the price is that coroutines cannot efficiently use multiple cores, and the code must be non-blocking, otherwise all coroutines will be stuck, which is demanding for developers. This feature of the coroutine makes it suitable for writing IO servers with a certain running time, such as http server, which can achieve very high throughput in some carefully debugged scenarios. However, the running time of most online services in Baidu is uncertain, and many searches are completed by dozens of people, and a slow function will jam all the coroutines. At this point, the eventloop is similar. If a callback is stuck, the entire loop is stuck. For example, ub**a**server (note the a, not ubserver) is Baidu’s attempt to an asynchronous framework, consisting of multiple parallel eventloops. The composition, the real performance is bad: the log recording in the callback is slower, the redis access is stuck, the calculation is a bit heavier, and other waiting requests will be a lot of timeouts. So this framework has never become popular.

bthread is an M:N thread library, a bthread being stuck will not affect other bthreads. There are two key technologies: work stealing scheduling and butex. The former allows bthread to be scheduled to more cores faster, and the latter allows bthread and pthread to wait and wake up each other. Neither of these two coroutines is needed. For more threading knowledge, please view [here](threading_overview.md).

##### Q: Should I use bthread more in my program?

Shouldn't. Unless you need to [make some code run concurrently](bthread_or_not.md) during an RPC process, you should not directly call the bthread function, and leave these to brpc to do better.

##### Q: How do bthread and pthread worker correspond?

The pthread worker can only run one bthread at any time. When the current bthread is suspended, the pthread worker first tries to pop a bthread to be run from the local runqueue. If not, it randomly steals the bthread of another worker to be run, and it sleeps if it is still not. Will be awakened when there is a new bthread to be run.

##### Q: Can blocked pthreads or system functions be called in bthread?

Yes, only the current pthread worker is blocked. Other pthread workers are not affected.

##### Q: Will blocking of one bthread affect other bthreads?

Does not affect. If bthread is blocked by the bthread API, it will give up the current pthread worker to other bthreads. If the bthread is blocked by the pthread API or system function, the bthread to be run on the current pthread worker will be stolen and run by other idle pthread workers.

##### Q: Can the bthread API be called in pthread?

Can. When the bthread API is called in bthread, it affects the current bthread, and when it is called in pthread, it affects the current pthread. Code using bthread API can be directly run in pthread.

##### Q: If a large number of bthreads call blocked pthreads or system functions, will it affect the operation of RPC?

meeting. For example, if there are 8 pthread workers, when all 8 bthreads call the system usleep(), the RPC code that handles network sending and receiving is temporarily unable to run. As long as the blocking time is not too long, this generally has no effect.
In brpc, users can choose to increase the number of workers to alleviate the problem. On the server side, you can set [ServerOptions.num_threads](server.md#worker threads) or [-bthread_concurrency](http://brpc.baidu.com:8765 /flags/bthread_concurrency), [-bthread_concurrency](http://brpc.baidu.com:8765/flags/bthread_concurrency) can be set on the client side.

Is there a way to circumvent it completely?

-An easy way to think of is to dynamically increase the number of workers. However, it may not be what you want. When a large number of workers are blocked at the same time,
They are likely to be waiting for the same resource (such as the same lock), and adding workers may just add more waiters.
-What is the distinction between io thread and worker thread? io thread specializes in sending and receiving, worker thread calls user logic, even if all worker threads are blocked, it will not affect io thread. But adding a layer of processing link (io thread) will not alleviate congestion, if worker All threads are stuck, the program will still be stuck,
It's just that the stuck place is transferred from the socket buffer to the message queue between the io thread and the worker thread. In other words, when the worker is stuck,
What the io thread is still running may be useless. In fact, this is the true meaning of the above-mentioned **no impact**. Another problem is that every request has to jump from the io thread to the worker thread. A context switch is added. When the machine is busy, the switch has a certain probability that it cannot be scheduled in time, which will cause more delays and long tails.
-A practical solution is [Limit Maximum Concurrency](server.md#Limit Maximum Concurrency), as long as the number of requests being processed at the same time is less than the number of workers, the "all workers are blocked" situation can naturally be avoided.
-Another solution is that when the blocked workers exceed the threshold (for example, 6 out of 8), the user code is not called in place, but is thrown into a separate thread pool to run. This way even if the user code is all blocked , It is always possible to reserve several workers to handle rpc sending and receiving. But the current bthread mode does not have this mechanism, but a similar mechanism has been implemented in [Open pthread mode](server.md#pthread mode). That is as mentioned above Yes, is this mechanism doing "useless work" when user code is blocked? It may be. But this mechanism is more to avoid deadlock in some extreme cases, such as all user code is locked in On a pthread mutex, and this mutex needs to be unlocked in a certain RPC callback, if all workers are blocked, then there is no thread to process the RPC callback, and the entire program is deadlocked. Although most RPC implementations are There is this potential problem, but the actual frequency seems to be very low.As long as you develop a good habit of not doing RPC in the lock, this can be completely avoided.

##### Q: Will bthread have [Channel](https://gobyexample.com/channels)?

Will not. Channel represents the relationship between two points, and many practical problems are multiple points. At this time, the most natural solution to using channels is: there is a role responsible for operating a certain thing or a certain resource, and other threads are all reporting to this through the channel. The role calls the shots. If we set N roles in the program and let them perform their duties, then the program can run in an orderly manner. So the subtext of using channel is to divide the program into different roles. Channels are intuitive, but they have a price: additional context switching. To do anything, you have to wait until the called place is scheduled, processed, and replied, before the calling place can continue. No matter how optimized this is, no matter how respect the cache locality is, there is also a significant overhead. Another reality is that the code using channels is not easy to write. Due to the constraints of business consistency, some resources are often tied together, so a role is likely to have multiple roles, but when it does one thing, it cannot do another thing, and things have priorities. The final code of various interruptions, jumps, and continued formation is extremely complicated.

What we need is often a buffered channel, which plays the role of queue and orderly execution. Bthread provides [ExecutionQueue](execution_queue.md) to accomplish this purpose.