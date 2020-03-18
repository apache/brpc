[中文版](../cn/memcache_client.md)

[memcached](http://memcached.org/) is a common caching service. In order to access memcached more conveniently and make full use of bthread's capability of concurrency, brpc directly supports the memcached protocol. Check [example/memcache_c++](https://github.com/brpc/brpc/tree/master/example/memcache_c++/) for an example.

**NOTE**: brpc only supports the binary protocol of memcache. There's little benefit to support the textual protocol which is replaced since memcached 1.3. If your memcached is older than 1.3, upgrade to a newer version.

Advantages compared to [libmemcached](http://libmemcached.org/libMemcached.html) (the official client):

- Thread safety. No need to set up separate clients for each thread.
- Support synchronous, asynchronous, semi-synchronous accesses etc. Support [ParallelChannel etc](combo_channel.md) to define access patterns declaratively.
- Support various [connection types](client.md#connection-type). Support timeout, backup request, cancellation, tracing, built-in services, and other benefits offered by brpc.
- Have the concept of requests and responses while libmemcached don't. Users have to do extra bookkeepings to associate received messages with sent messages, which is not trivial.

The current implementation takes full advantage of the RPC concurrency mechanism and avoids copying as much as possible. A single client can easily pushes a memcached instance (version 1.4.15) on the same machine to its limit: 90,000 QPS for single connection, 330,000 QPS for multiple connections. In most cases, brpc is able to make full use of memcached's capabilities.

# Request a memcached server

Create a `Channel` for accessing memcached:

```c++
#include <brpc/memcache.h>
#include <brpc/channel.h>
 
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_MEMCACHE;
if (channel.Init("0.0.0.0:11211", &options) != 0) {  // 11211 is the default port for memcached
   LOG(FATAL) << "Fail to init channel to memcached";
   return -1;
}
... 
```

Following example tries to set data to memcached:

```c++
// Set key="hello" value="world" flags=0xdeadbeef, expire in 10s, and ignore cas
brpc::MemcacheRequest request;
brpc::MemcacheResponse response;
brpc::Controller cntl;
if (!request.Set("hello", "world", 0xdeadbeef/*flags*/, 10/*expiring seconds*/, 0/*ignore cas*/)) {
    LOG(FATAL) << "Fail to SET request";
    return -1;
} 
channel.CallMethod(NULL, &cntl, &request, &response, NULL/*done*/);
if (cntl.Failed()) {
    LOG(FATAL) << "Fail to access memcached, " << cntl.ErrorText();
    return -1;
}  
if (!response.PopSet(NULL)) {
    LOG(FATAL) << "Fail to SET memcached, " << response.LastError();
    return -1;   
}
...
```

Notes on above code:

- The class of the request must be `MemcacheRequest`, response must be `MemcacheResponse`, otherwise `CallMethod` fails. `stub` is not necessary, just call `channel.CallMethod` with `method` to NULL.
- Call `request.XXX()` to add an operation, where `XXX` is `Set` in this example. Multiple operations inside a request are sent to a memcached server together (often referred to as "pipeline mode").
- call `response.PopXXX()` to pop result of an operation from the response, where `XXX` is `Set` in this example. true is returned on success, and false otherwise in which case use `response.LastError()` to get the error message. `XXX` must match the corresponding operation in the request, otherwise the pop is rejected. In above example, a `PopGet` would fail with the error message of "not a GET response".
- Results of `Pop` are independent from the RPC result. Even if "a value cannot be put into the memcached", the RPC may still be successful. RPC failure means things like broken connection, timeout etc. If the business logic requires the memcache operations to be succesful, you should test successfulness of both RPC and `PopXXX`.

Supported operations currently:

```c++
bool Set(const Slice& key, const Slice& value, uint32_t flags, uint32_t exptime, uint64_t cas_value);
bool Add(const Slice& key, const Slice& value, uint32_t flags, uint32_t exptime, uint64_t cas_value);
bool Replace(const Slice& key, const Slice& value, uint32_t flags, uint32_t exptime, uint64_t cas_value);
bool Append(const Slice& key, const Slice& value, uint32_t flags, uint32_t exptime, uint64_t cas_value);
bool Prepend(const Slice& key, const Slice& value, uint32_t flags, uint32_t exptime, uint64_t cas_value);
bool Delete(const Slice& key);
bool Flush(uint32_t timeout);
bool Increment(const Slice& key, uint64_t delta, uint64_t initial_value, uint32_t exptime);
bool Decrement(const Slice& key, uint64_t delta, uint64_t initial_value, uint32_t exptime);
bool Touch(const Slice& key, uint32_t exptime);
bool Version();
```

Corresponding operations in replies:

```c++
// Call LastError() of the response to check the error text when any following operation fails.
bool PopGet(IOBuf* value, uint32_t* flags, uint64_t* cas_value);
bool PopGet(std::string* value, uint32_t* flags, uint64_t* cas_value);
bool PopSet(uint64_t* cas_value);
bool PopAdd(uint64_t* cas_value);
bool PopReplace(uint64_t* cas_value);
bool PopAppend(uint64_t* cas_value);
bool PopPrepend(uint64_t* cas_value);
bool PopDelete();
bool PopFlush();
bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
bool PopTouch();
bool PopVersion(std::string* version);
```

# Request a memcached cluster

Create a `Channel` using the `c_md5` as the load balancing algorithm to access a memcached cluster mounted under a naming service. Note that each `MemcacheRequest` should contain only one operation or all operations have the same key. Under current implementation, multiple operations inside a single request are always sent to a same server. If the keys are located on different servers, the result must be wrong. In which case, you have to divide the request into multilple ones with one operation each.

Another choice is to use the common [twemproxy](https://github.com/twitter/twemproxy) solution, which makes clients access the cluster just like accessing a single server, although the solution needs to deploy proxies and adds more latency.