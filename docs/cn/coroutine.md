# C++20 协程支持

bRPC 支持 C++20 协程说明文档。

> 注：该功能是实验性的，请勿在生产环境下使用。

## 使用说明

### 适用场景

C++协程适用于极高并发的场景。由于bthread使用了mmap，存在系统限制，一个进程bthread数量一般最多到万级别，如果采用同步方式用一个bthread来处理一个请求，那么请求的并发度也只能到万级别。如果采用异步方式来写代码，可以达到更高的并发，但又会导致代码难以维护。这时我们就可以使用C++协程，以类似同步的方式来写代码，而达到异步的性能效果。

### 使用前提

1. 需要使用支持c++20的编译器，如gcc 11
2. 需要编译选项中加上 `-std=c++20`

### 简单示例

以下例子显示了如何在bRPC中启动一个C++20协程，在协程中发起 RPC调用，并等待返回结果。

```cpp
#include <brpc/channel.h>
#include <brpc/coroutine.h>

// 协程函数的返回类型，需要是brpc::experimental::Awaitable<T>
// T是函数返回的实际数据类型
brpc::experimental::Awaitable<int> RpcCall(brpc::Channel& channel) {
    EchoRequest request;
    EchoResponse response;
    EchoService_Stub stub(&_channel);
    brpc::Controller cntl;
    brpc::experimental::AwaitableDone done;
    stub.Echo(&cntl, &request, &response, &done);
    // 等待RPC返回结果
    co_await done.awaitable();
    // 返回数据，注意这里用co_return而不是return
    // 因为函数返回值类型是brpc::experimental::Awaitable<int>而不是int
    co_return cntl.ErrorCode();
}

brpc::experimental::Awaitable<void> CoroutineMain(const char* server) {
    brpc::Channel channel;
    channel.Init(server, NULL);
    // co_await会从Awaitable<int>得到int类型的返回值
    int code = co_await RpcCall(channel);
    printf("Rpc result:%d\n", code);
}

int main() {
    // 启动协程
    brpc::experimental::Coroutine coro(CoroutineMain("127.0.0.1:8080"));
    // 等待协程执行完成
    coro.join();
    return 0;
}
```

更完整的例子可以查看源码中的`example/coroutine/coroutine_server.cpp`文件。

### 更多用法

1. 在非协程环境下等待一个协程执行完成:

```cpp
brpc::experimental::Coroutine coro(func(args));
coro.join();
```

2. 在非协程环境下等待协程完成并获取返回值:

```cpp
brpc::experimental::Coroutine coro(func(args)); // func的返回值类型为Awaitable<int>
int result = coro.join<int>();
```

3. 在协程环境下等待协程执行完成:

```cpp
brpc::experimental::Coroutine coro(func(args));
... // 做一些其它事情
co_await coro.awaitable();
```

4. 在协程环境下等待协程执行完成并获取返回值:

```cpp
brpc::experimental::Coroutine coro(func(args)); // func的返回值类型为Awaitable<int>
... // 做一些其它事情
int ret = co_await coro.awaitable<int>();
```

5. 在协程环境下sleep：
```cpp
co_await brpc::experimental::Coroutine::usleep(1000);
```

### 注意事项

1. 协程不保证一个函数的上下文都在同一个pthread或同一个bthread下执行。在co_await之后，代码所在的pthread或bthread可能发生变化，因此依赖于pthread或bthread的线程局部变量的代码（比如rpcz功能）将无法正确工作。
2. 不应在协程中使用阻塞bthread(如bthread_join、同步RPC)或阻塞pthread的函数，否则可能导致死锁或者长耗时。
3. 不要在不必要的地方使用协程，如下面的代码，虽然也能正常工作，但没有意义:

```cpp
brpc::experimental::Awaitable<int> inplace_func() {
    co_return 123;
}
```

### 实现极致性能

如果确保服务的处理代码都运行在协程之中，并且没有任何阻塞bthread或阻塞pthread操作，则可以开启`usercode_in_coroutine`这个flag。开启这个flag之后，bRPC会简化服务端处理逻辑，减少不必要的bthread开销。在这种情况下，实际的工作线程数量将由event_dispatcher_num控制，而不再是由bthread worker数量控制。

## 实现原理

### C++20协程实现原理

