brpc提供了[异步接口](client.md#异步访问)，所以一个常见的问题是：我应该用异步接口还是bthread？

短回答：延时不高时你应该先用简单易懂的同步接口，不行的话用异步接口，只有在需要多核并行计算时才用bthread。

# 同步或异步

异步即用回调代替阻塞，有阻塞的地方就有回调。虽然在javascript这种语言中回调工作的很好，接受度也非常高，但只要用过，就会发现这和我们需要的回调是两码事，这个区别不是[lambda](https://en.wikipedia.org/wiki/Anonymous_function)，也不是[future](https://en.wikipedia.org/wiki/Futures_and_promises)，而是javascript是单线程的。javascript的回调放到多线程下可能没有一个能跑过，竞争太多，单线程的同步方法和多线程的同步方法是完全不同的。那是不是服务能搞成类似的形式呢？多个线程，每个都是独立的eventloop。可以，ub**a**server就是（注意带a)，但实际效果糟糕，因为阻塞改回调可不简单，当阻塞发生在循环，条件分支，深层子函数中时，改造特别困难，况且很多老代码、第三方代码根本不可能去改造。结果是代码中会出现不可避免的阻塞，导致那个线程中其他回调都被延迟，流量超时，server性能不符合预期。如果你说，”我想把现在的同步代码改造为大量的回调，除了我其他人都看不太懂，并且性能可能更差了”，我猜大部分人不会同意。别被那些鼓吹异步的人迷惑了，他们写的是从头到尾从下到上全异步且不考虑多线程的代码，和你要写的完全是两码事。

brpc中的异步和单线程的异步是完全不同的，异步回调会运行在与调用处不同的线程中，你会获得多核扩展性，但代价是你得意识到多线程问题。你可以在回调中阻塞，只要线程够用，对server整体的性能并不会有什么影响。不过异步代码还是很难写的，所以我们提供了[组合访问](combo_channel.md)来简化问题，通过组合不同的channel，你可以声明式地执行复杂的访问，而不用太关心其中的细节。

当然，延时不长，qps不高时，我们更建议使用同步接口，这也是创建bthread的动机：维持同步代码也能提升交互性能。

**判断使用同步或异步**：计算qps * latency(in seconds)，如果和cpu核数是同一数量级，就用同步，否则用异步。

比如：

- qps = 2000，latency = 10ms，计算结果 = 2000 * 0.01s = 20。和常见的32核在同一个数量级，用同步。
- qps = 100, latency = 5s, 计算结果 = 100 * 5s = 500。和核数不在同一个数量级，用异步。
- qps = 500, latency = 100ms，计算结果 = 500 * 0.1s = 50。基本在同一个数量级，可用同步。如果未来延时继续增长，考虑异步。

这个公式计算的是同时进行的平均请求数（你可以尝试证明一下），和线程数，cpu核数是可比的。当这个值远大于cpu核数时，说明大部分操作并不耗费cpu，而是让大量线程阻塞着，使用异步可以明显节省线程资源（栈占用的内存）。当这个值小于或和cpu核数差不多时，异步能节省的线程资源就很有限了，这时候简单易懂的同步代码更重要。

# 异步或bthread

有了bthread这个工具，用户甚至可以自己实现异步。以“半同步”为例，在brpc中用户有多种选择：

- 发起多个异步RPC后挨个Join，这个函数会阻塞直到RPC结束。（这儿是为了和bthread对比，实现中我们建议你使用[ParallelChannel](combo_channel.md#parallelchannel)，而不是自己Join）
- 启动多个bthread各自执行同步RPC后挨个join bthreads。

哪种效率更高呢？显然是前者。后者不仅要付出创建bthread的代价，在RPC过程中bthread还被阻塞着，不能用于其他用途。

**如果仅仅是为了并发RPC，别用bthread。**

不过当你需要并行计算时，问题就不同了。使用bthread可以简单地构建树形的并行计算，充分利用多核资源。比如检索过程中有三个环节可以并行处理，你可以建立两个bthread运行两个环节，在原地运行剩下的环节，最后join那两个bthread。过程大致如下：
```c++
bool search() {
  ...
  bthread th1, th2;
  if (bthread_start_background(&th1, NULL, part1, part1_args) != 0) {
    LOG(ERROR) << "Fail to create bthread for part1";
    return false;
  }
  if (bthread_start_background(&th2, NULL, part2, part2_args) != 0) {
    LOG(ERROR) << "Fail to create bthread for part2";
    return false;
  }
  part3(part3_args);
  bthread_join(th1);
  bthread_join(th2);
  return true;
}
```
这么实现的point：
- 你当然可以建立三个bthread分别执行三个部分，最后join它们，但相比这个方法要多耗费一个线程资源。
- bthread从建立到执行是有延时的（调度延时），在不是很忙的机器上，这个延时的中位数在3微秒左右，90%在10微秒内，99.99%在30微秒内。这说明两点：
  - 计算时间超过1ms时收益比较明显。如果计算非常简单，几微秒就结束了，用bthread是没有意义的。
  - 尽量让原地运行的部分最慢，那样bthread中的部分即使被延迟了几微秒，最后可能还是会先结束，而消除掉延迟的影响。并且join一个已结束的bthread时会立刻返回，不会有上下文切换开销。

另外当你有类似线程池的需求时，像执行一类job的线程池时，也可以用bthread代替。如果对job的执行顺序有要求，你可以使用基于bthread的[ExecutionQueue](execution_queue.md)。
