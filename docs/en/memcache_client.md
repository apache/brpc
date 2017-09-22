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

上述的代码有如下注意点：

- 请求类型必须为MemcacheRequest，回复类型必须为MemcacheResponse，否则CallMethod会失败。不需要stub，直接调用channel.CallMethod，method填NULL。
- 调用request.XXX()增加操作，本例XXX=Set，一个request多次调用不同的操作，这些操作会被同时送到memcached（常被称为pipeline模式）。
- 依次调用response.PopXXX()弹出操作结果，本例XXX=Set，成功返回true，失败返回false，调用response.LastError()可获得错误信息。XXX必须和request的依次对应，否则失败。本例中若用PopGet就会失败，错误信息为“not a GET response"。
- Pop结果独立于RPC结果。即使Set失败，RPC可能还是成功的。RPC失败意味着连接断开，超时之类的。“不能把某个值设入memcached”对于RPC来说还是成功的。如果业务上认为要成功操作才算成功，那么你不仅要判RPC成功，还要判PopXXX是成功的。

目前支持的请求操作有：

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

对应的回复操作：

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

# 访问memcached集群

建立一个使用c_md5负载均衡算法的channel，每个MemcacheRequest只包含一个操作或确保所有的操作始终落在同一台server，就能访问挂载在对应名字服务下的memcached集群了。如果request包含了多个操作，在当前实现下这些操作总会送向同一个server。比方说一个request中包含了多个Get操作，而对应的key分布在多个server上，那么结果就肯定不对了，这个情况下你必须把一个request分开为多个。

或者你可以沿用常见的[twemproxy](https://github.com/twitter/twemproxy)方案。这个方案虽然需要额外部署proxy，还增加了延时，但client端仍可以像访问单点一样的访问它。