[中文版](../cn/bthread_or_not.md)

# bthread or not

brpc provides an [asynchronous API](client.md#asynchronous-call), so a common question is: should I use the async API or bthread?

Short answer: when latency is not high, start with the simple synchronous API. If that is not enough, use the async API. Use bthread only when you need parallel compute across cores.

# Sync or async

Async means replacing blocking with callbacks: wherever you would block, you get a callback. Callbacks work well in JavaScript and are widely accepted there, but that is a different kind of callback from what server code needs. The difference is not [lambda](https://en.wikipedia.org/wiki/Anonymous_function) or [future](https://en.wikipedia.org/wiki/Futures_and_promises); it is that JavaScript is single-threaded. Drop those callbacks into a multi-threaded program and few of them would even run — too much contention. Single-threaded sync and multi-threaded sync are completely different. Can a service look similar: several threads, each an independent event loop? Yes. ub**a**server (note the **a**) did that, and the result was poor. Turning blocking into callbacks is not simple. When the block sits inside a loop, a branch, or a deep helper, the rewrite is especially hard, and a lot of legacy or third-party code cannot be rewritten at all. Unavoidable blocking then delays every other callback on that thread, traffic times out, and the server misses its performance target. If someone says "I want to turn our sync code into a pile of callbacks that nobody else understands, and it might even be slower", most people will say no. Do not be sold by async evangelism written for programs that are async top to bottom and ignore multi-threading. That is not the code you have to write.

Async in brpc is not single-threaded async. The callback runs on a different thread from the caller, so you get multi-core scalability, but you must deal with multi-threading. You can block inside the callback; as long as there are enough threads, overall server performance is fine. Async code is still hard to write, which is why we provide [combo channels](combo_channel.md): by composing channels you declare complex access patterns without sweating every detail.

When latency is short and QPS is not high, we still recommend the sync API. That is also why bthread exists: keep synchronous code and still improve interactive performance.

**Choosing sync or async**: compute `qps * latency` (latency in seconds). If the result is on the same order as the number of CPU cores, use sync; otherwise use async.

Examples:

- qps = 2000, latency = 10ms, result = 2000 * 0.01s = 20. Same order as a typical 32-core machine → sync.
- qps = 100, latency = 5s, result = 100 * 5s = 500. Not the same order as core count → async.
- qps = 500, latency = 100ms, result = 500 * 0.1s = 50. Roughly the same order → sync is OK. If latency keeps growing, consider async.

The formula is the average number of in-flight requests (try proving it). It is comparable to thread count and CPU cores. When it is much larger than the core count, most operations are not burning CPU; they are parking a lot of threads. Async then saves thread resources (stack memory) in a visible way. When the value is at or below the core count, the thread-resource savings from async are small, and simple sync code matters more.

# Async or bthread

With bthread you can even implement async yourself. Take "semi-sync" as an example. In brpc you have several options:

- Start several async RPCs and Join them one by one. Join blocks until the RPC finishes. (This is only for comparison with bthread. In real code we recommend [ParallelChannel](combo_channel.md#parallelchannel) instead of joining by hand.)
- Start several bthreads, each doing a sync RPC, then join the bthreads.

Which is faster? The first. The second pays for creating bthreads, and those bthreads stay blocked for the whole RPC and cannot be used for anything else.

**If you only need concurrent RPCs, do not use bthread.**

Parallel compute is a different story. bthread makes it easy to build a tree of parallel work and use multiple cores. If a search has three stages that can run in parallel, start two bthreads for two stages, run the third in place, then join the two bthreads:

```c++
bool search() {
  ...
  bthread th1, th2;
  if (bthread_start_background(&th1, nullptr, part1, part1_args) != 0) {
    LOG(ERROR) << "Fail to create bthread for part1";
    return false;
  }
  if (bthread_start_background(&th2, nullptr, part2, part2_args) != 0) {
    LOG(ERROR) << "Fail to create bthread for part2";
    return false;
  }
  part3(part3_args);
  bthread_join(th1);
  bthread_join(th2);
  return true;
}
```

Notes:

- You could start three bthreads and join all of them, but that costs one extra thread resource compared with running one stage in place.
- There is a delay from creating a bthread to running it (scheduling delay). On a machine that is not very busy, the median is about 3 microseconds, 90% finish within 10 microseconds, and 99.99% within 30 microseconds. Two consequences:
  - The payoff is clear when the compute takes more than 1ms. If the work finishes in a few microseconds, bthread is not worth it.
  - Run the slowest stage in place. Then even if the bthread stages are delayed by a few microseconds, they may still finish first, and the delay disappears. Joining an already-finished bthread returns immediately, with no context-switch cost.

If you need something like a thread pool that runs one class of jobs, you can also replace the pool with bthreads. If job order matters, use bthread's [ExecutionQueue](execution_queue.md).
