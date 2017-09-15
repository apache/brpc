# 常见线程模型

## 一个连接对应一个线程或进程

线程/进程处理来自绑定连接的消息，连接不断开线程/进程就不退。当连接数逐渐增多时，线程/进程占用的资源和上下文切换成本会越来越大，性能很差，这就是[C10K问题](http://en.wikipedia.org/wiki/C10k_problem)的来源。这两种方法常见于早期的web server，现在很少使用。

## 单线程reactor

以[libevent](http://libevent.org/)[, ](http://en.wikipedia.org/wiki/Reactor_pattern)[libev](http://software.schmorp.de/pkg/libev.html)等event-loop库为典型，一般是由一个event dispatcher等待各类事件，待事件发生后原地调用event handler，全部调用完后等待更多事件，故为"loop"。实质是把多段逻辑按事件触发顺序交织在一个系统线程中。一个event-loop只能使用一个核，故此类程序要么是IO-bound，要么是逻辑有确定的较短的运行时间（比如http server)，否则一个回调卡住就会卡住整个程序，容易产生高延时，在实践中这类程序非常不适合多人参与，一不注意整个程序就显著变慢了。event-loop程序的扩展性主要靠多进程。

单线程reactor的运行方式如下图所示：

![img](../images/threading_overview_1.png)

## N:1线程库

以[GNU Pth](http://www.gnu.org/software/pth/pth-manual.html), [StateThreads](http://state-threads.sourceforge.net/index.html)等为典型，一般是把N个用户线程映射入一个系统线程(LWP)，同时只能运行一个用户线程，调用阻塞函数时才会放弃时间片，又称为[Fiber](http://en.wikipedia.org/wiki/Fiber_(computer_science))。N:1线程库与单线程reactor等价，只是事件回调被替换为了独立的栈和寄存器状态，运行回调变成了跳转至对应的上下文。由于所有的逻辑运行在一个系统线程中，N:1线程库不太会产生复杂的race condition，一些编码场景不需要锁。和event loop库一样，由于只能利用一个核，N:1线程库无法充分发挥多核性能，只适合一些特定的程序。不过这也使其减少了多核间的跳转，加上对独立signal mask的舍弃，上下文切换可以做的很快（100~200ns），N:1线程库的性能一般和event loop库差不多，扩展性也主要靠多进程。

## 多线程reactor

以kylin, [boost::asio](http://www.boost.org/doc/libs/1_56_0/doc/html/boost_asio.html)为典型。一般由一个或多个线程分别运行event dispatcher，待事件发生后把event handler交给一个worker thread执行。由于百度内以SMP机器为主，这种可以利用多核的结构更加合适，多线程交换信息的方式也比多进程更多更简单，所以往往能让多核的负载更加均匀。不过由于cache一致性的限制，多线程reactor模型并不能获得线性于核数的扩展性，在特定的场景中，粗糙的多线程reactor实现跑在24核上甚至没有精致的单线程reactor实现跑在1个核上快。reactor有proactor变种，即用异步IO代替event dispatcher，boost::asio[在windows下](http://msdn.microsoft.com/en-us/library/aa365198(VS.85).aspx)就是proactor。

多线程reactor的运行方式如下：

![img](../images/threading_overview_2.png)

# 那我们还能改进什么呢？

## 扩展性并不好

理论上用户把逻辑都写成事件驱动是最好的，但实际上由于编码难度和可维护性的问题，用户的使用方式大都是混合的：回调中往往会发起同步操作，从而阻塞住worker线程使其无法去处理其他请求。一个请求往往要经过几十个服务，这意味着线程把大量时间花在了等待下游请求上。用户往往得开几百个线程以维持足够的吞吐，这造成了高强度的调度开销。另外为了简单，任务的分发大都是使用全局竞争的mutex + condition。当所有线程都在争抢时，效率显然好不到哪去。更好的办法是使用更多的任务队列和相应的的调度算法以减少全局竞争。

## 异步编程是困难的

异步编程中的流程控制对于专家也充满了陷阱。任何挂起操作（sleep一会儿，等待某事完成etc）都意味着用户需要显式地保存状态，并在回调函数中恢复状态。异步代码往往得写成状态机的形式。当挂起的位置较少时，这有点麻烦，但还是可把握的。问题在于一旦挂起发生在条件判断、循环、子函数中，写出这样的状态机并能被很多人理解和维护，几乎是不可能的，而这在分布式系统中又很常见，因为一个节点往往要对多个其他节点同时发起操作。另外如果恢复可由多种事件触发（比如fd有数据或超时了），挂起和恢复的过程容易出现race condition，对多线程编码能力要求很高。语法糖(比如lambda)可以让编码不那么“麻烦”，但无法降低难度。

## 异步编程不能使用[RAII](http://en.wikipedia.org/wiki/Resource_Acquisition_Is_Initialization) 

更常见的方法是使用共享指针，这看似方便，但也使内存的ownership变得难以捉摸，如果内存泄漏了，很难定位哪里没有释放；如果segment fault了，也不知道哪里多释放了一下。大量使用引用计数的用户代码很难控制代码质量，容易长期在内存问题上耗费时间。如果引用计数还需要手动维护，保持质量就更难了（kylin就是这样），每次修改都会让维护者两难。没有RAII模式也使得使用同步原语更易出错，比如不能使用lock_guard；比如在callback之外lock，callback之内unlock；在实践中都很容易出错。

## cache bouncing

当event dispatcher把任务递给worker时，用户逻辑不得不从一个核跳到另一个核，相关的cpu cache必须同步过来，这是微秒级的操作，并不很快。如果worker能直接在event dispatcher所在的核上运行就更好了，因为大部分系统（在这个时间尺度下）并没有密集的事件流，尽快运行已有的任务的优先级高于event dispatcher获取新事件。另一个例子是收到response后最好在当前cpu core唤醒发起request的阻塞线程。

# M:N线程库

要满足我们期望的这些改善，一个选择是M:N线程库，即把M个用户线程映射入N个系统线程(LWP)。我们看看上面的问题在这个模型中是如何解决的：

- 每个系统线程往往有独立的runqueue，可能有一个或多个scheduler把用户线程分发到不同的runqueue，每个系统线程会优先运行自己runqueue中的用户线程，然后再做全局调度。这当然更复杂，但比全局mutex + condition有更好的扩展性。
- 虽然M:N线程库和多线程reactor是等价的，但同步的编码难度显著地低于事件驱动，大部分人都能很快掌握同步操作。
- 不用把一个函数拆成若干个回调，可以使用RAII。
- 从用户线程A切换为用户线程B时，也许我们可以让B在A所在的核上运行，而让A去其他核运行，从而使更高优先级的B更少受到cache miss的干扰。

实现全功能的M:N线程库是极其困难的，所以M:N线程库一直是个活跃的研究话题。我们这里说的M:N线程库特别针对编写网络服务，在这一前提下一些需求可以简化，比如没有时间片抢占，没有优先级等，即使有也以简单粗暴为主，无法和操作系统级别的实现相比。M:N线程库可以在用户态也可以在内核中实现，用户态的实现以新语言为主，比如GHC threads和goroutine，这些语言可以围绕线程库设计全新的API。而在主流语言中的实现往往得修改内核，比如[Windows UMS](https://msdn.microsoft.com/en-us/library/windows/desktop/dd627187(v=vs.85).aspx)。google SwicthTo虽然是1:1，但基于它可以实现M:N的效果。在使用上M:N线程库更类似于系统线程，需要用锁或消息传递保证代码的线程安全。
