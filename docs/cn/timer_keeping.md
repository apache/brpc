在几点几分做某件事是RPC框架的基本需求，这件事比看上去难。

让我们先来看看系统提供了些什么： posix系统能以[signal方式](http://man7.org/linux/man-pages/man2/timer_create.2.html)告知timer触发，不过signal逼迫我们使用全局变量，写[async-signal-safe](https://docs.oracle.com/cd/E19455-01/806-5257/gen-26/index.html)的函数，在面向用户的编程框架中，我们应当尽力避免使用signal。linux自2.6.27后能以[fd方式](http://man7.org/linux/man-pages/man2/timerfd_create.2.html)通知timer触发，这个fd可以放到epoll中和传输数据的fd统一管理。唯一问题是：这是个系统调用，且我们不清楚它在多线程下的表现。

为什么这么关注timer的开销?让我们先来看一下RPC场景下一般是怎么使用timer的：

- 在发起RPC过程中设定一个timer，在超时时间后取消还在等待中的RPC。几乎所有的RPC调用都有超时限制，都会设置这个timer。
- RPC结束前删除timer。大部分RPC都由正常返回的response导致结束，timer很少触发。

你注意到了么，在RPC中timer更像是”保险机制”，在大部分情况下都不会发挥作用，自然地我们希望它的开销越小越好。一个几乎不触发的功能需要两次系统调用似乎并不理想。那在应用框架中一般是如何实现timer的呢？谈论这个问题需要区分“单线程”和“多线程”:

- 在单线程框架中，比如以[libevent](http://libevent.org/)[, ](http://en.wikipedia.org/wiki/Reactor_pattern)[libev](http://software.schmorp.de/pkg/libev.html)为代表的eventloop类库，或以[GNU Pth](http://www.gnu.org/software/pth/pth-manual.html), [StateThreads](http://state-threads.sourceforge.net/index.html)为代表的coroutine / fiber类库中，一般是以[小顶堆](https://en.wikipedia.org/wiki/Heap_(data_structure))记录触发时间。[epoll_wait](http://man7.org/linux/man-pages/man2/epoll_wait.2.html)前以堆顶的时间计算出参数timeout的值，如果在该时间内没有其他事件，epoll_wait也会醒来，从堆中弹出已超时的元素，调用相应的回调函数。整个框架周而复始地这么运转，timer的建立，等待，删除都发生在一个线程中。只要所有的回调都是非阻塞的，且逻辑不复杂，这套机制就能提供基本准确的timer。不过就像[Threading Overview](threading_overview.md)中说的那样，这不是RPC的场景。
- 在多线程框架中，任何线程都可能被用户逻辑阻塞较长的时间，我们需要独立的线程实现timer，这种线程我们叫它TimerThread。一个非常自然的做法，就是使用用锁保护的小顶堆。当一个线程需要创建timer时，它先获得锁，然后把对应的时间插入堆，如果插入的元素成为了最早的，唤醒TimerThread。TimerThread中的逻辑和单线程类似，就是等着堆顶的元素超时，如果在等待过程中有更早的时间插入了，自己会被插入线程唤醒，而不会睡过头。这个方法的问题在于每个timer都需要竞争一把全局锁，操作一个全局小顶堆，就像在其他文章中反复谈到的那样，这会触发cache bouncing。同样数量的timer操作比单线程下的慢10倍是非常正常的，尴尬的是这些timer基本不触发。

我们重点谈怎么解决多线程下的问题。

一个惯例思路是把timer的需求散列到多个TimerThread，但这对TimerThread效果不好。注意我们上面提及到了那个“制约因素”：一旦插入的元素是最早的，要唤醒TimerThread。假设TimerThread足够多，以至于每个timer都散列到独立的TimerThread，那么每次它都要唤醒那个TimerThread。 “唤醒”意味着触发linux的调度函数，触发上下文切换。在非常流畅的系统中，这个开销大约是3-5微秒，这可比抢锁和同步cache还慢。这个因素是提高TimerThread扩展性的一个难点。多个TimerThread减少了对单个小顶堆的竞争压力，但同时也引入了更多唤醒。

另一个难点是删除。一般用id指代一个Timer。通过这个id删除Timer有两种方式：1.抢锁，通过一个map查到对应timer在小顶堆中的位置，定点删除，这个map要和堆同步维护。2.通过id找到Timer的内存结构，做个标记，留待TimerThread自行发现和删除。第一种方法让插入逻辑更复杂了，删除也要抢锁，线程竞争更激烈。第二种方法在小顶堆内留了一大堆已删除的元素，让堆明显变大，插入和删除都变慢。

第三个难点是TimerThread不应该经常醒。一个极端是TimerThread永远醒着或以较高频率醒过来（比如每1ms醒一次），这样插入timer的线程就不用负责唤醒了，然后我们把插入请求散列到多个堆降低竞争，问题看似解决了。但事实上这个方案提供的timer精度较差，一般高于2ms。你得想这个TimerThread怎么写逻辑，它是没法按堆顶元素的时间等待的，由于插入线程不唤醒，一旦有更早的元素插入，TimerThread就会睡过头。它唯一能做的是睡眠固定的时间，但这和现代OS scheduler的假设冲突：频繁sleep的线程的优先级最低。在linux下的结果就是，即使只sleep很短的时间，最终醒过来也可能超过2ms，因为在OS看来，这个线程不重要。一个高精度的TimerThread有唤醒机制，而不是定期醒。

另外，更并发的数据结构也难以奏效，感兴趣的同学可以去搜索"concurrent priority queue"或"concurrent skip list"，这些数据结构一般假设插入的数值较为散开，所以可以同时修改结构内的不同部分。但这在RPC场景中也不成立，相互竞争的线程设定的时间往往聚集在同一个区域，因为程序的超时大都是一个值，加上当前时间后都差不多。

这些因素让TimerThread的设计相当棘手。由于大部分用户的qps较低，不足以明显暴露这个扩展性问题，在r31791前我们一直沿用“用一把锁保护的TimerThread”。TimerThread是brpc在默认配置下唯一的高频竞争点，这个问题是我们一直清楚的技术债。随着brpc在高qps系统中应用越来越多，是时候解决这个问题了。r31791后的TimerThread解决了上述三个难点，timer操作几乎对RPC性能没有影响，我们先看下性能差异。

> 在示例程序example/mutli_threaded_echo_c++中，r31791后TimerThread相比老TimerThread在24核E5-2620上（超线程），以50个bthread同步发送时，节省4%cpu（差不多1个核），qps提升10%左右；在400个bthread同步发送时，qps从30万上升到60万。新TimerThread的表现和完全关闭超时时接近。

那新TimerThread是如何做到的？

- 一个TimerThread而不是多个。
- 创建的timer散列到多个Bucket以降低线程间的竞争，默认13个Bucket。
- Bucket内不使用小顶堆管理时间，而是链表 + nearest_run_time字段，当插入的时间早于nearest_run_time时覆盖这个字段，之后去和全局nearest_run_time（和Bucket的nearest_run_time不同）比较，如果也早于这个时间，修改并唤醒TimerThread。链表节点在锁外使用[ResourcePool](memory_management.md)分配。
- 删除时通过id直接定位到timer内存结构，修改一个标志，timer结构总是由TimerThread释放。
- TimerThread被唤醒后首先把全局nearest_run_time设置为几乎无限大(max of int64)，然后取出所有Bucket内的链表，并把Bucket的nearest_run_time设置为几乎无限大(max of int64)。TimerThread把未删除的timer插入小顶堆中维护，这个堆就它一个线程用。在每次运行回调或准备睡眠前都会检查全局nearest_run_time， 如果全局更早，说明有更早的时间加入了，重复这个过程。

这里勾勒了TimerThread的大致工作原理，工程实现中还有不少细节问题，具体请阅读[timer_thread.h](https://github.com/apache/brpc/blob/master/src/bthread/timer_thread.h)和[timer_thread.cpp](https://github.com/apache/brpc/blob/master/src/bthread/timer_thread.cpp)。

这个方法之所以有效：

- Bucket锁内的操作是O(1)的，就是插入一个链表节点，临界区很小。节点本身的内存分配是在锁外的。
- 由于大部分插入的时间是递增的，早于Bucket::nearest_run_time而参与全局竞争的timer很少。
- 参与全局竞争的timer也就是和全局nearest_run_time比一下，临界区很小。
- 和Bucket内类似，极少数Timer会早于全局nearest_run_time并去唤醒TimerThread。唤醒也在全局锁外。
- 删除不参与全局竞争。
- TimerThread自己维护小顶堆，没有任何cache bouncing，效率很高。 
- TimerThread醒来的频率大约是RPC超时的倒数，比如超时=100ms，TimerThread一秒内大约醒10次，已经最优。

至此brpc在默认配置下不再有全局竞争点，在400个线程同时运行时，profiling也显示几乎没有对锁的等待。

下面是一些和linux下时间管理相关的知识：

- epoll_wait的超时精度是毫秒，较差。pthread_cond_timedwait的超时使用timespec，精度到纳秒，一般是60微秒左右的延时。
- 出于性能考虑，TimerThread使用wall-time，而不是单调时间，可能受到系统时间调整的影响。具体来说，如果在测试中把系统时间往前或往后调一个小时，程序行为将完全undefined。未来可能会让用户选择单调时间。
- 在cpu支持nonstop_tsc和constant_tsc的机器上，brpc和bthread会优先使用基于rdtsc的cpuwide_time_us。那两个flag表示rdtsc可作为wall-time使用，不支持的机器上会转而使用较慢的内核时间。我们的机器（Intel Xeon系列）大都有那两个flag。rdtsc作为wall-time使用时是否会受到系统调整时间的影响，未测试不清楚。
