[中文版](README_cn.md)

[![Build Status](https://travis-ci.org/brpc/brpc.svg?branch=master)](https://travis-ci.org/brpc/brpc)

# ![brpc](docs/images/logo.png)

A industrial-grade RPC framework used throughout [Baidu](http://ir.baidu.com/phoenix.zhtml?c=188488&p=irol-irhome), with **600,000+** instances(not counting clients) and **500+** kinds of services, called "**baidu-rpc**" inside Baidu. Only C++ implementation is opensourced right now.

You can use it to:
* Build a server that can talk in multiple protocols (**on same port**), or access all sorts of services
  * restful http/https, h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced). using http in brpc is much more friendly than [libcurl](https://curl.haxx.se/libcurl/).
  * [redis](docs/en/redis_client.md) and [memcached](docs/en/memcache_client.md), thread-safe, more friendly and performant than the official clients
  * [rtmp](https://github.com/brpc/brpc/blob/master/src/brpc/rtmp.h)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), for building [live-streaming services](docs/cn/live_streaming.md).
  * hadoop_rpc(not opensourced yet)
  * [rdma](https://en.wikipedia.org/wiki/Remote_direct_memory_access) support via [openucx](https://github.com/openucx/ucx) (will be opensourced)
  * all sorts of protocols used in Baidu: [baidu_std](docs/cn/baidu_std.md), [streaming_rpc](docs/en/streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc, and nshead-based ones.
  * Access protobuf-based protocols with HTTP+json, probably from another language.
  * Build [HA](https://en.wikipedia.org/wiki/High_availability) distributed services using an industrial-grade implementation of [RAFT consensus algorithm](https://raft.github.io) (will be opensourced at [braft](https://github.com/brpc/braft))
* Create rich processing patterns
  * Services can handle requests [synchronously](docs/en/server.md) or [asynchronously](docs/en/server.md#asynchronous-service).
  * Access service [synchronously](docs/en/client.md#synchronus-call) or [asynchronously](docs/en/client.md#asynchronous-call), or even [semi-synchronously](docs/en/client.md#semi-synchronous-call).
  * Use [combo channels](docs/en/combo_channel.md) to simplify complicated client patterns declaratively, including sharded and parallel accesses.
* Debug services [via http](docs/en/builtin_service.md), and run  [cpu](docs/cn/cpu_profiler.md), [heap](docs/cn/heap_profiler.md) and [contention](docs/cn/contention_profiler.md) profilers.
* Get [better latency and throughput](#better-latency-and-throughput).
* [Extend brpc](docs/en/new_protocol.md) with the protocols used in your organization quickly, or customize components, including [naming services](docs/cn/load_balancing.md#名字服务) (dns, zk, etcd), [load balancers](docs/cn/load_balancing.md#负载均衡) (rr, random, consistent hashing)

# Try it!

* Read [overview](docs/en/overview.md) to know where brpc can be used and its advantages.
* Read [building steps](docs/cn/getting_started.md) to get started and play with [examples](https://github.com/brpc/brpc/tree/master/example/).
* Docs:
  * [Performance benchmark](docs/cn/benchmark.md)
  * [bvar](docs/en/bvar.md)
    * [bvar_c++](docs/cn/bvar_c++.md)
  * [bthread](docs/cn/bthread.md)
    * [bthread or not](docs/cn/bthread_or_not.md)
    * [thread-local](docs/cn/thread_local.md)
    * [Execution Queue](docs/cn/execution_queue.md)
  * Client
    * [Basics](docs/en/client.md)
    * [Error code](docs/en/error_code.md)
    * [Combo channels](docs/en/combo_channel.md)
    * [Access HTTP](docs/en/http_client.md)
    * [Access UB](docs/cn/ub_client.md)
    * [Streaming RPC](docs/en/streaming_rpc.md)
    * [Access redis](docs/en/redis_client.md)
    * [Access memcached](docs/en/memcache_client.md)
    * [Backup request](docs/en/backup_request.md)
    * [Dummy server](docs/en/dummy_server.md)
  * Server
    * [Basics](docs/en/server.md)
    * [Build HTTP service](docs/en/http_service.md)
    * [Build Nshead service](docs/cn/nshead_service.md)
    * [Debug server issues](docs/cn/server_debugging.md)
    * [Server push](docs/en/server_push.md)
    * [Avalanche](docs/cn/avalanche.md)
    * [Live streaming](docs/cn/live_streaming.md)
    * [json2pb](docs/cn/json2pb.md)
  * [Builtin Services](docs/en/builtin_service.md)
    * [status](docs/en/status.md)
    * [vars](docs/en/vars.md)
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
    * [IOBuf](docs/en/iobuf.md)
    * [Streaming Log](docs/en/streaming_log.md)
    * [FlatMap](docs/cn/flatmap.md)
    * [brpc外功修炼宝典](docs/cn/brpc_intro.pptx)(新人培训材料)
  * RPC in depth
    * [New Protocol](docs/en/new_protocol.md)
    * [Atomic instructions](docs/en/atomic_instructions.md)
    * [IO](docs/en/io.md)
    * [Threading Overview](docs/en/threading_overview.md)
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

# Contribute code

brpc welcomes contributions, especially those on adapting different platforms and extending protocols.

Make sure the code meets following requirements before submitting your PR:

- Conforms to [google C++ coding style](https://google.github.io/styleguide/cppguide.html)
- The code appears where it should be. For example the code to support an extra protocol should not be put in general classes like server.cpp, channel.cpp, while a general modification would better not be hidden inside a very specific protocol.
- Has unittests.

Check following items after submitting the PR:

- Compilations and unittests in [travis-ci](https://travis-ci.org/brpc/brpc/pull_requests) are passed.
