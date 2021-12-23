brpc provides [asynchronous interface] (client.md#asynchronous access), so a common question is: should I use asynchronous interface or bthread?

Short answer: When the delay is not high, you should first use a simple and easy-to-understand synchronous interface, if not, use an asynchronous interface. Use bthread only when you need multi-core parallel computing.

# Synchronous or asynchronous

Asynchronous means using callbacks instead of blocking, and callbacks where there is blocking. Although callbacks work well in a language like javascript, and the acceptance is very high, as long as you have used them, you will find that this and the callback we need are two different things. The difference is not [lambda](https://en.wikipedia .org/wiki/Anonymous_function), nor [future](https://en.wikipedia.org/wiki/Futures_and_promises), but JavaScript is single-threaded. JavaScript callbacks may not be able to run under multi-threaded, too much competition, single-threaded synchronization method and multi-threaded synchronization method is completely different. Can the service be made into a similar form? Multiple threads, each of which is an independent eventloop. Yes, ub**a**server is (note that with a), but the actual effect is bad, because it is not easy to change the callback from blocking. When blocking occurs in loops, conditional branches, and deep sub-functions, it is particularly difficult to modify, and there are many old ones. Code and third-party code cannot be modified at all. As a result, there will be inevitable blockage in the code, causing other callbacks in that thread to be delayed, traffic timeouts, and server performance that does not meet expectations. If you say, "I want to transform the current synchronization code into a large number of callbacks, everyone except me can't understand it, and the performance may be worse", I guess most people will not agree. Don't be confused by those who advocate asynchrony. They write code that is completely asynchronous from start to finish and does not consider multithreading, which is completely different from what you want to write.

Asynchrony in brpc is completely different from single-threaded asynchrony. Asynchronous callbacks will run in a different thread from where they are called, and you will get multi-core scalability, but the cost is that you have to be aware of multi-threading issues. You can block in the callback, as long as the thread is enough, it will not have any impact on the overall performance of the server. However, asynchronous code is still difficult to write, so we provide [combined access](combo_channel.md) to simplify the problem. By combining different channels, you can perform complex access declaratively without paying too much attention to the details.

Of course, when the delay is not long and the qps is not high, we recommend using a synchronous interface. This is also the motivation for creating bthreads: maintaining synchronous code can also improve interactive performance.

** Determine whether to use synchronous or asynchronous **: Calculate qps * latency (in seconds), if it is the same order of magnitude as the number of cpu cores, use synchronization, otherwise use asynchronous.

for example:

-qps = 2000, latency = 10ms, calculation result = 2000 * 0.01s = 20. It is in the same order of magnitude as the common 32 cores, using synchronization.
-qps = 100, latency = 5s, calculation result = 100 * 5s = 500. It is not in the same order of magnitude as the number of cores, and asynchronous is used.
-qps = 500, latency = 100ms, calculation result = 500 * 0.1s = 50. Basically in the same order of magnitude, synchronization is available. If the delay continues to increase in the future, consider asynchronous.

This formula calculates the average number of simultaneous requests (you can try to prove it), which is comparable to the number of threads and the number of cpu cores. When this value is much larger than the number of cpu cores, it means that most operations do not consume cpu, but block a large number of threads. Using asynchrony can significantly save thread resources (memory occupied by the stack). When this value is less than or similar to the number of cpu cores, the thread resources that can be saved by asynchrony are very limited. At this time, simple and easy-to-understand synchronous code is more important.

# Asynchronous or bthread

With the bthread tool, users can even implement asynchrony by themselves. Taking "semi-synchronization" as an example, users have multiple choices in brpc:

-After initiating multiple asynchronous RPCs, Join one by one, this function will block until the RPC ends. (This is for comparison with bthread. In the implementation, we recommend that you use [ParallelChannel](combo_channel.md#parallelchannel) instead of Join yourself)
-Start multiple bthreads to execute synchronous RPC and join bthreads one by one.

Which is more efficient? Obviously the former. The latter not only has to pay the cost of creating bthreads, bthreads are also blocked during the RPC process and cannot be used for other purposes.

**If it's just for concurrent RPC, don't use bthread. **

But when you need to compute in parallel, the problem is different. Using bthread can simply construct tree-shaped parallel calculations and make full use of multi-core resources. For example, there are three links in the retrieval process that can be processed in parallel. You can create two bthreads to run two links, run the remaining links in place, and finally join the two bthreads. The process is roughly as follows:
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
Point realized like this:
-Of course you can create three bthreads to execute the three parts separately, and join them at the end, but this method consumes one more thread resource.
-There is a delay from establishment to execution of bthread (scheduling delay). On a machine that is not very busy, the median of this delay is about 3 microseconds, 90% within 10 microseconds, and 99.99% within 30 Within microseconds. This shows two points:
-The benefit is more obvious when the calculation time exceeds 1ms. If the calculation is very simple, and it will be over in a few microseconds, it is meaningless to use bthread.
-Try to make the part that runs in place the slowest, so even if the part in the bthread is delayed for a few microseconds, it may end first in the end, eliminating the effect of the delay. And when joining a finished bthread, it will return immediately, without context switching overhead.

In addition, when you have a requirement similar to a thread pool, such as a thread pool that executes a type of job, you can also use bthread instead. If there is a requirement for the execution order of the job, you can use the bthread-based [ExecutionQueue](execution_queue.md).