为了方便理解，我们把上面的CoroutineMain函数稍微改写一下，把co_await前后的逻辑分成两部分：

```cpp
brpc::experimental::Awaitable<void> CoroutineMain(const char* server) {
    brpc::Channel channel;
    channel.Init(server, NULL);
    brpc::experimental::Awaitable<int> awaitable = RpcCall(channel);

    int code = co_await awaitable;
    printf("Rpc result:%d\n", code);
}
```

上面的代码实际上是怎么执行的呢？当你使用co_await关键字的时候，编译器会把co_await后面的步骤转换成一个callback函数，把这个callback传给实际co_await的那个`Awaitable<T>`对象，比如上面的CoroutineMain函数，经过编译器转换后会变成大概如下的逻辑(简化版，实际要比这个复杂得多):

```cpp

brpc::experimental::Awaitable<void> CoroutineMain(const char* server) {
    // 根据函数返回类型，找到Awaitable<void>的名为promise_type的子类
    // 在函数的入口，创建一个promise_type类型的对象
    auto promise = new brpc::experimental::Awaitable<void>::promise_type();
    // 从promise对象中创建返回Awaitable对象
    Awaitable<void> ret = promise->get_return_object();

    // co_await之前的逻辑，保持不变
    brpc::Channel channel;
    channel.Init(server, NULL);
    brpc::experimental::Awaitable<int> awaitable = RpcCall(channel);

    // co_await的逻辑，转成一个await_suspend的函数调用，传入一个callback函数
    awaitable.await_suspend([promise, &awaitable]() {
        // co_await之后的逻辑，转移到callback函数中
        int code = awaitable.await_resume();
        printf("Rpc result:%d\n", code);
        // 在final_suspend里面，会做一些唤醒调用者、资源释放的工作
        promise->final_suspend();
        delete promise;
    })
    // 返回Awaitable<void>对象，以便上层函数进行处理
    return ret;
}
```

也就是说，co_await就是一个语法转换器，把看似同步的代码转化成异步调用的代码，仅此而已。至于Awaitable类和promise类的具体实现，编译器就不关心了，这是基础库需要做的。比如在brpc中封装了brpc::experimental::Awaitable类和promise子类，实现了await_suspend/await_resume等逻辑，使协程可以正确的工作起来。

### 原子等待操作

上面我们看到的是一个中间函数，它co_await一个子函数返回的Awaitable对象，然后自己也返回一个Awaitable对象。这样层层调用一定有一个尽头，即原子等待操作，它会返回Awaitable对象，但是它内部不再有co_await/co_return这样的语句了。目前实现了3种原子等待操作，未来可以扩展更多。

1. 等待RPC返回结果: `AwaitableDone::awaitable()`
2. 等待sleep: `Coroutine::usleep()`
3. 等待另一个协程完成: `Coroutine::awaitable()`

下面是一个原子等待操作的示例实现，我们需要手动创建一个promise对象，设置set_needs_suspend()，然后发起一个异步调用（如bthread_timer_add)，在回调函数里设置好返回值、调用promise->on_done()，最后根据promise返回Awaitable对象即可。

```cpp
inline Awaitable<int> Coroutine::usleep(int sleep_us) {
    auto promise = new detail::AwaitablePromise<int>();
    promise->set_needs_suspend();
    bthread_timer_t timer;
    auto abstime = butil::microseconds_from_now(sleep_us);
    auto cb = [](void* p) {
        auto promise = static_cast<detail::AwaitablePromise<int>*>(p);
        promise->set_value(0);
        promise->on_done();
    };
    bthread_timer_add(&timer, abstime, cb, promise);
    return Awaitable<int>(promise);
}
```

### 协程与多线程

上面我们可以看到，协程本质上就是一种callback，和线程没有直接关系。它可以是单线程的，也可以是多线程的，这完全取决于它的原子等待操作里是怎么调用callback的。在bRPC的环境里，callback有可能从另一个pthread或bthread发起，所以协程也是需要考虑多线程问题。比如，有可能在调用co_await语句之前，要等待的事情就已经结束了，对于这种情况co_await应该立即返回。

协程和线程可以一起使用，比如我们可以使用bthread将任务scale到多核，然后在任务内部的子任务用协程来实现异步化。