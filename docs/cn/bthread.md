[bthread](https://github.com/apache/brpc/tree/master/src/bthread)是brpc使用的M:N线程库，目的是在提高程序的并发度的同时，降低编码难度，并在核数日益增多的CPU上提供更好的scalability和cache locality。”M:N“是指M个bthread会映射至N个pthread，一般M远大于N。由于linux当下的pthread实现([NPTL](http://en.wikipedia.org/wiki/Native_POSIX_Thread_Library))是1:1的，M个bthread也相当于映射至N个[LWP](http://en.wikipedia.org/wiki/Light-weight_process)。bthread的前身是Distributed Process(DP)中的fiber，一个N:1的合作式线程库，等价于event-loop库，但写的是同步代码。

# Goals

- 用户可以延续同步的编程模式，能在数百纳秒内建立bthread，可以用多种原语同步。
- bthread所有接口可在pthread中被调用并有合理的行为，使用bthread的代码可以在pthread中正常执行。
- 能充分利用多核。
- better cache locality, supporting NUMA is a plus.

# NonGoals

- 提供pthread的兼容接口，只需链接即可使用。**拒绝理由**: bthread没有优先级，不适用于所有的场景，链接的方式容易使用户在不知情的情况下误用bthread，造成bug。
- 覆盖各类可能阻塞的glibc函数和系统调用，让原本阻塞系统线程的函数改为阻塞bthread。**拒绝理由**: 
  - bthread阻塞可能切换系统线程，依赖系统TLS的函数的行为未定义。
  - 和阻塞pthread的函数混用时可能死锁。
  - 这类hook函数本身的效率一般更差，因为往往还需要额外的系统调用，如epoll。但这类覆盖对N:1合作式线程库(fiber)有一定意义：虽然函数本身慢了，但若不覆盖会更慢（系统线程阻塞会导致所有fiber阻塞）。
- 修改内核让pthread支持同核快速切换。**拒绝理由**: 拥有大量pthread后，每个线程对资源的需求被稀释了，基于thread-local cache的代码效果会很差，比如tcmalloc。而独立的bthread不会有这个问题，因为它最终还是被映射到了少量的pthread。bthread相比pthread的性能提升很大一部分来自更集中的线程资源。另一个考量是可移植性，bthread更倾向于纯用户态代码。

# FAQ

##### Q：bthread是协程(coroutine)吗？

不是。我们常说的协程特指N:1线程库，即所有的协程运行于一个系统线程中，计算能力和各类eventloop库等价。由于不跨线程，协程之间的切换不需要系统调用，可以非常快(100ns-200ns)，受cache一致性的影响也小。但代价是协程无法高效地利用多核，代码必须非阻塞，否则所有的协程都被卡住，对开发者要求苛刻。协程的这个特点使其适合写运行时间确定的IO服务器，典型如http server，在一些精心调试的场景中，可以达到非常高的吞吐。但百度内大部分在线服务的运行时间并不确定，且很多检索由几十人合作完成，一个缓慢的函数会卡住所有的协程。在这点上eventloop是类似的，一个回调卡住整个loop就卡住了，比如ub**a**server（注意那个a，不是ubserver）是百度对异步框架的尝试，由多个并行的eventloop组成，真实表现糟糕：回调里打日志慢一些，访问redis卡顿，计算重一点，等待中的其他请求就会大量超时。所以这个框架从未流行起来。

bthread是一个M:N线程库，一个bthread被卡住不会影响其他bthread。关键技术两点：work stealing调度和butex，前者让bthread更快地被调度到更多的核心上，后者让bthread和pthread可以相互等待和唤醒。这两点协程都不需要。更多线程的知识查看[这里](threading_overview.md)。

##### Q: 我应该在程序中多使用bthread吗？

不应该。除非你需要在一次RPC过程中[让一些代码并发运行](bthread_or_not.md)，你不应该直接调用bthread函数，把这些留给brpc做更好。

##### Q：bthread和pthread worker如何对应？

pthread worker在任何时间只会运行一个bthread，当前bthread挂起时，pthread worker先尝试从本地runqueue弹出一个待运行的bthread，若没有，则随机偷另一个worker的待运行bthread，仍然没有才睡眠并会在有新的待运行bthread时被唤醒。

##### Q：bthread中能调用阻塞的pthread或系统函数吗？

可以，只阻塞当前pthread worker。其他pthread worker不受影响。

##### Q：一个bthread阻塞会影响其他bthread吗？

不影响。若bthread因bthread API而阻塞，它会把当前pthread worker让给其他bthread。若bthread因pthread API或系统函数而阻塞，当前pthread worker上待运行的bthread会被其他空闲的pthread worker偷过去运行。

##### Q：pthread中可以调用bthread API吗？

可以。bthread API在bthread中被调用时影响的是当前bthread，在pthread中被调用时影响的是当前pthread。使用bthread API的代码可以直接运行在pthread中。

##### Q：若有大量的bthread调用了阻塞的pthread或系统函数，会影响RPC运行么？

会。比如有8个pthread worker，当有8个bthread都调用了系统usleep()后，处理网络收发的RPC代码就暂时无法运行了。只要阻塞时间不太长, 这一般**没什么影响**, 毕竟worker都用完了, 除了排队也没有什么好方法.
在brpc中用户可以选择调大worker数来缓解问题, 在server端可设置[ServerOptions.num_threads](server.md#worker线程数)或[-bthread_concurrency](http://brpc.baidu.com:8765/flags/bthread_concurrency), 在client端可设置[-bthread_concurrency](http://brpc.baidu.com:8765/flags/bthread_concurrency).

那有没有完全规避的方法呢?

- 一个容易想到的方法是动态增加worker数. 但实际未必如意, 当大量的worker同时被阻塞时,
  它们很可能在等待同一个资源(比如同一把锁), 增加worker可能只是增加了更多的等待者. 
- 那区分io线程和worker线程? io线程专门处理收发, worker线程调用用户逻辑, 即使worker线程全部阻塞也不会影响io线程. 但增加一层处理环节(io线程)并不能缓解拥塞, 如果worker线程全部卡住, 程序仍然会卡住,
  只是卡的地方从socket缓冲转移到了io线程和worker线程之间的消息队列. 换句话说, 在worker卡住时,
  还在运行的io线程做的可能是无用功. 事实上, 这正是上面提到的**没什么影响**真正的含义. 另一个问题是每个请求都要从io线程跳转至worker线程, 增加了一次上下文切换, 在机器繁忙时, 切换都有一定概率无法被及时调度, 会导致更多的延时长尾.
- 一个实际的解决方法是[限制最大并发](server.md#限制最大并发), 只要同时被处理的请求数低于worker数, 自然可以规避掉"所有worker被阻塞"的情况.
- 另一个解决方法当被阻塞的worker超过阈值时(比如8个中的6个), 就不在原地调用用户代码了, 而是扔到一个独立的线程池中运行. 这样即使用户代码全部阻塞, 也总能保留几个worker处理rpc的收发. 不过目前bthread模式并没有这个机制, 但类似的机制在[打开pthread模式](server.md#pthread模式)时已经被实现了. 那像上面提到的, 这个机制是不是在用户代码都阻塞时也在做"无用功"呢? 可能是的. 但这个机制更多是为了规避在一些极端情况下的死锁, 比如所有的用户代码都lock在一个pthread mutex上, 并且这个mutex需要在某个RPC回调中unlock, 如果所有的worker都被阻塞, 那么就没有线程来处理RPC回调了, 整个程序就死锁了. 虽然绝大部分的RPC实现都有这个潜在问题, 但实际出现频率似乎很低, 只要养成不在锁内做RPC的好习惯, 这是完全可以规避的. 

##### Q：bthread会有[Channel](https://gobyexample.com/channels)吗？

不会。channel代表的是两点间的关系，而很多现实问题是多点的，这个时候使用channel最自然的解决方案就是：有一个角色负责操作某件事情或某个资源，其他线程都通过channel向这个角色发号施令。如果我们在程序中设置N个角色，让它们各司其职，那么程序就能分类有序地运转下去。所以使用channel的潜台词就是把程序划分为不同的角色。channel固然直观，但是有代价：额外的上下文切换。做成任何事情都得等到被调用处被调度，处理，回复，调用处才能继续。这个再怎么优化，再怎么尊重cache locality，也是有明显开销的。另外一个现实是：用channel的代码也不好写。由于业务一致性的限制，一些资源往往被绑定在一起，所以一个角色很可能身兼数职，但它做一件事情时便无法做另一件事情，而事情又有优先级。各种打断、跳出、继续形成的最终代码异常复杂。

我们需要的往往是buffered channel，扮演的是队列和有序执行的作用，bthread提供了[ExecutionQueue](execution_queue.md)，可以完成这个目的。
