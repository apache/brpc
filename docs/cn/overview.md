[English version](../en/overview.md)

# 什么是RPC?

互联网上的机器大都通过[TCP/IP协议](http://en.wikipedia.org/wiki/Internet_protocol_suite)相互访问，但TCP/IP只是往远端发送了一段二进制数据，为了建立服务还有很多问题需要抽象：

- 数据以什么格式传输？不同机器间，网络间可能是不同的字节序，直接传输内存数据显然是不合适的；随着业务变化，数据字段往往要增加或删减，怎么兼容前后不同版本的格式？
- 一个TCP连接可以被多个请求复用以减少开销么？多个请求可以同时发往一个TCP连接么?
- 如何管理和访问很多机器？
- 连接断开时应该干什么？
- 万一server不发送回复怎么办？
- ...

[RPC](http://en.wikipedia.org/wiki/Remote_procedure_call)可以解决这些问题，它把网络交互类比为“client访问server上的函数”：client向server发送request后开始等待，直到server收到、处理、回复client后，client又再度恢复并根据response做出反应。

![rpc.png](../images/rpc.png)

我们来看看上面的一些问题是如何解决的：

- 数据需要序列化，[protobuf](https://github.com/google/protobuf)在这方面做的不错。用户填写protobuf::Message类型的request，RPC结束后，从同为protobuf::Message类型的response中取出结果。protobuf有较好的前后兼容性，方便业务调整字段。http广泛使用[json](http://www.json.org/)作为序列化方法。
- 用户无需关心连接如何建立，但可以选择不同的[连接方式](client.md#连接方式)：短连接，连接池，单连接。
- 大量机器一般通过命名服务被发现，可基于[DNS](https://en.wikipedia.org/wiki/Domain_Name_System), [ZooKeeper](https://zookeeper.apache.org/), [etcd](https://github.com/coreos/etcd)等实现。在百度内，我们使用BNS (Baidu Naming Service)。brpc也提供["list://"和"file://"](client.md#命名服务)。用户可以指定负载均衡算法，让RPC每次选出一台机器发送请求，包括: round-robin, randomized, [consistent-hashing](consistent_hashing.md)(murmurhash3 or md5)和 [locality-aware](lalb.md).
- 连接断开时可以重试。
- 如果server没有在给定时间内回复，client会返回超时错误。

# 哪里可以使用RPC?

几乎所有的网络交互。

RPC不是万能的抽象，否则我们也不需要TCP/IP这一层了。但是在我们绝大部分的网络交互中，RPC既能解决问题，又能隔离更底层的网络问题。

对于RPC常见的质疑有：

- 我的数据非常大，用protobuf序列化太慢了。首先这可能是个伪命题，你得用[profiler](cpu_profiler.md)证明慢了才是真的慢，其次很多协议支持携带二进制数据以绕过序列化。
- 我传输的是流数据，RPC表达不了。事实上brpc中很多协议支持传递流式数据，包括[http中的ProgressiveReader](http_client.md#持续下载), h2的streams, [streaming rpc](streaming_rpc.md), 和专门的流式协议RTMP。
- 我的场景不需要回复。简单推理可知，你的场景中请求可丢可不丢，可处理也可不处理，因为client总是无法感知，你真的确认这是OK的？即使场景真的不需要，我们仍然建议用最小的结构体回复，因为这不大会是瓶颈，并且追查复杂bug时可能是很有价值的线索。

# 什么是![brpc](../images/logo.png)?

百度内最常使用的工业级RPC框架, 有1,000,000+个实例(不包含client)和上千种服务, 在百度内叫做"**baidu-rpc**". 目前只开源C++版本。

你可以使用它：

* 搭建能在**一个端口**支持多协议的服务, 或访问各种服务
  * restful http/https, [h2](https://http2.github.io/http2-spec)/[gRPC](https://grpc.io)。使用brpc的http实现比[libcurl](https://curl.haxx.se/libcurl/)方便多了。从其他语言通过HTTP/h2+json访问基于protobuf的协议.
  * [redis](redis_client.md)和[memcached](memcache_client.md), 线程安全，比官方client更方便。
  * [rtmp](https://github.com/brpc/brpc/blob/master/src/brpc/rtmp.h)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), 可用于搭建[流媒体服务](https://github.com/brpc/media-server).
  * hadoop_rpc(可能开源)
  * 支持[rdma](https://en.wikipedia.org/wiki/Remote_direct_memory_access)(即将开源)
  * 支持[thrift](thrift.md) , 线程安全，比官方client更方便
  * 各种百度内使用的协议: [baidu_std](baidu_std.md), [streaming_rpc](streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc和使用nshead的各种协议.
  * 基于工业级的[RAFT算法](https://raft.github.io)实现搭建[高可用](https://en.wikipedia.org/wiki/High_availability)分布式系统，已在[braft](https://github.com/brpc/braft)开源。
* Server能[同步](server.md)或[异步](server.md#异步service)处理请求。
* Client支持[同步](client.md#同步访问)、[异步](client.md#异步访问)、[半同步](client.md#半同步)，或使用[组合channels](combo_channel.md)简化复杂的分库或并发访问。
* [通过http界面](builtin_service.md)调试服务, 使用[cpu](cpu_profiler.md), [heap](heap_profiler.md), [contention](contention_profiler.md) profilers.
* 获得[更好的延时和吞吐](#更好的延时和吞吐).
* 把你组织中使用的协议快速地[加入brpc](new_protocol.md)，或定制各类组件, 包括[命名服务](load_balancing.md#命名服务) (dns, zk, etcd), [负载均衡](load_balancing.md#负载均衡) (rr, random, consistent hashing)

# brpc的优势

### 更友好的接口

只有三个(主要的)用户类: [Server](https://github.com/brpc/brpc/blob/master/src/brpc/server.h), [Channel](https://github.com/brpc/brpc/blob/master/src/brpc/channel.h), [Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h), 分别对应server端，client端，参数集合. 你不必推敲诸如"如何初始化XXXManager", "如何组合各种组件",  "XXXController的XXXContext间的关系是什么"。要做的很简单:

* 建服务? 包含[brpc/server.h](https://github.com/brpc/brpc/blob/master/src/brpc/server.h)并参考注释或[示例](https://github.com/brpc/brpc/blob/master/example/echo_c++/server.cpp).
* 访问服务? 包含[brpc/channel.h](https://github.com/brpc/brpc/blob/master/src/brpc/channel.h)并参考注释或[示例](https://github.com/brpc/brpc/blob/master/example/echo_c++/client.cpp).
* 调整参数? 看看[brpc/controller.h](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h). 注意这个类是Server和Channel共用的，分成了三段，分别标记为Client-side, Server-side和Both-side methods。

我们尝试让事情变得更加简单，以命名服务为例，在其他RPC实现中，你也许需要复制一长段晦涩的代码才可使用，而在brpc中访问BNS可以这么写"bns://node-name"，DNS是`Init("http://domain-name", ...)`，本地文件列表是"file:///home/work/server.list"，相信不用解释，你也能明白这些代表什么。

### 使服务更加可靠

brpc在百度内被广泛使用:

* map-reduce服务和table存储
* 高性能计算和模型训练
* 各种索引和排序服务
* ….

它是一个经历过考验的实现。

brpc特别重视开发和维护效率, 你可以通过浏览器或curl[查看server内部状态](builtin_service.md), 分析在线服务的[cpu热点](cpu_profiler.md), [内存分配](heap_profiler.md)和[锁竞争](contention_profiler.md), 通过[bvar](bvar.md)统计各种指标并通过[/vars](vars.md)查看。

### 更好的延时和吞吐

虽然大部分RPC实现都声称“高性能”，但数字仅仅是数字，要在广泛的场景中做到高性能仍是困难的。为了统一百度内的通信架构，brpc在性能方面比其他RPC走得更深。

- 对不同客户端请求的读取和解析是完全并发的，用户也不用区分”IO线程“和”处理线程"。其他实现往往会区分“IO线程”和“处理线程”，并把[fd](http://en.wikipedia.org/wiki/File_descriptor)（对应一个客户端）散列到IO线程中去。当一个IO线程在读取其中的fd时，同一个线程中的fd都无法得到处理。当一些解析变慢时，比如特别大的protobuf message，同一个IO线程中的其他fd都遭殃了。虽然不同IO线程间的fd是并发的，但你不太可能开太多IO线程，因为这类线程的事情很少，大部分时候都是闲着的。如果有10个IO线程，一个fd能影响到的”其他fd“仍有相当大的比例（10个即10%，而工业级在线检索要求99.99%以上的可用性）。这个问题在fd没有均匀地分布在IO线程中，或在多租户(multi-tenancy)环境中会更加恶化。在brpc中，对不同fd的读取是完全并发的，对同一个fd中不同消息的解析也是并发的。解析一个特别大的protobuf message不会影响同一个客户端的其他消息，更不用提其他客户端的消息了。更多细节看[这里](io.md#收消息)。
- 对同一fd和不同fd的写出是高度并发的。当多个线程都要对一个fd写出时（常见于单连接），第一个线程会直接在原线程写出，其他线程会以[wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)的方式托付自己的写请求，多个线程在高度竞争下仍可以在1秒内对同一个fd写入500万个16字节的消息。更多细节看[这里](io.md#发消息)。
- 尽量少的锁。高QPS服务可以充分利用一台机器的CPU。比如为处理请求[创建bthread](memory_management.md), [设置超时](timer_keeping.md), 根据回复[找到RPC上下文](bthread_id.md), [记录性能计数器](bvar.md)都是高度并发的。即使服务的QPS超过50万，用户也很少在[contention profiler](contention_profiler.md))中看到框架造成的锁竞争。
- 服务器线程数自动调节。传统的服务器需要根据下游延时的调整自身的线程数，否则吞吐可能会受影响。在brpc中，每个请求均运行在新建立的[bthread](bthread.md)中，请求结束后线程就结束了，所以天然会根据负载自动调节线程数。

brpc和其他实现的性能对比见[这里](benchmark.md)。
