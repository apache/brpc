# What is RPC?

Most machines on internet communicate with each other via [TCP/IP](https://en.wikipedia.org/wiki/Internet_protocol_suite). However, TCP/IP only guarantees reliable data transmissions. We need to abstract more to build services:

* What is the format of data transmission? Different machines and networks may have different byte-orders, directly sending in-memory data is not suitable. Fields in the data are added, modified or removed gradually, how do newer services talk with older services?
* Can TCP connection be reused for multiple requests to reduce overhead? Can multiple requests be sent through one TCP connection simultaneously?
* How to talk with a cluster with many machines?
* What should I do when the connection is broken? What if the server does not respond?
* ...

[RPC](https://en.wikipedia.org/wiki/Remote_procedure_call) addresses the above issues by abstracting network communications as "clients accessing functions on servers": client sends a request to server, wait until server receives -> processes -> responds to the request, then do actions according to the result. 
![rpc.png](docs/images/rpc.png)

Let's see how the issues are solved.

* RPC needs serialization which is done by [protobuf](https://github.com/google/protobuf) pretty well. Users fill requests in format of protobuf::Message, do RPC, and fetch results from responses in protobuf::Message. protobuf has good forward and backward compatibility for users to change fields and build services incrementally. For http services, [json](http://www.json.org/) is used for serialization extensively.
* Establishment and re-using of connections is transparent to users, but users can make choices like [different connection types](docs/cn/client.md#连接方式): short, pooled, single.
* Machines are discovered by a Naming Service, which can be implemented by [DNS](https://en.wikipedia.org/wiki/Domain_Name_System), [ZooKeeper](https://zookeeper.apache.org/) or [etcd](https://github.com/coreos/etcd). Inside Baidu, we use BNS (Baidu Naming Service). brpc provides ["list://" and "file://" as well](docs/cn/client.md#名字服务). Users specify load balancing algorithms to choose one machine for each request from all machines, including: round-robin, randomized, [consistent-hashing](docs/cn/consistent_hashing.md)(murmurhash3 or md5) and [locality-aware](docs/cn/lalb.md).
* RPC retries when the connection is broken. When server does not respond within the given time, client fails with a timeout error.

# Where can I use RPC?

Almost all network communications.

RPC can't do everything surely, otherwise we don't need the layer of TCP/IP. But in most network communications, RPC meets requirements and isolates the underlying details. 

Common doubts on RPC:

- My data is binary and large, using protobuf will be slow. First, this is possibly a wrong feeling and you will have to test it and prove it with [profilers](docs/cn/cpu_profiler.md). Second, many protocols support carrying binary data along with protobuf requests and bypass the serialization.
- I'm sending streaming data which can't be processed by RPC. Actually many protocols in RPC can handle streaming data, including [ProgressiveReader in http](docs/cn/http_client.md#持续下载), streams in h2, [streaming rpc](docs/cn/streaming_rpc.md), and RTMP which is a specialized streaming protocol.
- I don't need replies. With some inductions, we know that in your scenario requests can be dropped at any stage because the client is always unaware of the situation. Are you really sure this is acceptable? Even if you don't need the reply, we recommend sending back small-sized replies, which are unlikely to be performance bottlenecks and will probably provide valuable clues when debugging complex bugs. 

# What is ![brpc](docs/images/logo.png)?

A RPC framework used throughout [Baidu](http://ir.baidu.com/phoenix.zhtml?c=188488&p=irol-irhome), with **600,000+** instances(not counting clients) and **500+** kinds of services, called "**baidu-rpc**" inside Baidu. Only C++ implementation is opensourced right now.

You can use it to:
* Build a server that can talk in multiple protocols (**on same port**), or access all sorts of services
  * restful http/https, h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced soon). using http in brpc is much more friendly than [libcurl](https://curl.haxx.se/libcurl/).
  * [redis](docs/cn/redis_client.md) and [memcached](docs/cn/memcache_client.md), thread-safe, more friendly and performant than the official clients
  * [rtmp](https://github.com/brpc/brpc/blob/master/src/brpc/rtmp.h)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), for building [live-streaming services](docs/cn/live_streaming.md).
  * hadoop_rpc(not opensourced yet)
  * [rdma](https://en.wikipedia.org/wiki/Remote_direct_memory_access) support via [openucx](https://github.com/openucx/ucx) (will be opensourced soon)
  * all sorts of protocols used in Baidu: baidu_std, [streaming_rpc](docs/cn/streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc, and nshead-based ones.
  * Access protobuf-based protocols with HTTP+json, probably from another language.
  * Build distributed services using [RAFT consensus algorithm](https://raft.github.io) (will be opensourced at [braft](https://github.com/brpc/braft) soon)
* Create rich processing patterns
  * Services can handle requests [synchronously](docs/cn/server.md) or [asynchronously](docs/cn/server.md#异步service).
  * Access service [synchronously](docs/cn/client.md#同步访问) or [asynchronously](docs/cn/client.md#异步访问), or even [semi-synchronously](docs/cn/client.md#半同步).
  * Use [combo channels](docs/cn/combo_channel.md) to simplify complicated client patterns declaratively, including sharded and parallel accesses.
* Debug services [via http](docs/cn/builtin_service.md), and run  [cpu](docs/cn/cpu_profiler.md), [heap](docs/cn/heap_profiler.md) and [contention](docs/cn/contention_profiler.md) profilers.
* Get [better latency and throughput](#better-latency-and-throughput).
* [Extend brpc](docs/cn/new_protocol.md) with the protocols used in your organization quickly, or customize components, including [naming services](docs/cn/load_balancing.md#名字服务) (dns, zk, etcd), [load balancers](docs/cn/load_balancing.md#负载均衡) (rr, random, consistent hashing)

# Advantages of brpc

### More friendly API

Only 3 (major) user headers: [Server](https://github.com/brpc/brpc/blob/master/src/brpc/server.h), [Channel](https://github.com/brpc/brpc/blob/master/src/brpc/channel.h), [Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h), corresponding to server-side, client-side and parameter-set respectively. You don't have to worry about "How to initialize XXXManager", "How to layer all these components together",  "What's the relationship between XXXController and XXXContext". All you need to do is simple:

* Build service? include [brpc/server.h](https://github.com/brpc/brpc/blob/master/src/brpc/server.h) and follow the comments or [examples](https://github.com/brpc/brpc/blob/master/example/echo_c++/server.cpp).

* Access service? include [brpc/channel.h](https://github.com/brpc/brpc/blob/master/src/brpc/channel.h) and follow the comments or [examples](https://github.com/brpc/brpc/blob/master/example/echo_c++/client.cpp).

* Tweak parameters? Checkout [brpc/controller.h](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h). Note that the class is shared by server and channel. Methods are separated into 3 parts: client-side, server-side and both-side.

We tried to make simple things simple. Take naming service as an example. In older RPC implementations you may need to copy a pile of obscure code to make it work, however, in brpc accessing BNS is expressed as `Init("bns://node-name", ...)`, DNS is `Init("http://domain-name", ...)` and local machine list is `Init("file:///home/work/server.list", ...)`. Without any explanation, you know what it means.

### Make services more reliable

brpc is extensively used in Baidu, from:

* map-reduce service & table storages
* high-performance computing & model training
* all sorts of indexing & ranking servers
* ….

It's been proven.

brpc pays special attentions to development and maintenance efficency, you can [view internal status of servers](docs/cn/builtin_service.md) in web browser or with curl, you can analyze [cpu usage](docs/cn/cpu_profiler.md), [heap allocations](docs/cn/heap_profiler.md) and [lock contentions](docs/cn/contention_profiler.md) of services online, you can measure stats by [bvar](docs/cn/bvar.md) which is viewable in [/vars](docs/cn/vars.md).

### Better latency and throughput

Although almost all RPC implementations claim that they're "high-performant", the numbers are probably just numbers. Being really high-performant in different scenarios is difficult. To unify communication infra inside Baidu, brpc goes much deeper at performance than other implementations.

* Reading and parsing requests from different clients is fully parallelized and users don't need to distinguish between "IO-threads" and "Processing-threads". Other implementations probably have "IO-threads" and "Processing-threads" and hash file descriptors(fd) into IO-threads. When a IO-thread handles one of its fds, other fds in the thread can't be handled. If a message is large, other fds are significantly delayed. Although different IO-threads run in parallel, you won't have many IO-threads since they don't have too much to do generally except reading/parsing from fds. If you have 10 IO-threads, one fd may affect 10% of all fds, which is unacceptable to industrial online services (requiring 99.99% availability). The problem will be worse when fds are distributed unevenly accross IO-threads (unfortunately common), or the service is multi-tenancy (common in cloud services). In brpc, reading from different fds is parallelized and even processing different messages from one fd is parallelized as well. Parsing a large message does not block other messages from the same fd, not to mention other fds. More details can be found [here](docs/cn/io.md#收消息).
* Writing into one fd and multiple fds is highly concurrent. When multiple threads write into the same fd (common for multiplexed connections), the first thread directly writes in-place and other threads submit their write requests in [wait-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom) manner. One fd can be written into 5,000,000 16-byte messages per second by a couple of highly-contended threads. More details can be found [here](docs/cn/io.md#发消息).
* Minimal locks. High-QPS services can utilize all CPU power on the machine. For example, [creating bthreads](docs/cn/memory_management.md) for processing requests, [setting up timeout](docs/cn/timer_keeping.md), [finding RPC contexts](docs/cn/bthread_id.md) according to response, [recording performance counters](docs/cn/bvar.md) are all highly concurrent. Users see very few contentions (via [contention profiler](docs/cn/contention_profiler.md)) caused by RPC framework even if the service runs at 500,000+ QPS.
* Server adjusts thread number according to load. Traditional implementations set number of threads according to latency to avoid limiting the throughput. brpc creates a new [bthread](docs/cn/bthread.md) for each request and ends the bthread when the request is done, which automatically adjusts thread number according to load.

Check out [benchmark](docs/cn/benchmark.md) for a comparison between brpc and other implementations.

# Try it!

* Check out [Getting Started](docs/cn/getting_started.md) to start.
* Play with [examples](https://github.com/brpc/brpc/tree/master/example/).
* Docs:
  * [Benchmark](docs/cn/benchmark.md)
  * [bvar](docs/cn/bvar.md)
    * [bvar_c++](docs/cn/bvar_c++.md)
  * [bthread](docs/cn/bthread.md)
    * [bthread or not](docs/cn/bthread_or_not.md)
    * [thread-local](docs/cn/thread_local.md)
    * [Execution Queue](docs/cn/execution_queue.md)
  * Client
    * [Basics](docs/cn/client.md)
    * [ErrorCode](docs/cn/error_code.md)
    * [Combo channels](docs/cn/combo_channel.md)
    * [Access HTTP](docs/cn/http_client.md)
    * [Access UB](docs/cn/ub_client.md)
    * [Streaming RPC](docs/cn/streaming_rpc.md)
    * [Access redis](docs/cn/redis_client.md)
    * [Access memcached](docs/cn/memcache_client.md)
    * [Backup request](docs/cn/backup_request.md)
    * [Dummy server](docs/cn/dummy_server.md)
  * Server
    * [Basics](docs/cn/server.md)
    * [Build HTTP service](docs/cn/http_service.md)
    * [Build Nshead service](docs/cn/nshead_service.md)
    * [Debug server issues](docs/cn/server_debugging.md)
    * [Server push](docs/cn/server_push.md)
    * [Avalanche](docs/cn/avalanche.md)
    * [Live streaming](docs/cn/live_streaming.md)
    * [json2pb](docs/cn/json2pb.md)
  * [Builtin Services](docs/cn/builtin_service.md)
    * [status](docs/cn/status.md)
    * [vars](docs/cn/vars.md)
    * [connections](docs/cn/connections.md)
    * [flags](docs/cn/flags.md)
    * [rpcz](docs/cn/rpcz.md)
    * [cpu_profiler](docs/cn/cpu_profiler.md)
    * [heap_profiler](docs/cn/heap_profiler.md)
    * [contention_profiler](docs/cn/contention_profiler.md)
  * Tools
    * [rpc_press](docs/cn/rpc_press.md)
    * [rpc_replay](docs/cn/rpc_replay.md)
    * [rpc_view](docs/cn/rpc_view.md)
    * [benchmark_http](docs/cn/benchmark_http.md)
    * [parallel_http](docs/cn/parallel_http.md)
  * Others
    * [IOBuf](docs/cn/iobuf.md)
    * [Streaming Log](docs/cn/streaming_log.md)
    * [FlatMap](docs/cn/flatmap.md)
    * [brpc外功修炼宝典](docs/cn/brpc_intro.pptx)(新人培训材料)
  * RPC in depth
    * [New Protocol](docs/cn/new_protocol.md)
    * [Atomic instructions](docs/cn/atomic_instructions.md)
    * [IO](docs/cn/io.md)
    * [Threading Overview](docs/cn/threading_overview.md)
    * [Load Balancing](docs/cn/load_balancing.md)
    * [Locality-aware](docs/cn/lalb.md)
    * [Consistent Hashing](docs/cn/consistent_hashing.md)
    * [Memory Management](docs/cn/memory_management.md)
    * [Timer keeping](docs/cn/timer_keeping.md)
    * [bthread_id](docs/cn/bthread_id.md)
  * Use cases inside Baidu
    * [百度地图api入口](docs/cn/case_apicontrol.md)
    * [联盟DSP](docs/cn/case_baidu_dsp.md)
    * [ELF学习框架](docs/cn/case_elf.md)
    * [云平台代理服务](docs/cn/case_ubrpc.md)
