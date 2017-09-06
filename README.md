![baidu_rpc_logo](docs/images/logo.png)

# What is RPC?

[RPC](http://en.wikipedia.org/wiki/Remote_procedure_call) abstracts the network communications as "clients access functions on servers": client sends a request to server, and wait until server receives -> processes -> responds the request, then do actions according to the result. RPC satisfies most network communication requirements as well as encapsulates the underlying details.

# What is baidu-rpc?

A RPC framework used throughout Baidu, with more than 600,000 instances. You can use it for:

* Build a server that can talk in multiple protocols (on same port), including:
  * http/https, h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced soon)
  * hadoop_rpc(not opensourced yet)
  * [rtmp](https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol)/[flv](https://en.wikipedia.org/wiki/Flash_Video)/[hls](https://en.wikipedia.org/wiki/HTTP_Live_Streaming), for building live-streaming services.
  * all sorts of protocols based on protobuf used in Baidu: baidu_std, hulu_pbrpc, [sofa_pbrpc](https://github.com/baidu/sofa-pbrpc), nova_pbrpc, public_pbrpc, ubrpc, and nshead-based ones.
* Access services in an unified way, including:
  * http (much more friendly than [libcurl](https://curl.haxx.se/libcurl/)), h2/h2c (compatible with [grpc](https://github.com/grpc/grpc), will be opensourced soon)
  * [redis](docs/cn/redis_client.md) and [memcached](docs/cn/memcache_client.md), thread-safe and more friendly and performant than the official clients
  * [rtmp](https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol)/[flv](https://en.wikipedia.org/wiki/Flash_Video), for building live-streaming services.
  * all sorts of protocols based on protobuf used in Baidu.
* Debug services [via http](docs/cn/builtin_service.md), and run online profilers.
* Get [better latency and throughput](docs/cn/benchmark.md).

Check out [Getting Started](docs/en/getting_started.md) or [开始使用](docs/cn/getting_started) for more information.
