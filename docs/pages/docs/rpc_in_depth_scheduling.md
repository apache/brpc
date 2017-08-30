### 切换方法

bthread通过[boost.context](http://www.boost.org/doc/libs/1_56_0/libs/context/doc/html/index.html)切换上下文。[setjmp](http://en.wikipedia.org/wiki/Setjmp.h), [signalstack](http://linux.die.net/man/2/sigaltstack), [ucontext](http://en.wikipedia.org/wiki/Setcontext)也可以切换上下文，但boost.context是[最快的](http://www.boost.org/doc/libs/1_56_0/libs/context/doc/html/context/performance.html)。

### 基本方法

调度bthread需要比上下文切换更多的东西。每个线程都有自己的[TaskGroup](http://websvn.work.baidu.com/repos/public/show/trunk/bthread/baidu/bthread/task_group.h)，包含独立的[runqueue](http://websvn.work.baidu.com/repos/public/show/trunk/bthread/baidu/bthread/work_stealing_queue.h)，它允许一个线程在一端push/pop，多个线程在另一端steal，这种结构被称作work stealing queue。bthread通过调用TaskGroup::sched或TaskGroup::sched_to放弃CPU，这些函数会先尝试pop自己的runqueue，若没有就偷另一个TaskGroup的runqueue（所有的TaskGroup由[TaskControl](http://websvn.work.baidu.com/repos/public/show/trunk/bthread/baidu/bthread/task_control.h?revision=HEAD)管理），还是没有就调用TaskControl::wait_task()，它在任一runqueue有新bthread时都会被唤醒。获得待运行bthread后TaskGroup会跳至其上下文。

新建bthread的模式和pthread有所不同：可以直接跳至新bthread的上下文，再调度原bthread，这相当于原bthread把自己的时间片让给了新bthread。当新bthread要做的工作比原bthread更紧急时，这可以让新bthread免去可能的调度排队，并保持cache locality。请注意，“调度原bthread"不意味着原bthread必须等到新bthread放弃CPU才能运行，通过调用TaskControl::signal_task()，它很可能在若干微秒后就会被偷至其他pthread worker而继续运行。当然，bthread也允许调度新bthread而让原bthread保持运行。这两种新建方式分别对应bthread_start_urgent()和bthread_start_background()函数。

### 相关工作

标准work stealing调度的过程是每个worker pthread都有独立的runqueue，新task入本地runqueue，worker pthread没事干后先运行本地的task，没有就随机偷另一个worker的task，还是没事就不停地sched_yield... 随机偷... sched_yield... 随机偷... 直到偷到任务。典型代表是intel的[Cilk](http://en.wikipedia.org/wiki/Cilk)，近期改进有[BWS](http://jason.cse.ohio-state.edu/bws/)。实现较简单，但硬伤在于[sched_yield](http://man7.org/linux/man-pages/man2/sched_yield.2.html)：当一个线程调用sched_yield后，OS对何时唤醒它没任何信息，一般便假定这个线程不急着用CPU，把它排到很低的优先级，比如在linux 2.6后的[CFS](http://en.wikipedia.org/wiki/Completely_Fair_Scheduler)中，OS把调用sched_yield的线程视作用光了时间配额，直到所有没有用完的线程运行完毕后才会调度它。由于在线系统中的低延时（及吞吐）来自线程之间的快速唤醒和数据传递，而这种算法中单个task往往无法获得很好的延时，更适合追求总体完成时间的离线科学计算。这种算法的另一个现实困扰是，在cpu profiler结果中总是有大量的sched_yield，不仅影响结果分析，也让用户觉得这类程序简单粗暴，占CPU特别多。

goroutine自1.1后使用的是另一种work stealing调度，大致过程是新goroutine入本地runqueue，worker先运行本地runqueue中的goroutine，如果没有没有就随机偷另一个worker的runqueue，和标准work stealing只偷一个不同，go会偷掉一半。如果也没有，则会睡眠，所以在产生新goroutine时可能要唤醒worker。由于在调用syscall时会临时放弃go-context，goroutine还有一个全局runqueue，大概每个context调度61次会去取一次全局runqueue，以防止starvation。虽然[设计文档](https://docs.google.com/document/d/1TTj4T2JO42uD5ID9e89oa0sLKhJYD0Y_kqxDv3I3XMw/edit#heading=h.mmq8lm48qfcw)中称其为scalable，但很难称之为scalable，最多“相比之前只有一个全局runqueue更scalable了”，其中有诸多全局竞争点，其对待starvation的态度也相当山寨，考虑到go的gc也不给力，基本上你不能期待goroutine的延时有什么保证，很可能会出现10ms甚至更长的延时，完全不能用于在线服务。

根据[这份材料](http://www.erlang.org/euc/08/euc_smp.pdf)，erlang中的lightweight process也有一部分算法也是work stealing，另外每250毫秒对所有scheduler的负载做个统计，然后根据统计结果调整每个scheduler的负载，不过我觉得效果不会太好，因为这时间也实在太长了。另外他们看上去无法避免在还有待运行的任务时，一些scheduler却闲着。不过对于erlange这类本身[比较慢](http://benchmarksgame.alioth.debian.org/u64q/benchmark.php?test=all&lang=hipe&lang2=gpp&data=u64q)的语言，scheduler要求可能也不高，和我们对在线服务的要求不是一个数量级。

### 调度bthread

之所以对调度如此苛刻，在于我们希望**为每个请求建立bthread**。这意味着如下要求：

- 建立bthread必须非常快。但这不属于调度，在“创建和回收bthread”一节中已有阐述。
- 只要有worker pthread闲着并且可获得CPU，待运行的bthread应该在O(1)内开始运行。这看上去严苛，但却不可或缺，对于一个延时几十毫秒的检索，如果明明有worker pthread可用，但却因为调度的原因导致这个检索任务只能等着本线程的其他任务完成，对可用性（4个9）会有直接的影响。
- 延时不应高于内核的调度延时：大约是5微秒。否则又让用户陷入了两难：一些场景要用pthread，一些用bthread。我们希望用户轻松一点。
- 增加worker pthread数量可以线性增加调度能力。
- 建立后还未运行的bthread应尽量减少内存占用。请求很可能会大于worker pthread数，不可避免地在队列中积累，还没运行却需要占用大量资源，比如栈，是没道理的。

bthread的调度算法能满足这些要求。为了说明白这个过程，我们先解释[futex](http://man7.org/linux/man-pages/man2/futex.2.html)是如何工作的。futex有一系列接口，最主要的是两个：

- futex_wait(void* futex, int expected_value);
- futex_wake(void* futex, int num_wakeup);

当*(int*)futex和expected_value相等时，futex_wait会阻塞，**判断相等并阻塞是原子的**。futex_wake则是唤醒阻塞于futex上的线程，最多num_wakeup个。（对齐的）有效地址都可以作为futex。一种配合方式如下：

```c++
int g_futex = 0;  // shared by all threads
 
// Consumer thread
while (1) {
    const int expected_val = g_futex;
    while (there's sth to consume) {
        consume(...);
    }
    futex_wait(&g_futex, expected_val);
}
 
// Producer thread
produce(...);
atomically_add1(g_futex);   /*note*/
futex_wake(&g_futex, 1);
```

由于futex_wait的原子性，在Producer thread中原子地给g_futex加1后，至少有一个Consumer thread要么看到g_futex != expected_val而不阻塞，或从阻塞状态被唤醒，然后去消费掉所有的东西。比如Consumer thread在消费掉所有的东西后到重新futex_wait中这段时间，Producer thread可能产生了更多东西。futex把这个race condition简化成了两种可能：

- 这段时间内Producer thread执行了原子加（note那行）
- 这段时间内Producer thread还没有执行原子加。

第一种情况，Consumer thread中的futex_wait会看到g_futex != expected_val，立刻返回-1 (errno=EWOULDBLOCK)。第二种情况，Consumer会先阻塞，然后被原子加之后的futex_wake唤醒。总之Consumer总会看到这个新东西，而不会漏掉。我们不再说明更多细节，需要提醒的是，这个例子只是为了说明后续算法，如果你对[原子操作](http://wiki.baidu.com/pages/viewpage.action?pageId=36886832)还没有概念，**绝对**不要在工作项目中使用futex。

bthread的基本调度结构是每个worker pthread都有一个runqueue，新建的bthread压入本地runqueue，调度就是告诉一些闲着的worker，“我这有bthread待运行了，赶紧来偷”。如果我们没有提醒其他worker，在本地runqueue中的bthread必须等到本线程挨个执行排在它之前的bthread，直到把它弹出为止。这种只在一个worker pthread中轮换的方式就是fiber或N:1 threading。显而易见，这限制了并行度。为了充分发挥SMP的潜力，我们要唤醒其他worker来偷走bthread一起并行执行。把上面的代码中的consume换成"偷bthread”，produce换成“压入本地runqueue"，就是最简单的bthread调度算法。

TODO:原理

下图中每个线程都在不停地建立（很快结束的）bthread，最多有24个这样的线程（24核机器）。使用三种不同方法的吞吐如下：

- futex（绿色） - 用一个全局futex管理所有闲置的worker pthread，在需要时signal这个futex。这个方法完全不能扩展，在2个线程时已经饱和（120万左右）。
- futex optimized with atomics（红色）- 仍然是一个全局futex，但是用原子变量做了缓冲，可以合并掉连续的futex_wake。这个方法已经可以扩展，最终能达到1300万左右的吞吐。这种方法的延时会高一些。
- new algorithm（蓝色）- 新算法有非常好的扩展性，不仅是斜的，而且斜率还在变大。这是由于新算法在激烈的全局竞争后会只修改thread local变量，所以越来越快，最终可以达到4500万吞吐。由于类似的原因，新算法的延时越忙反而越低，在不忙时和内核调度延时(5us)接近，在很忙时会降到0.2us。

![img](http://wiki.baidu.com/download/attachments/35959040/image2014-12-7%2021%3A37%3A22.png?version=1&modificationDate=1417959443000&api=v2)