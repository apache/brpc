[中文版](../cn/streaming_rpc.md)

# Overview

There are some scenarios when the client or server needs to send huge amount of data, which may grow over time or is too large to put into the RPC attachment. For example, it could be the replica or snapshot transmitting between different nodes in a distributed system. Although we could send data segmentation across multiple RPC between client and server, this will introduce the following problems:

- If these RPCs are parallel, there is no guarantee on the order of the data at the receiving side, which leads to complicate code of reassembling.
- If these RPCs are serial, we have to endure the latency of the network RTT for each RPC together with the process time, which is especially unpredictable.

In order to allow large packets to flow between client and server like a stream, we provide a new communication model: Streaming RPC. Streaming RPC enables users to establishes Stream which is a user-space connection between client and service. Multiple Streams can share the same TCP connection at the same time. The basic transmission unit on Stream is message. As a result, the sender can continuously write to messages to a Stream, while the receiver can read them out in the order of sending.

Streaming RPC ensures/provides:

- The message order at the receiver is exactly the same as that of the sender
- Boundary for messages
- Full duplex
- Flow control
- Notification on timeout

We do not support segment large messages automatically so that multiple Streams on a single TCP connection may lead to [Head-of-line blocking](https://en.wikipedia.org/wiki/Head-of-line_blocking) problem. Please avoid putting huge data into single message until we provide automatic segmentation.

For examples please refer to [example/streaming_echo_c++](https://github.com/brpc/brpc/tree/master/example/streaming_echo_c++/).

# Create a Stream

Currently stream is established by the client only. A new Stream object is created in client and then is used to issues an RPC (through baidu_std protocol) to the specified service. The service could accept this stream by responding to the request without error, thus a Stream is created once the client receives the response successfully. Any error during this process fails the RPC and thus fails the Stream creation. Take the Linux environment as an example, the client creates a [socket](http://linux.die.net/man/7/socket) first (creates a Stream), and then try to establish a connection with the remote side by [connect](http://linux.die.net/man/2/connect) (establish a Stream through RPC). Finally the stream has been created once the remote side [accept](http://linux.die.net/man/2/accept) the request.

> If the client tries to establish a stream to a server that doesn't support streaming RPC, it will always return failure.

In the code we use `StreamId` to represent a Stream, which is the key ID to pass when reading, writing, closing the Stream.

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
     
    // Maximum messages in batch passed to handler->on_received_messages
    // default: 128
    size_t messages_in_batch;
 
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

# Accept a Stream

If a Stream is attached inside the request of an RPC, the service can accept the Stream by `StreamAccept`. On success this function fill the created Stream into `response_stream`, which can be used to send message to the client.

```c++
// [Called at the server side]
// Accept the Stream. If client didn't create a Stream with the request
// (cntl.has_remote_stream() returns false), this method would fail.
// Return 0 on success, -1 otherwise.
int StreamAccept(StreamId* response_stream, Controller &cntl, const StreamOptions* options);
```

# Read from a Stream

Upon creating/accepting a Stream, your can fill the `hander` in `StreamOptions` with your own implemented `StreamInputHandler`. Then you will be notified when the stream receives data, is closed by the other end, or reaches idle timeout.

```c++
class StreamInputHandler {
public:
    // Callback when stream receives data
    virtual int on_received_messages(StreamId id, butil::IOBuf *const messages[], size_t size) = 0;
 
    // Callback when there is no data for a long time on the stream
    virtual void on_idle_timeout(StreamId id) = 0;
 
    // Callback when stream is closed by the other end
    virtual void on_closed(StreamId id) = 0;
};
```

> ***The first call to `on_received_message `***
>
> On the client's side, if the creation process is synchronous, `on_received_message` will be called when the blocking RPC returns. If it's asynchronous, `on_received_message` won't be called until `done->Run()` finishes.
>
> On the server' side, `on_received_message` will be called once `done->Run()` finishes.

# Write to a Stream

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

# Flow Control

When the amount of unacknowledged data reaches the limit, the `Write` operation at the sender will fail with EAGAIN immediately. At this moment, you should wait for the receiver to consume the data synchronously or asynchronously.

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

# Close a Stream

```c++
// Close |stream_id|, after this function is called:
//  - All the following |StreamWrite| would fail
//  - |StreamWait| wakes up immediately.
//  - Both sides |on_closed| would be notifed after all the pending buffers have
//    been received
// This function could be called multiple times without side-effects
int StreamClose(StreamId stream_id);
```

