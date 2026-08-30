[中文版](../cn/bthread.md)

# bthread

[bthread](https://github.com/apache/brpc/tree/master/src/bthread) is the M:N threading library used by brpc. The goal is to raise concurrency, keep coding simple, and get better scalability and cache locality on CPUs with more and more cores. "M:N" means M bthreads are mapped onto N pthreads, and M is usually much larger than N. Because Linux pthread ([NPTL](http://en.wikipedia.org/wiki/Native_POSIX_Thread_Library)) is 1:1, those M bthreads are also mapped onto N [LWPs](http://en.wikipedia.org/wiki/Light-weight_process). bthread grew out of the fiber in Distributed Process (DP), an N:1 cooperative threading library. That model is equivalent to an event-loop library, except that users write synchronous code.

# Goals

- Users keep the synchronous programming style, can create a bthread in a few hundred nanoseconds, and can synchronize with a variety of primitives.
- Every bthread API is callable from a pthread and has reasonable behavior. Code that uses bthread APIs can run correctly inside a pthread.
- Make full use of multiple cores.
- Better cache locality; NUMA support is a plus.

# Non-goals

- Provide a pthread-compatible ABI that works just by linking. **Rejected because**: bthread has no priorities and is not suitable for every workload. Silent replacement by linking would make users pick up bthread without knowing it, and cause bugs.
- Intercept every possibly blocking glibc function and syscall so that they block the bthread instead of the system thread. **Rejected because**:
  - Blocking a bthread may switch the underlying system thread, so functions that depend on system TLS have undefined behavior.
  - Mixing them with functions that block pthreads can deadlock.
  - These hooks are usually slower, because they often need extra syscalls such as epoll. The same coverage is more useful for an N:1 cooperative library (fiber): the hook itself is slower, but without it the whole system thread blocks and every fiber stalls.
- Patch the kernel so that pthread can switch quickly on the same core. **Rejected because**: with a large number of pthreads, per-thread resources are diluted and thread-local caches (for example tcmalloc) work poorly. A separate bthread library does not have this problem, because it still maps onto a small number of pthreads. A large part of bthread's speedup over pthread comes from concentrating thread resources. Portability also matters: bthread prefers pure userland code.

# FAQ

##### Q: Is bthread a coroutine?

No. "Coroutine" here means an N:1 threading library: all coroutines run in one system thread. Compute power is equivalent to an event-loop library. Because they never leave that thread, switches need no syscall (about 100ns–200ns) and cache-coherence cost is small. The cost is that coroutines cannot use multiple cores well, and the code must be non-blocking, otherwise every coroutine stalls. That makes them a good fit for IO servers whose run time is deterministic, such as an HTTP server. Carefully tuned, they can reach very high throughput. Most online services at Baidu do not have deterministic run time, and a search is often built by dozens of people. One slow function stalls every coroutine. Event loops have the same problem: one blocking callback stalls the whole loop. ub**a**server (note the **a**, not ubserver) was Baidu's attempt at an async framework of several parallel event loops. In practice it behaved poorly: a slightly slow log in a callback, a hiccup talking to Redis, or a bit more compute caused waiting requests to time out in bulk. The framework never caught on.

bthread is an M:N threading library. One stalled bthread does not stall the others. The two key techniques are work-stealing scheduling and butex. The former schedules bthreads onto more cores quickly; the latter lets bthreads and pthreads wait for and wake each other. Neither is needed by a coroutine. See [threading overview](threading_overview.md) for more on threading models.

##### Q: Should I create lots of bthreads in my program?

No. Unless you need to [run some code concurrently inside one RPC](bthread_or_not.md), do not call bthread APIs directly. Leave that to brpc.

##### Q: How do bthreads map onto pthread workers?

A pthread worker runs exactly one bthread at a time. When the current bthread suspends, the worker first tries to pop a ready bthread from its local runqueue. If that is empty, it steals a ready bthread from a random other worker. If that also fails, it sleeps and is woken when a new ready bthread appears.

##### Q: Can a bthread call blocking pthread or system functions?

Yes. That only blocks the current pthread worker. Other pthread workers are unaffected.

##### Q: Does one blocked bthread affect other bthreads?

No. If the bthread blocks on a bthread API, it yields the current pthread worker to other bthreads. If it blocks on a pthread API or a system function, ready bthreads on that worker are stolen by idle pthread workers.

##### Q: Can pthread code call bthread APIs?

Yes. A bthread API called from a bthread affects the current bthread; called from a pthread, it affects the current pthread. Code that uses bthread APIs can run directly in a pthread.

##### Q: If many bthreads call blocking pthread or system functions, does that hurt RPC?

Yes. For example, with 8 pthread workers, if 8 bthreads all call `usleep()`, RPC code that handles network IO cannot run for a while. As long as the block is not too long, this usually **does not matter much**: the workers are all busy, and queuing is about the only option left.
In brpc you can raise the worker count to mitigate this: on the server set [ServerOptions.num_threads](server.md#number-of-worker-pthreads) or [-bthread_concurrency](flags.md); on the client set [-bthread_concurrency](flags.md).

Is there a way to avoid this completely?

- Dynamically adding workers is the obvious idea, but it often fails in practice. When many workers block at once, they are often waiting for the same resource (for example the same lock). Adding workers may only add more waiters.
- Split IO threads and worker threads? IO threads would only send and receive, so a fully blocked worker pool would not stall IO. An extra hop does not relieve congestion. If every worker is stuck, the program is still stuck; the stall just moves from the socket buffer to the queue between IO threads and workers. In other words, IO threads that keep running while workers are stuck may be doing useless work. That is what **does not matter much** above really means. Another cost is that every request jumps from an IO thread to a worker, adding a context switch. On a busy machine that switch is sometimes not scheduled promptly, which lengthens the tail latency.
- A practical fix is to [limit max concurrency](server.md#limit-concurrency). If the number of in-flight requests stays below the worker count, "all workers blocked" does not happen.
- Another fix: when blocked workers pass a threshold (for example 6 of 8), stop running user code in place and throw it into a separate thread pool. Even if all user code blocks, a few workers remain to handle RPC IO. bthread mode does not have this mechanism today, but a similar one is implemented when [pthread mode](server.md#pthread-mode) is on. Is that "useless work" while user code is fully blocked, as above? Possibly. The mechanism is more about avoiding a rare deadlock: all user code holds a pthread mutex that must be unlocked in an RPC callback; if every worker is blocked, nothing can run that callback and the process deadlocks. Most RPC implementations have this latent issue, but it is rare in practice. Do not issue RPCs while holding a lock, and you can avoid it.

##### Q: Will bthread have [Channel](https://gobyexample.com/channels)?

No. A channel models a relationship between two points, while many real problems are many-to-many. The natural channel solution is then: one role owns a thing or a resource, and every other thread sends commands to that role over a channel. Give the program N roles, each doing its job, and the program runs in an orderly way. So using channels implies splitting the program into roles. Channels are intuitive, but they cost extra context switches. Nothing finishes until the callee is scheduled, processes the message, and replies. No amount of cache-locality tuning removes that cost. Another reality: channel-heavy code is hard to write. Business consistency often binds resources together, so one role wears several hats, cannot do two things at once, and work has priorities. Interrupts, early exits, and resumes make the final code very complex.

What we usually need is a buffered channel that acts as a queue with ordered execution. bthread provides [ExecutionQueue](execution_queue.md) for that.
