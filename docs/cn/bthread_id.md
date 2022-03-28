bthread_id是一个特殊的同步结构，它可以互斥RPC过程中的不同环节，也可以O(1)时间内找到RPC上下文(即Controller)。注意，这里我们谈论的是bthread_id_t，不是bthread_t（bthread的tid），这个名字起的确实不太好，容易混淆。

具体来说，bthread_id解决的问题有：

- 在发送RPC过程中response回来了，处理response的代码和发送代码产生竞争。
- 设置timer后很快触发了，超时处理代码和发送代码产生竞争。
- 重试产生的多个response同时回来产生的竞争。
- 通过correlation_id在O(1)时间内找到对应的RPC上下文，而无需建立从correlation_id到RPC上下文的全局哈希表。
- 取消RPC。

上文提到的bug在其他rpc框架中广泛存在，下面我们来看下brpc是如何通过bthread_id解决这些问题的。

bthread_id包括两部分，一个是用户可见的64位id，另一个是对应的不可见的bthread::Id结构体。用户接口都是操作id的。从id映射到结构体的方式和brpc中的[其他结构](memory_management.md)类似：32位是内存池的位移，32位是version。前者O(1)时间定位，后者防止ABA问题。

bthread_id的接口不太简洁，有不少API：

- create
- lock
- unlock
- unlock_and_destroy
- join
- error

这么多接口是为了满足不同的使用流程。

- 发送request的流程：bthread_id_create -> bthread_id_lock -> ... register timer and send RPC ... -> bthread_id_unlock
- 接收response的流程：bthread_id_lock -> ..process response -> bthread_id_unlock_and_destroy
- 异常处理流程：timeout/socket fail -> bthread_id_error -> 执行on_error回调(这里会加锁)，分两种情况
   - 请求重试/backup request： 重新register timer and send RPC -> bthread_id_unlock
   - 无法重试，最终失败：bthread_id_unlock_and_destroy
- 同步等待RPC结束：bthread_id_join

为了减少等待，bthread_id做了一些优化的机制：

- error发生的时候，如果bthread_id已经被锁住，会把error信息放到一个pending queue中，bthread_id_error函数立即返回。当bthread_id_unlock的时候，如果pending queue里面有任务就取出来执行。
- RPC结束的时候，如果存在用户回调，先执行一个bthread_id_about_to_destroy，让正在等待的bthread_id_lock操作立即失败，再执行用户回调（这个可能耗时较长，不可控），最后再执行bthread_id_unlock_and_destroy
