# What is RPC?

Most machines on internet communicate with each other via [TCP/IP](http://en.wikipedia.org/wiki/Internet_protocol_suite). However TCP/IP only guarantees reliable data transmissions, we need to abstract more to build services:

* What is the format of data transmission? Different machine and network may have different byte-orders, directly sending in-memory data is not suitable. Fields in the data are added, modified or removed guadually, how do newer services talk with older services?
* Can TCP connection be reused for multiple requests to reduce overhead? Can multiple requests be sent through one TCP connection simultaneously?
* How to talk with a cluster with many machines?
* What should I do when the connection is broken? What if the server does not respond?
* ...

[RPC](http://en.wikipedia.org/wiki/Remote_procedure_call) addresses above issues by abstracting the network communications as "clients access functions on servers": client sends a request to server, wait until server receives -> processes -> responds the request, then do actions according to the result. 
![rpc](docs/images/rpc.png)

Let's see how the issues are solved.

* RPC needs serialization which is done by [protobuf](https://github.com/google/protobuf) pretty well. Users fill requests in format of protobuf::Message, do RPC, and fetch results from responses in protobuf::Message. protobuf has good forward and backward compatibility for users to change fields and build services incrementally. For http services, [json](http://www.json.org/) is used for serialization extensively.
* Establishment and re-using of connections are transparent to users, but users can make choices, say [different connection types](client.md#连接方式): short, pooled, single.
* Machines are discovered by Naming Service, which can be implemented by [DNS](https://en.wikipedia.org/wiki/Domain_Name_System), [ZooKeeper](https://zookeeper.apache.org/) or [etcd](https://github.com/coreos/etcd). Inside Baidu, we use BNS (Baidu Naming Service). baidu-rpc provides ["list://" and "file://" as well](docs/cn/client.md#名字服务). Users specify load balancing algorithms to choose one machine for each request from all machines, including: round-robin, randomized, [consistent-hashing](docs/cn/consistent_hashing.md)(murmurhash3 or md5) and [locality-aware](docs/cn/lalb.md).
* RPC retries when the connection is broken. When server does not respond within given time, client fails with  timeout error.

# Where can I use RPC?

Almost all network communications.

RPC can't do everything surely, otherwise we don't need the layer of TCP/IP. But in most network communications, RPC meets requirements and isolates the underlying details. 

Common doubts on RPC:

- My data is binary and large, using protobuf is slow. First this is possibly a wrong feeling, you have to prove it with [profilers](docs/cn/cpu_profiler.md), second many protocols support carrying binary data along with protobuf requests and bypass the serialization.
- I'm sending streaming data, which can't be processed by RPC. Actually there're many protocols in RPC can handle streaming data, including [ProgressiveReader in http](docs/cn/http_client.md#持续下载), streams in h2, [streaming rpc](docs/cn/streaming_rpc.md).
- I don't need replies. With some inductions, we know that in your scene, requests can be dropped at any stage, because the client is always unaware of the situation. Are you really sure this is acceptable? Even if you don't need the reply, we recommend sending back small-size replies, which are unlikely performance bottlenecks and probably valuable clues when debugging complex bugs. 

# What is ![baidu-rpc-log](docs/images/logo.png)?

A RPC framework used throughout [Baidu](http://ir.baidu.com/phoenix.zhtml?c=188488&p=irol-irhome), with more than 600,000 instances. Only C++ implementation is opensourced right now.

You can use it for:
* Build a server that can talk in multiple protocols (**on same port**), including:
  * http/https, h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced soon)
  * [rtmp](https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), for building live-streaming services.
  * hadoop_rpc(not opensourced yet)
  * all sorts of protocols used in Baidu: baidu_std, [streaming_rpc](docs/cn/streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc, and nshead-based ones.

* Access all sorts of services more easily, including:
  * http (much more friendly than [libcurl](https://curl.haxx.se/libcurl/)), h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced soon)
  * [redis](docs/cn/redis_client.md) and [memcached](docs/cn/memcache_client.md), thread-safe, more friendly and performant than the official clients
  * [rtmp](https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol)/[flv](https://en.wikipedia.org/wiki/Flash_Video), for building live-streaming services.
  * [streaming_rpc](docs/cn/streaming_rpc.md)
  * all sorts of protocols used in Baidu: baidu_std, [streaming_rpc](docs/cn/streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc, and nshead-based ones.
* rdma support (will be opensourced soon)

* Debug services [via http](docs/cn/builtin_service.md), and run online profilers.

* Get [better latency and throughput](#better-latency-and-throughput).

* Many components in baidu-rpc are customizable, including [naming services](docs/cn/load_balancing.md#名字服务) (dns, zk, etcd), [load balancers](docs/cn/load_balancing.md#负载均衡) (rr, random, consistent hashing), [new protocols](docs/cn/new_protocol.md). Extend them by your own or issue your requests.

**Check out [Getting Started](docs/en/getting_started.md) or [开始使用](docs/cn/getting_started.md) to build!**


# Advantages of baidu-rpc

### More friendly API

Only 3 (major) user headers: [Server](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/server.h), [Channel](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/channel.h), [Controller](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/controller.h), corresponding to server-side, client-side and parameter-set respectively. You don't have to worry about "How to initialize XXXManager", "How to layer all these components together",  "What's the relationship between XXXController and XXXContext".  All you to do is simple:

* Build service? include [brpc/server.h](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/server.h) and follow the comments or [examples](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/example/echo_c++/server.cpp).

* Access service? include [brpc/channel.h](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/channel.h) and follow the comments or [examples](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/example/echo_c++/client.cpp).

* Tweak parameters? Checkout [brpc/controller.h](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/src/brpc/controller.h). Note that the class is shared by server and channel. Methods are separated into 3 parts: client-side, server-side and both-side.

We tried to make simple things simple. Take naming service as an example, in older RPC implementations, you may need to copy a pile of obscure code to make it work, however in baidu-rpc accessing BNS is expressed as `Init("bns://node-name"...`, DNS is "http://domain-name" and local machine list is "file:///home/work/server.list". Without any explanation, you know what it means.

### Build and access all sorts of services

baidu-rpc is capable of accessing and hosting many protocols listed in above section, additionally:

* most of the protobuf-based protocols can be accessed by HTTP+json automatically
* You can build restful http service without protobuf as well.
* You can access service [synchronously](docs/cn/client.md#同步访问) or [asynchrounously](docs/cn/client.md#异步访问), or even [semi-synchronously](docs/cn/client.md#半同步).
* Services can handle requests [synchronously](docs/cn/server.md) or [asynchrounously](docs/cn/server.md#异步service).
* Provide [combo channels](docs/cn/combo_channel.md) to simplify complicated client patterns declaratively, including sharded and parallel accesses.

More importantly, you can [extend baidu-rpc](docs/cn/new_protocol.md) with the protocols used in your organization quickly.

### Make services more reliable

baidu-rpc is extensively used in baidu-rpc, with more than 600,000 instances and 500 kinds of services, from map-reduce, table storages, high-performance computing, machine learning, indexing servers, ranking servers…. It's been proven.

baidu-rpc pays special attentions to development and maintenance efficency, you can [view internal status of servers](docs/cn/builtin_service.md) in browers or with curl, you can analyze [cpu usages](docs/cn/cpu_profiler.md), [heap allocations](docs/cn/heap_profiler.md) and [lock contentions](docs/cn/contention_profiler.md) of services online, you can measure stats by [bvar](docs/cn/bvar.md), which is viewable in [/vars](docs/cn/vars.md).

### Better latency and throughput

Although almost all RPC implementations claim that they're "high-performant", the number are probably just numbers. Being really high-performant in different scenarios is difficult. To make users easier, baidu-rpc goes much deeper at performance than other implementations. 

* Reading and parsing requests from different clients is fully parallelized, and users don't need to distinguish between "IO-threads" and "Processing-threads".  Other implementations probably have "IO-threads" and "Processing-threads" and hash file descriptors(fd) into IO-threads. When a IO-thread handles one of its fds, other fds in the thread can't be handled. If a message is large, other fds are sigficantly delayed. Although different IO-threads run in parallel, you won't have many IO-threads since they don't have too much to do generally except reading/parsing from fds. If you have 10 IO-threads, one fd may affect 10% of all fds, which is unacceptable to industrial online services (requiring 99.99% availability). The problem will be worse, when fds are distributed unevenly accross IO-threads (unfortunately common), or the service is multi-tenancy (common in cloud services). In baidu-rpc, reading from different fds is parallelized and even processing different messages from one fd is parallelized as well. Parsing a large message does not block other messages from the same fd, not to mention other fds. More details can be found [here](docs/cn/io.md#收消息).
* Writing into one fd and multiple fds are highly concurrent. When multiple threads write into the same fd (common for multiplex requests, and h2), the first thread directly writes in-place and other threads submit their write requests in [wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom) manner. One fd can be written into 5,000,000 16-byte messages per second by a couple of highly-contended threads. More details can be found [here](docs/cn/io.md#发消息).
* Minimal locks. High-QPS services can utilize all CPU power on the machine easily. For example, [creating bthreads](docs/cn/memory_management.md) for processing requests is highly concurrent. [setting up timeout](docs/cn/timer_keeping.md) is highly concurrent, [finding RPC contexts](docs/cn/bthread_id.md) according to response is highly concurrent, [recording performance counters](docs/cn/bvar.md) is highly concurrent as well. Users should see very few contentions (via [contention profiler](docs/cn/contention_profiler.md)) caused by RPC framework even if the service runs at 500,000+ QPS.
* Server adjusts thread number according to load. Traditional implementations set number of threads according to latency to avoid limiting the throughput. baidu-rpc creates a new [bthread](docs/cn/bthread.md) for each request and ends the bthread when the request is done, which automatically adjusts thread number according to load.

Check out [benchmark](docs/cn/benchmark.md) for a comparison between baidu-rpc and other implementations.