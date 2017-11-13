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
* Establishment and re-using of connections is transparent to users, but users can make choices like [different connection types](docs/en/client.md#connection-type): short, pooled, single.
* Machines are discovered by a Naming Service, which can be implemented by [DNS](https://en.wikipedia.org/wiki/Domain_Name_System), [ZooKeeper](https://zookeeper.apache.org/) or [etcd](https://github.com/coreos/etcd). Inside Baidu, we use BNS (Baidu Naming Service). brpc provides ["list://" and "file://"](docs/en/client.md#naming-service) as well. Users specify load balancing algorithms to choose one machine for each request from all machines, including: round-robin, randomized, [consistent-hashing](docs/cn/consistent_hashing.md)(murmurhash3 or md5) and [locality-aware](docs/cn/lalb.md).
* RPC retries when the connection is broken. When server does not respond within the given time, client fails with a timeout error.

# Where can I use RPC?

Almost all network communications.

RPC can't do everything surely, otherwise we don't need the layer of TCP/IP. But in most network communications, RPC meets requirements and isolates the underlying details. 

Common doubts on RPC:

- My data is binary and large, using protobuf will be slow. First, this is possibly a wrong feeling and you will have to test it and prove it with [profilers](docs/cn/cpu_profiler.md). Second, many protocols support carrying binary data along with protobuf requests and bypass the serialization.
- I'm sending streaming data which can't be processed by RPC. Actually many protocols in RPC can handle streaming data, including [ProgressiveReader in http](docs/en/http_client.md#progressively-download), streams in h2, [streaming rpc](docs/en/streaming_rpc.md), and RTMP which is a specialized streaming protocol.
- I don't need replies. With some inductions, we know that in your scenario requests can be dropped at any stage because the client is always unaware of the situation. Are you really sure this is acceptable? Even if you don't need the reply, we recommend sending back small-sized replies, which are unlikely to be performance bottlenecks and will probably provide valuable clues when debugging complex bugs. 
