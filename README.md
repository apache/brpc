[中文版](README_cn.md)

[![Linux Build Status](https://github.com/apache/brpc/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/apache/brpc/actions/workflows/ci-linux.yml)
[![MacOs Build Status](https://github.com/apache/brpc/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/apache/brpc/actions/workflows/ci-macos.yml)

![brpc logo (light)](docs/images/logo.png#gh-light-mode-only)
![brpc logo (dark)](docs/images/logo-white.png#gh-dark-mode-only)

[bRPC](https://brpc.apache.org/) is an Industrial-grade RPC framework using C++ Language, which is often used in  high performance system such as Search, Storage, Machine learning, Advertisement, Recommendation etc.

### "bRPC" means "better RPC". 

You can use it to:
* Build a server that can talk in multiple protocols (**on same port**), or access all sorts of services
  * restful http/https, [h2](https://httpwg.org/specs/rfc9113.html)/[gRPC](https://grpc.io). using http/h2 in bRPC is much more friendly than [libcurl](https://curl.haxx.se/libcurl/). Access protobuf-based protocols with HTTP/h2+json, probably from another language.
  * [redis](docs/en/redis_client.md) and [memcached](docs/en/memcache_client.md), thread-safe, more friendly and performant than the official clients.
  * [rtmp](https://github.com/apache/brpc/blob/master/src/brpc/rtmp.h)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), for building [streaming services](https://github.com/brpc/media-server).
  * hadoop_rpc (may be opensourced)
  * [rdma](https://en.wikipedia.org/wiki/Remote_direct_memory_access) support
  * [thrift](docs/en/thrift.md) support,  thread-safe, more friendly and performant than the official clients.
  * all sorts of protocols used in Baidu: [baidu_std](docs/en/baidu_std.md), [streaming_rpc](docs/en/streaming_rpc.md), hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc and nshead-based ones.
  * Build [HA](https://en.wikipedia.org/wiki/High_availability) distributed services using an industrial-grade implementation of [RAFT consensus algorithm](https://raft.github.io) which is opensourced at [braft](https://github.com/brpc/braft)
* Servers can handle requests [synchronously](docs/en/server.md) or [asynchronously](docs/en/server.md#asynchronous-service).
* Clients can access servers [synchronously](docs/en/client.md#synchronus-call), [asynchronously](docs/en/client.md#asynchronous-call), [semi-synchronously](docs/en/client.md#semi-synchronous-call), or use [combo channels](docs/en/combo_channel.md) to simplify sharded or parallel accesses declaratively.
* Debug services [via http](docs/en/builtin_service.md), and run  [cpu](docs/en/cpu_profiler.md), [heap](docs/en/heap_profiler.md) and [contention](docs/en/contention_profiler.md) profilers.
* Get [better latency and throughput](docs/en/overview.md#better-latency-and-throughput).
* [Extend bRPC](docs/en/new_protocol.md) with the protocols used in your organization quickly, or customize components, including [naming services](docs/en/load_balancing.md) (dns, zk, etcd), [load balancers](docs/en/load_balancing.md) (rr, random, consistent hashing)

# Try it!

* Read [overview](docs/en/overview.md) to know where bRPC can be used and its advantages.
* Read [getting started](docs/en/getting_started.md) for building steps and play with [examples](https://github.com/apache/brpc/tree/master/example/).
* Docs:
  * [Performance benchmark](docs/en/benchmark.md)
  * [bvar](docs/en/bvar.md)
    * [bvar_c++](docs/en/bvar_c++.md)
  * [bthread](docs/en/bthread.md)
    * [bthread or not](docs/en/bthread_or_not.md)
    * [thread-local](docs/en/thread_local.md)
    * [Execution Queue](docs/en/execution_queue.md)
    * [bthread tracer](docs/en/bthread_tracer.md)
    * [bthread tagged task group](docs/en/bthread_tagged_task_group.md)
  * Client
    * [Basics](docs/en/client.md)
    * [Error code](docs/en/error_code.md)
    * [Combo channels](docs/en/combo_channel.md)
    * [Access http/h2](docs/en/http_client.md)
    * [Access gRPC](docs/en/http_derivatives.md#h2grpc)
    * [Access thrift](docs/en/thrift.md#client-accesses-thrift-server)
    * [Access UB](docs/en/ub_client.md)
    * [Streaming RPC](docs/en/streaming_rpc.md)
    * [Access redis](docs/en/redis_client.md)
    * [Access memcached](docs/en/memcache_client.md)
    * [Backup request](docs/en/backup_request.md)
    * [Dummy server](docs/en/dummy_server.md)
  * Server
    * [Basics](docs/en/server.md)
    * [Serve http/h2](docs/en/http_service.md)
    * [Serve gRPC](docs/en/http_derivatives.md#h2grpc)
    * [Serve thrift](docs/en/thrift.md#server-processes-thrift-requests)
    * [Serve Nshead](docs/en/nshead_service.md)
    * [Debug server issues](docs/en/server_debugging.md)
    * [Server push](docs/en/server_push.md)
    * [Avalanche](docs/en/avalanche.md)
    * [Auto ConcurrencyLimiter](docs/en/auto_concurrency_limiter.md)
    * [Media Server](https://github.com/brpc/media-server)
    * [json2pb](docs/en/json2pb.md)
  * [Builtin Services](docs/en/builtin_service.md)
    * [status](docs/en/status.md)
    * [vars](docs/en/vars.md)
    * [connections](docs/en/connections.md)
    * [flags](docs/en/flags.md)
    * [rpcz](docs/en/rpcz.md)
    * [cpu_profiler](docs/en/cpu_profiler.md)
    * [heap_profiler](docs/en/heap_profiler.md)
    * [contention_profiler](docs/en/contention_profiler.md)
  * Tools
    * [rpc_press](docs/en/rpc_press.md)
    * [rpc_replay](docs/en/rpc_replay.md)
    * [rpc_view](docs/en/rpc_view.md)
    * [benchmark_http](docs/en/benchmark_http.md)
    * [parallel_http](docs/en/parallel_http.md)
  * Others
    * [IOBuf](docs/en/iobuf.md)
    * [Streaming Log](docs/en/streaming_log.md)
    * [FlatMap](docs/en/flatmap.md)
    * [Coroutine](docs/en/coroutine.md)
    * [Circuit Breaker](docs/en/circuit_breaker.md)
    * [RDMA](docs/en/rdma.md)
    * [Bazel Support](docs/en/bazel_support.md)
    * [Wireshark baidu_std dissector plugin](docs/en/wireshark_baidu_std.md)
    * [bRPC introduction](docs/cn/brpc_intro.pptx)(training material)
    * [A tutorial on building large-scale services](docs/en/tutorial_on_building_services.pptx)(training material)
    * [bRPC internal](docs/en/brpc_internal.pptx)(training material)
  * RPC in depth
    * [New Protocol](docs/en/new_protocol.md)
    * [Atomic instructions](docs/en/atomic_instructions.md)
    * [IO](docs/en/io.md)
    * [Threading Overview](docs/en/threading_overview.md)
    * [Load Balancing](docs/en/load_balancing.md)
    * [Locality-aware](docs/en/lalb.md)
    * [Consistent Hashing](docs/en/consistent_hashing.md)
    * [Memory Management](docs/en/memory_management.md)
    * [Timer keeping](docs/en/timer_keeping.md)
    * [bthread_id](docs/en/bthread_id.md)
  * Use cases
    * [User cases](community/cases.md)

# Contribute code
Please refer to [here](CONTRIBUTING.md).

# Feedback and Getting involved
* Report bugs, ask questions or give suggestions by [Github Issues](https://github.com/apache/brpc/issues)
* Subscribe to the mailing list(dev-subscribe@brpc.apache.org) to get updated with the project

# Code of Conduct
We follow the code of conduct from Apache Software Foundation, please refer it here [Link](https://www.apache.org/foundation/policies/conduct)
