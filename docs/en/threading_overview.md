# Common thread model

## A connection corresponds to a thread or process

A thread/process handles all the messages from a fd and quits until the connection is closed. When the number of connections increases, the resources occupied by threads/processes and the cost of context switch will become increasingly large which causes poor performance. It is the source of the [C10K](http://en.wikipedia.org/wiki/C10k_problem) problem. These two methods(using thread or process) are common in early web servers and are rarely used today.

## Single-threaded reactor

The event-loop library such as [libevent](http://libevent.org/)[, ](http://en.wikipedia.org/wiki/Reactor_pattern)[libev](http://software.schmorp.de/pkg/libev.html) is a typical example. Usually a event dispatcher is responsible for waiting different kinds of event and calls event handler in-place after an event happens. After handler is processed, dispatcher waits more events, from where "loop" comes from. Essentially all handler functions are executed in the order of occurrence in a system thread. One event-loop can use only one core, so this kind of program is either IO-bound or has a short and fixed running time(such as http servers). Otherwise one callback will block the whole program and causes high latencies. In practice this kind of program is not suitable for many people involved, because the performance may be significantly degraded if no enough attentions are paid. The extensibility of the event-loop program depends on multiple processes.

The single-threaded reactor works as shown below:

![img](../images/threading_overview_1.png)

## N:1 thread library

Generally, N user threads are mapped into a system thread (LWP), and only one user thread can be run, such as [GNU Pth](http://www.gnu.org/software/pth/pth-manual.html), [StateThreads](http://state-threads.sourceforge.net/index.html). When the blocking function is called, current user thread is scheduled out. It is also known as [Fiber](http://en.wikipedia.org/wiki/Fiber_(computer_science)). N:1 thread library is equal to single-thread reactor. Event callback is replaced by an independent stack and registers, and running callbacks becomes jumping to the corresponding context. Since all user codes run in a system thread, N:1 thread library does not produce complex race conditions, and some scenarios do not need a lock. Because only one core can be used just like event loop library, N:1 thread library cannot give full play to multi-core performance, only suitable for some specific scenarios. But it also to reduce the jump between different cores, coupled with giving up the independent signal mask, context switch can be done quickly(100 ~ 200ns). Generally, the performance of N:1 thread library is as good as event loop and its extensibility also depends on multiple processes.

## Multi-threaded reactr

Kylin, [boost::asio](http://www.boost.org/doc/libs/1_56_0/doc/html/boost_asio.html) is a typical example. Generally event dispatcher is run by one or several threads and schedules event handler to a worker thread after event happens. Since SMP machines are widely used in Baidu, the structure using multiple cores like this is more suitable and the method of exchanging messages between threads is simpler than that between processes, so it often makes multi-core load more uniform. However, due to cache coherence restrictions, the multi-threaded reactor model does not achieve linearity in the scalability of the core. In a particular scenario, the rough multi-threaded reactor running on a 24-core machine is not even faster than a single-threaded reactor with a dedicated implementation. Reactor has a proactor variant, namely using asynchronous IO to replace event dispatcher. Boost::asio is a proactor under [Windows](http://msdn.microsoft.com/en-us/library/aa365198(VS.85).aspx).

The multi-threaded reactor works2 as shown blew:

![img](../images/threading_overview_2.png)

# What else can we improve?

## Extensibility is not good enough

Ideally, it is best for users to write event-driven codes, but in reality because of the difficulty of coding and maintainability, the using way of users is mostly mixed: synchronous IO is often issued in callbacks so that worker thread is blocked and it cannot process other requests. A request often goes through dozens of services, which means that a thread spent a lot of time waiting for responses from downstreams. Users often have to launch hundreds of threads to maintain high throughput, which resulted in high-intensity scheduling overhead. What's more, for simplicity, mutex and condition involved global contention is often used for distributing tasks. When all threads are in a highly contented state, the efficiency is clearly not high. A better approach is to use more task queues and corresponding scheduling algorithms to reduce global contention.

## Asynchronous programming is difficult

The code flow control in asynchronous programming is full of traps even for the experts. Any suspending operation(such as sleeping for a while or waiting for something to finish) implies that users should save states explicitly. Asynchronous code is often written in the form of state machines. When the position of suspension is less, it is a bit cumbersome, but still can be grasped. The problem is that once the suspending occurs in the conditional judgment, the loop or the sub-function, it is almost impossible to write such a state machine and be understood and maintained by many people, while it is quite common in distributed system since a node often wants to initiate operation on multiple other nodes at the same time. In addition, if the recovery can be triggered by a variety of events (such as fd has data or timeout happens), the process of suspend and resume is prone to have race conditions, which requires high ability of multi-threaded programming. Syntax sugar(such as lambda) can make coding less troublesome but can not reduce difficulty.

## RAII(http://en.wikipedia.org/wiki/Resource_Acquisition_Is_Initialization) cannot be used in Asynchronous programming

The more common way is to use a shared pointer, which seems convenient, but also makes the ownership of memory become elusive. If the memory leaked, it is difficult to locate the address not releasing; if segment fault happens, we also do not know the address releasing twice. It is difficult to control the quality of the code that uses the reference count extensively and it is easy to spend time on the memory problem for a long term. If the reference count also requires manual maintenance, keeping the quality of code is even more difficult(such as the code in kylin) and each modification will make the maintainers in a dilemma. No RAII also makes use of synchronization primitives more error-prone, for example, lock_guard cannot be used, locking outside callback and unlocking inside callback, which is prone to error in practice.

## cache bouncing

When the event dispatcher dispatches a task to a worker, the user code has to jump from one core to another and the relevant cpu cache must be synchronized, which is a not very fast operation that takes several microseconds. If the worker can run directly on the core where the event dispatcher is running, since most systems(at this time scale) do not have intensive event flows, the priority of running the existing tasks is higher than getting new events from event dispatcher. Another example is that it is best to wake up the blocking thread that send the request in the same cpu core when response is received.

# M:N thread library

To meet these improvements we expect, one option is the M:N thread library, which maps M user threads into N system threads(LWP). Let us see how above problems are solved in this mode:

- Each system thread often has a separate runqueue. There may be one or more scheduler to distribute user thread to different runqueue and each system thread will run user thread in their own runqueue in a high priority, then do the global scheduling. This is certainly more complicated, but better than the global mutex/condition.
- Although the M:N thread library and the multi-threaded reactor are equivalent, the difficulty of coding in a synchronous way is significantly lower than that in an event-driven way and most people can quickly learn the method of synchronous programming.
- There is no need to split a function into several callbacks, you can use RAII.
- When switching from user thread A to user thread B, perhaps we can let B run on the core on which A is running, and let A run on another core, so that B with higher priority is less affected by cache miss.

Implementing a full-featured M:N thread library is extremely difficult, so M:N thread library is always an active research topic. The M:N thread library we said here is especially for network services. In this context, some of the requirements can be simplified, for example, there are no time slice and priority. Even if there are some kinds of requirement, they are implemented using a simple way and can not be compared with the implementation of operating systems. M:N thread library can be implemented either in the user level or in the kernel level. New languages prefer the implementation in the user level, such as GHC threads and goroutine, which can design new APIs based on thread libraries. The implementation in the mainstream language often have to modify the kernel, such as Windows UMS(https://msdn.microsoft.com/en-us/library/windows/desktop/dd627187(v=vs.85).aspx). Although google SwicthTo is 1:1, M:N can be implemented based on it. M:N thread library is more similar to the system thread in usage, which needs locks or message passing to ensure thread safety of the code.
