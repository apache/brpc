[English version](../en/streaming_rpc.md)

# 概述

在一些应用场景中， client或server需要向对面发送大量数据，这些数据非常大或者持续地在产生以至于无法放在一个RPC的附件中。比如一个分布式系统的不同节点间传递replica或snapshot。client/server之间虽然可以通过多次RPC把数据切分后传输过去，但存在如下问题：

- 如果这些RPC是并行的，无法保证接收端有序地收到数据，拼接数据的逻辑相当复杂。
- 如果这些RPC是串行的，每次传递都得等待一次网络RTT+处理数据的延时，特别是后者的延时可能是难以预估的。

为了让大块数据以流水线的方式在client/server之间传递， 我们提供了Streaming RPC这种交互模型。Streaming RPC让用户能够在client/service之间建立用户态连接，称为Stream,  同一个TCP连接之上能同时存在多个Stream。 Stream的传输数据以消息为基本单位， 输入端可以源源不断的往Stream中写入消息， 接收端会按输入端写入顺序收到消息。

Streaming RPC保证：

- 有消息边界。
- 接收消息的顺序和发送消息的顺序严格一致。
- 全双工。
- 支持流控。
- 提供超时提醒

目前的实现还没有自动切割过大的消息，同一个tcp连接上的多个Stream之间可能有[Head-of-line blocking](https://en.wikipedia.org/wiki/Head-of-line_blocking)问题，请尽量避免过大的单个消息，实现自动切割后我们会告知并更新文档。

例子见[example/streaming_echo_c++](https://github.com/brpc/brpc/tree/master/example/streaming_echo_c++/)。

# 建立Stream

目前Stream都由Client端建立。Client先在本地创建一个Stream，再通过一次RPC（必须使用baidu_std协议）与指定的Service建立一个Stream，如果Service在收到请求之后选择接受这个Stream， 那在response返回Client后Stream就会建立成功。过程中的任何错误都把RPC标记为失败，同时也意味着Stream创建失败。用linux下建立连接的过程打比方，Client先创建[socket](http://linux.die.net/man/7/socket)（创建Stream），再调用[connect](http://linux.die.net/man/2/connect)尝试与远端建立连接（通过RPC建立Stream），远端[accept](http://linux.die.net/man/2/accept)后连接就建立了（service接受后创建成功）。

> 如果Client尝试向不支持Streaming RPC的老Server建立Stream，将总是失败。

程序中我们用StreamId代表一个Stream，对Stream的读写，关闭操作都将作用在这个Id上。

```c++
struct StreamOptions
    // The max size of unconsumed data allowed at remote side.
    // If |max_buf_size| <= 0, there's no limit of buf size
    // default: 2097152 (2M)
    int max_buf_size;
 
    // Notify user when there's no data for at least |idle_timeout_ms|
    // milliseconds since the last time that on_received_messages or on_idle_timeout
    // finished.
    // default: -1
    long idle_timeout_ms;
     
    // How many messages at most passed to handler->on_received_messages
    // default: 1
    size_t max_messages_size;
 
    // Handle input message, if handler is NULL, the remote side is not allowd to
    // write any message, who will get EBADF on writting
    // default: NULL
    StreamInputHandler* handler;
};
 
// [Called at the client side]
// Create a Stream at client-side along with the |cntl|, which will be connected
// when receiving the response with a Stream from server-side. If |options| is
// NULL, the Stream will be created with default options
// Return 0 on success, -1 otherwise
int StreamCreate(StreamId* request_stream, Controller &cntl, const StreamOptions* options);
```

# 接受Stream

如果client在RPC上附带了一个Stream， service在收到RPC后可以通过调用StreamAccept接受。接受后Server端对应产生的Stream存放在response_stream中，Server可通过这个Stream向Client发送数据。

```c++
// [Called at the server side]
// Accept the Stream. If client didn't create a Stream with the request
// (cntl.has_remote_stream() returns false), this method would fail.
// Return 0 on success, -1 otherwise.
int StreamAccept(StreamId* response_stream, Controller &cntl, const StreamOptions* options);
```

# 读取Stream

在建立或者接受一个Stream的时候， 用户可以继承StreamInputHandler并把这个handler填入StreamOptions中. 通过这个handler，用户可以处理对端的写入数据，连接关闭以及idle timeout

```c++
class StreamInputHandler {
public:
    // 当接收到消息后被调用
    virtual int on_received_messages(StreamId id, butil::IOBuf *const messages[], size_t size) = 0;
 
    // 当Stream上长时间没有数据交互后被调用
    virtual void on_idle_timeout(StreamId id) = 0;
 
    // 当Stream被关闭时被调用
    virtual void on_closed(StreamId id) = 0;
};
```

>***第一次收到请求的时间***
>
>在client端，如果建立过程是一次同步RPC， 那在等待的线程被唤醒之后，on_received_message就可能会被调用到。 如果是异步RPC请求， 那等到这次请求的done->Run() 执行完毕之后， on_received_message就可能会被调用。
>
>在server端， 当框架传入的done->Run()被调用完之后， on_received_message就可能会被调用。

# 写入Stream

```c++
// Write |message| into |stream_id|. The remote-side handler will received the
// message by the written order
// Returns 0 on success, errno otherwise
// Errno:
//  - EAGAIN: |stream_id| is created with positive |max_buf_size| and buf size
//            which the remote side hasn't consumed yet excceeds the number.
//  - EINVAL: |stream_id| is invalied or has been closed
int StreamWrite(StreamId stream_id, const butil::IOBuf &message);
```

# 流控

当存在较多已发送但未接收的数据时，发送端的Write操作会立即失败(返回EAGAIN）， 这时候可以通过同步或异步的方式等待对端消费掉数据。

```c++
// Wait util the pending buffer size is less than |max_buf_size| or error occurs
// Returns 0 on success, errno otherwise
// Errno:
//  - ETIMEDOUT: when |due_time| is not NULL and time expired this
//  - EINVAL: the Stream was close during waiting
int StreamWait(StreamId stream_id, const timespec* due_time);
 
// Async wait
void StreamWait(StreamId stream_id, const timespec *due_time,
                void (*on_writable)(StreamId stream_id, void* arg, int error_code),
                void *arg);
```

# 关闭Stream

```c++
// Close |stream_id|, after this function is called:
//  - All the following |StreamWrite| would fail
//  - |StreamWait| wakes up immediately.
//  - Both sides |on_closed| would be notifed after all the pending buffers have
//    been received
// This function could be called multiple times without side-effects
int StreamClose(StreamId stream_id);
```

