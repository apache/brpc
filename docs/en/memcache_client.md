[memcached](http://memcached.org/) is a common cache service today. In order to speed up the access to memcached and make full use of bthread concurrency, brpc directly support the memcached protocol. For examples please refer to: [example/memcache_c++](https://github.com/brpc/brpc/tree/master/example/memcache_c++/)

**NOTE**: brpc only supports the binary protocol of memcache rather than the textual one before version 1.3 since there is little benefit to do that now. If your memcached has a version earlier than 1.3, please upgrade to the latest.

Compared to [libmemcached](http://libmemcached.org/libMemcached.html) (the official client), we have advantages in:

- Thread safety. No need to set up a separate client for each thread.
- Support access patterns of synchronous, asynchronous, batch synchronous, batch asynchronous. Can be used with ParallelChannel to enable access combinations.
- Support various [connection types](client.md#Connection Type). Support timeout, backup request, cancellation, tracing, built-in services, and other basic benefits of the RPC framework.
- Have the concept of request/response while libmemcached haven't, where users have to do extra maintenance since the received message doesn't have a relationship with the sent message.

The current implementation takes full advantage of the RPC concurrency mechanism to avoid copying as much as possible. A single client can easily reaches the limit of a memcached instance (version 1.4.15) on the same machine: 90,000 QPS for single connection, 330,000 QPS for multiple connections. In most cases, brpc should be able to make full use of memcached's performance.

# Request to single memcached

Create a `Channel` to access memcached:

```c++
#include <brpc/memcache.h>
#include <brpc/channel.h>
 
ChannelOptions options;
options.protocol = brpc::PROTOCOL_MEMCACHE;
if (channel.Init("0.0.0.0:11211", &options) != 0) {  // 11211 is the default port for memcached
   LOG(FATAL) << "Fail to init channel to memcached";
   return -1;
}
... 
```

Set data to memcached

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

There are some notes on the above code:

- The class of the request must be `MemcacheRequest`, and `MemcacheResponse` for the response, otherwise `CallMethod` will fail. `stub` is not necessary. Just call `channel.CallMethod` with `method` set to NULL.
- Call `request.XXX()` to add operation, where `XXX=Set` in this case. Multiple operations on a single request will be sent to memcached in batch (often referred to as pipeline mode).
- call `response.PopXXX()` pop-up operation results, where `XXX=Set` in this case. Return true on success, and false on failure, in which case use `response.LastError()` to get the error message. Operation `XXX` must correspond to request, otherwise it will fail. In the above example, a `PopGet` will fail with the error message of "not a GET response".
- The results of `Pop` are independent of RPC result. Even if `Set` fails, RPC may still be successful. RPC failure means things like broken connection, timeout, and so on . *Can not put a value into memcached* is  still a successful RPC. AS a reulst, in order to make sure success of the entire process, you need to not only determine the success of RPC, but also the success of `PopXXX`.

Currently our supported operations are:

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

And the corresponding reply operations:

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

# Access to memcached cluster

If you want to access a memcached cluster mounted on some naming service, you should create a `Channel` that uses the c_md5 as the load balancing algorithm and make sure each `MemcacheRequest` contains only one operation or all operations fall on the same server. Since under the current implementation, multiple operations inside a single request will always be sent to the same server. For example, if a request contains a number of Get while the corresponding keys distribute in different servers, the result must be wrong, in which case you have to separate the request according to key distribution.

Another choice is to follow the common [twemproxy](https://github.com/twitter/twemproxy) style. This allows the client can still access the cluster just like a single point, although it requires deployment of the proxy and the additional latency.