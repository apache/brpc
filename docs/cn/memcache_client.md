[English version](../en/memcache_client.md)

[memcached](http://memcached.org/)是常用的缓存服务，为了使用户更快捷地访问memcached并充分利用bthread的并发能力，brpc直接支持memcache协议。示例程序：[example/memcache_c++](https://github.com/brpc/brpc/tree/master/example/memcache_c++/)

**注意**：brpc只支持memcache的二进制协议。memcached在1.3前只有文本协议，但在当前看来支持的意义甚微。如果你的memcached早于1.3，升级版本。

相比使用[libmemcached](http://libmemcached.org/libMemcached.html)(官方client)的优势有：

- 线程安全。用户不需要为每个线程建立独立的client。
- 支持同步、异步、半同步等访问方式，能使用[ParallelChannel等](combo_channel.md)组合访问方式。
- 支持多种[连接方式](client.md#连接方式)。支持超时、backup request、取消、tracing、内置服务等一系列brpc提供的福利。
- 有明确的request和response。而libmemcached是没有的，收到的消息不能直接和发出的消息对应上，用户得做额外开发，而且并没有那么容易做对。

当前实现充分利用了RPC的并发机制并尽量避免了拷贝。一个client可以轻松地把一个同机memcached实例(版本1.4.15)压到极限：单连接9万，多连接33万。在大部分情况下，brpc client能充分发挥memcached的性能。

# 访问单台memcached

创建一个访问memcached的Channel：

```c++
#include <brpc/memcache.h>
#include <brpc/channel.h>
 
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_MEMCACHE;
if (channel.Init("0.0.0.0:11211", &options) != 0) {  // 11211是memcached的默认端口
   LOG(FATAL) << "Fail to init channel to memcached";
   return -1;
}
... 
```

往memcached中设置一份数据。

```c++
// 写入key="hello" value="world" flags=0xdeadbeef，10秒失效，无视cas。
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

上述代码的说明：

- 请求类型必须为MemcacheRequest，回复类型必须为MemcacheResponse，否则CallMethod会失败。不需要stub，直接调用channel.CallMethod，method填NULL。
- 调用request.XXX()增加操作，本例XXX=Set，一个request多次调用不同的操作，这些操作会被同时送到memcached（常被称为pipeline模式）。
- 依次调用response.PopXXX()弹出操作结果，本例XXX=Set，成功返回true，失败返回false，调用response.LastError()可获得错误信息。XXX必须和request的依次对应，否则失败。本例中若用PopGet就会失败，错误信息为“not a GET response"。
- Pop结果独立于RPC结果。即使“不能把某个值设入memcached”，RPC可能还是成功的。RPC失败指连接断开，超时之类的。如果业务上认为要成功操作才算成功，那么你不仅要判RPC成功，还要判PopXXX是成功的。

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

建立一个使用c_md5负载均衡算法的channel就能访问挂载在对应命名服务下的memcached集群了。注意每个MemcacheRequest应只包含一个操作或确保所有的操作是同一个key。如果request包含了多个操作，在当前实现下这些操作总会送向同一个server，假如对应的key分布在多个server上，那么结果就不对了，这个情况下你必须把一个request分开为多个，每个包含一个操作。

或者你可以沿用常见的[twemproxy](https://github.com/twitter/twemproxy)方案。这个方案虽然需要额外部署proxy，还增加了延时，但client端仍可以像访问单点一样的访问它。
