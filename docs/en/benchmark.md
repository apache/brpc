NOTE: following tests were done in 2015, which may not reflect latest status of the package.

# Preface

Under the premise of multi-core, performance and threads are closely linked. The jump between threads has a decisive effect on the performance of high-frequency IO operations: a jump means a delay of at least 3-20 microseconds, because the L1 cache of each core is independent (our cpu L2 cache is also independent), This is followed by a large number of cache misses. The read and write delays of some variables will increase from nanoseconds to microseconds by hundreds of times: waiting for the CPU to synchronize the corresponding cachelines. Sometimes this brings an unexpected result. When each processing is short, a multi-threaded program may not be faster than a single-threaded program. Because the former may only do a little "business" every time after paying a large switching price, while the latter is constantly doing "business". However, single threading also has a price. The premise for it to work well is that "businesses" are fast, otherwise, once a certain time becomes slow, all subsequent "businesses" will be delayed. In some programs with generally short processing time, using (multiple disjoint) single threads can "do business" to the greatest extent. Since the processing time of each request is determined, the delay performance is also very stable. Various http servers That's it. But what our search service needs to do is much more complicated. There are a large number of back-end services that need to be accessed. The widespread long-tail requests make it impossible to determine the processing time each time, and the sorting strategy is becoming more and more complicated. If you still use (multiple disjoint) single threads, an unpredictable performance jitter, or a large request may cause a subsequent bunch of requests to be delayed.

In order to avoid mutual influence between requests, the request-level thread jump is the price brpc must pay. What we can do is to make [thread jump optimization](io.md#the-full-picture). However, the performance test of the service does not reflect this well. The processing in the test is often extremely simple, making the impact of thread switching unprecedentedly huge. By controlling the ratio of multi-threaded and single-threaded processing, we can manipulate the qps of a test service from 1 million to 5 million (same machine) freely, which damages The credibility of the performance test results. You must know that the real service is not accumulating a number or echoing a string. An echo program with several million qps is not instructive. In view of this, one year after launching the performance test (at the end of 15th), after brpc has undergone more than 1,200 changes, we need to review all the tests and strengthen the threading factor to obtain results that have a clear meaning to the real scene. Specifically:

-Requests should not be of equal length, but have a long tail. This can examine whether RPC can make requests concurrent, otherwise a slow request will affect a large number of subsequent requests.
-There must be a multi-level server scenario. In the server, the client is used to access the downstream server, which can examine the comprehensive performance of the server and the client.
-There must be a scenario where one client accesses multiple servers. This can be used to determine whether the load balancing is sufficiently concurrent. In a real scenario, a client rarely accesses only one server.

We hope that this set of test scenarios can be useful for performance testing of other services.

# Test target

## UB

The RPC framework developed by Baidu in 2008 is widely used in Baidu's product line and has been replaced by brpc. Each request of UB occupies a connection (connection pool). In large-scale services, each machine needs to maintain a large number of connections, which limits its usage scenarios. Distributed systems like Baidu do not use UB. UB only supports the nshead+mcpack protocol, and does not consider the scalability, so adding new protocols and new functions often requires adjusting a large section of code. In practice, most people "retire". UB lacks debugging and operation and maintenance interfaces, and the running status of the service is basically a black box for users. It can only track problems by inefficient logging. When service problems occur, maintainers are often required to investigate together, which is very inefficient. There are multiple variants of UB:

* ubrpc: Baidu developed the RPC framework based on UB in 10 years, using .idl files (similar to .proto) to describe the data schema instead of manual packaging. This RPC is used, but not widely.

-nova_pbrpc: The RPC framework developed by the Baidu Net Alliance team based on UB in 12 years used protobuf instead of mcpack as the serialization method. The protocol is nshead + user's protobuf.
-public_pbrpc: The RPC framework developed by Baidu based on UB in early 2013 used protobuf instead of mcpack as the serialization method, but the protocol is different from nova_pbrpc, roughly nshead + meta protobuf. There is a string field in meta protobuf that contains user's protobuf. Since user data has to be serialized twice, the performance of this RPC is very poor and has not been promoted.

We use nova_pbrpc, which is widely used in the Baidu Network Alliance team, as the representative of UB. The code is r10500 at the time of testing. Early UB supports CPOOL and XPOOL, using [select](http://linux.die.net/man/2/select) and [leader-follower model](http://kircher-schwanninger.de/michael/ publications/lf.pdf), and later provided EPOLL, using [epoll](http://man7.org/linux/man-pages/man7/epoll.7.html) to handle multiple connections. In view of the fact that most of the product line uses EPOLL model, our UB configuration also uses EPOLL. UB only supports [connection pool] (client.md#connection mode), and the result is referred to by "**ubrpc_mc**" (mc stands for "multiple
connection"). Although this name is not accurate (see the introduction to ubrpc above), in the context of this article, please default ubrpc = UB.

## hulu-pbrpc

Baidu implemented the RPC framework based on saber (kylin variant) and protobuf in 13 years. Hulu has many problems in multi-threaded implementation and has been replaced by brpc. The code is `pbrpc_2-0-15-27959_PD_BL` during testing. hulu-pbrpc only supports single connection, the result is referred to by "**hulu-pbrpc**".

## brpc

The rpc product developed by INF at the end of 2014 supports all protocols in Baidu (not limited to protobuf), and unified the RPC framework of Baidu's main distributed systems and business lines for the first time. The code for testing is r31906. brpc supports both single connection and connection pool. The result of the former is referred to as "**baidu-rpc**", and the result of the latter is referred to as "**baidu-rpc_mc**".

## sofa-pbrpc

The Baidu Dasou team implemented the RPC framework based on boost::asio and protobuf in 13 years. There are multiple versions. After consulting relevant students, I confirmed that the ps/opensource and github are newer and will be synchronized regularly. Therefore, the test uses the version under ps/opensource. The code for testing is `sofa-pbrpc_1-0-2_BRANCH`. sofa-pbrpc only supports single connection, the result is referred to as "**sofa-pbrpc**".

## apache thrift

Thrift is a serialization method and rpc framework first developed by Facebook in 2007. It includes a unique serialization format and IDL, and supports many programming languages. Renamed [apache thrift](https://thrift.apache.org/) after open source, fb has a [fbthrift branch](https://github.com/facebook/fbthrift), we use apache thrift. The code for testing is `thrift_0-9-1-400_PD_BL`. The disadvantages of thrift are: The code seems to be layered and clear. There are many choices of client and server, but none of them are universal enough. Each server implementation can only solve a small part of the scene. Each client is not thread-safe and it is very troublesome to use in practice. Since thrift does not have a thread-safe client, a client must be established in each thread and use an independent connection. In the test, thrift actually took advantage of other implementations: its client does not need to deal with multi-threading issues. The result of thrift is referred to as "**thrift_mc**".

## gRPC

The rpc framework developed by Google uses http/2 and protobuf 3.0, and its code is <https://github.com/grpc/grpc/tree/release-0_11> when tested. gRPC is not stubby, positioning is more like promoting http/2 and protobuf 3.0, but since many people are very interested in its performance, we also (very troublesome) added it. The result of gRPC is referred to as "**grpc**".

# Test Methods

As explained in the preamble, there is huge room for adjustment in performance figures. The key here is what is our bottom line requirement for RPC. If we break away from this bottom line, the performance in the test will seriously deviate from the real environment.

The bottom line is that **RPC must be able to handle long tail**.

In the Baidu environment, this is a vernacular. Which product line and system does not have a long tail? As the RPC framework that carries most of the services, it must naturally handle the long tail and reduce the impact of the long tail on normal requests. But at the implementation level, this problem has too much influence on the design. If there is no long tail in the test, then the RPC implementation can assume that each request is about the same speed. At this time, the optimal method is to use multiple threads to process the requests independently. Because there is no context switching and cache consistency synchronization, the performance of the program will be significantly higher than the performance when multiple threads cooperate.

For example, a simple echo program only needs 200-300 nanoseconds to process a request, and a single thread can reach a throughput of 3-5 million. But if multiple threads cooperate, even in an extremely smooth system, you will have to pay a context switch cost of 3-5 microseconds and a cache synchronization cost of 1 microsecond. This has not considered other mutual exclusion logic between multiple threads. Generally speaking, the throughput of a single thread is difficult to exceed 100,000. Even if all 24 cores are used up, the throughput is only 2.4 million, which is not as good as a thread. This is the reason why [single-threaded model](threading_overview.md#single-threaded reactor) is selected for the typical service of http server (multiple threads run eventloop independently): The processing time of most http requests is predictable, which is very important for downstream The access will not have any blocking code. This model can maximize cpu utilization while providing acceptable latency.

Multi-threading pays such a large price to **isolate the impact of requests**. A computationally complex or simply blocking process will not affect other requests, and a 1% long tail will only affect 1% of performance in the end. However, multiple independent threads cannot guarantee this. A request enters a thread is equivalent to "fixed for a lifetime". If the previous request slows down a bit, it can only be slowed down. A long tail of 1% affects far more than 1% of requests, resulting in poor performance. In other words, at first glance, the multi-threaded model is "slow", but in real applications, it will achieve better overall performance.

The delay can accurately reflect the interference effect of the long tail. If the delay of the ordinary request is not interfered by the long tail request, it means that the RPC has successfully isolated the request. QPS cannot reflect this. As long as the CPU is busy, even if a normal request enters the queue full of long tails and is severely delayed, the final QPS will not change much. In order to measure the interference effect of the long tail, we added 1% of the long tail request in the test involving delay.

# start testing

## surroundings

The machine configuration used for the performance test is:

-Stand-alone 1: CPU with 24 cores with hyper-threading, E5-2620 @ 2.00GHz; 64GB memory; OS linux 2.6.32_1-15-0-0
-Multi-machine 1 (15 units + 8 units): CPUs are not enabled with hyper-threading 12 cores, and 15 of them have CPUs E5-2420 @ 1.90GHz., 64GB memory, Gigabit network card, and multi-queue cannot be enabled. The remaining 8 units are E5-2620 2.0GHz, gigabit network cards, and bind multiple queues to the first 8 cores. These long-term test machines are rather complicated, spanning multiple computer rooms, and the ones with a delay of more than 1ms in the test are these machines.
-Multi-machine 2 (30 units): CPU with 12 cores without hyper-threading, E5-2620 v3 @ 2.40GHz.; 96GB memory; OS linux 2.6.32_1-17-0-0; 10 Gigabit network card, bind multiple queues to The first 8 cores. This is a new machine temporarily borrowed. The configuration is very good. They are all in the Guangzhou computer room. The delay is very short. It is this batch of machines that have a delay of several hundred microseconds in the test.

All the graphs below are drawn using the dashboard program developed by brpc. After removing the path, you can see all the brpc
The server is the same as [built-in service](builtin_service.md).

## Configuration

Unless otherwise specified, the configuration in all tests is only the number difference (thread number, request size, client number etc.), not the model difference. We ensure that the qps and delay seen by users are different dimensions of the same scene, rather than two scenes that cannot be unified.

All RPC servers are configured with 24 worker threads, which generally run user processing logic. Special instructions for each type of RPC:

-UB: Configured with 12 reactor threads, using the EPOOL model. The connection pool limit is configured as the number of threads (24)
-hulu-pbrpc: 12 additional IO threads are configured. These threads will handle fd reading, request parsing and other tasks. Hulu has a "shared queue" configuration item, which is not turned on by default. Its function is to statically hash fd into multiple threads. Since there is no more contention between threads, hulu's qps will be significantly improved, but it will obviously be long tailed. Impact (for the reason, see [TEST METHOD](#测试方法)). Considering that most users will not change the configuration, we also choose not to open it.
-thrift: 12 additional IO threads are configured. These threads will handle fd reading, request parsing and other tasks. Thrift's client does not support multi-threading. Each thread has to use an independent client, and the connection is also separated.
-sofa-pbrpc: According to the requirements of sofa students, configure io_service_pool_size to 24 and work_thread_num to 1. The approximate meaning is to use independent 24 groups of thread pools, each with 1 worker thread. Similar to when hulu does not open the "shared queue", this configuration will significantly improve the QPS of sofa-pbrpc, but at the same time it will lose its ability to handle long tails. If you use it in a real product, we do not recommend this configuration. (You should use io_service_pool_size=1, work_thread_num=24)
-brpc: Although the brpc client will get 10%~20% QPS improvement and lower latency when running in bthread, the clients in the test all run in a unified pthread.

All RPC clients are sent synchronously by multiple threads. This method is closest to the situation in the real system, and the delay factor is also taken into account when examining QPS.

A popular solution is that the client keeps writing data to the connection to see the server performance. The disadvantage of this method is that the server can read a large number of requests at once, and the competition of different RPCs has become a "for loop to execute user code". Competition, not the efficiency of distributing requests. In a real system, the server rarely reads more than 4 requests at the same time. This method also completely abandons the delay. The client actually enters the state when the server is caught in an avalanche, and all requests time out due to a large number of queues.

## The QPS of a single client on the same machine → a single server under different requests (the higher the better)

This test runs on [Single Machine 1] (#环境). The values ​​in the figure are the number of bytes of user data. The actual request size also includes the protocol header, which generally increases by about 40 bytes.

(X-axis is the number of bytes of user data, Y-axis is the corresponding QPS)

![img](../images/qps_vs_reqsize.png)

The curve ending in _mc represents that the client and server maintain multiple connections (number of threads), which will perform better in this test.

**analyze**

* brpc: When the request packet is less than 16KB, the throughput under a single connection exceeds the ubrpc_mc and thrift_mc of multiple connections. As the request packet becomes larger, the write speed of the kernel to a single connection becomes a bottleneck. The brpc under multi-connection reached the highest 2.3GB/s in the test. Note: Although the brpc using the connection pool has higher throughput when sending large packets, it will also consume more CPU (the same is true for UB and thrift). The single-connection brpc in the figure below can already provide a throughput of more than 800M, which is enough to fill a 10G network card, and the CPU used may only be 1/2 of the multi-link (the writing process is [wait-free](io. md#send a message)), please use the single link first in the real system.
* thrift: It is significantly lower than brpc in the initial stage, and surpasses the brpc of a single connection as the packet becomes larger.
* UB: A curve similar to thrift, but with an average of 40,000-50,000 QPS lower than the single-connection brpc in the 32K package. QPS hardly changed during the whole process.
* gRPC: At the beginning, it was almost parallel to UB, but it was about 10,000 lower, and it started to drop when it exceeded 8K.
* hulu-pbrpc and sofa-pbrpc: Before 512 bytes, it was higher than UB and gRPC, but after that, they fell sharply and successively came to the bottom. This trend is a sign of insufficient concurrency.

## Same machine single client→single server QPS under different thread counts (the higher the better)

This test runs on [Single Machine 1] (#环境).

(The X axis is the number of threads, and the Y axis is the corresponding QPS)

![img](../images/qps_vs_threadnum.png)

**analyze**

brpc: With the increase of sending threads, QPS is increasing rapidly, and it has good multi-thread scalability.

UB and thrift: 8 threads are higher than brpc, but after more than 8 threads, brpc is quickly surpassed, thrift continues to "shift", and UB has dropped significantly.

gRPC, hulu-pbrpc, sofa-pbrpc: almost overlap, 256 threads are only doubled compared to 1 thread, multi-threaded scalability is not good.

## The delay of a single client on the same machine → a single server under a fixed QPS [CDF] (vars.md#statistics and view quantile value) (the left the better, the straighter the better)
This test runs on [Single Machine 1] (#环境). Considering the processing capabilities of different RPCs, we chose a lower QPS that can be achieved in many systems: 10,000.

In this test, 1% of long-tail requests took 5 milliseconds. The delay of long-tail requests is not included in the result, because we are investigating whether ordinary requests are processed in time.

(X-axis is the delay (microseconds), Y-axis is the proportion of requests less than the X-axis delay)

![img](../images/latency_cdf.png)

**analyze**
-brpc: The average delay is short, almost unaffected by the long tail.
-UB and thrift: The average delay is 1 millisecond higher than brpc, which is not affected by the long tail.
-hulu-pbrpc: The direction is similar to UB and thrift, but the average delay is further increased by 1 millisecond.
-gRPC: It was good at the beginning, but it performed badly after arriving in the long tail area, and some of the requests directly timed out. (This is the case for repeated tests, like there are bugs)
-sofa-pbrpc: 30% of ordinary requests (not shown in the picture above) are severely disturbed by Long Tail.

## Cross-machine multi-client→single server QPS (the higher the better)

This test is run on [多机1](#环境).

(X-axis is the number of clients, Y-axis is the corresponding QPS)

![img](../images/qps_vs_multi_client.png)

**analyze**
* brpc: With the increase of cilent, the QPS of the server is increasing rapidly, and there is good client scalability.
* sofa-pbrpc: As the client increases, the QPS of the server also increases rapidly, but the magnitude is not as good as that of brpc, and the client scalability is also good. The improvement from 16 clients to 32 clients is small.
* hulu-pbrpc: As the client increases, the QPS of the server increases, but the magnitude is further smaller than that of sofa-pbrpc.
* UB: Increasing the client can hardly increase the QPS of the server.
* thrift: The average QPS is lower than UB, and increasing the client can hardly increase the QPS of the server.
* gRPC: Bottom, increasing the client can hardly increase the QPS of the server.

## Cross-machine multi-client→single server delay under fixed QPS [CDF] (vars.md#statistics and view quantile value) (the left the better, the straighter the better)

This test is run on [多机1](#环境). The load balancing algorithm is provided by default by round-robin or RPC. Since there are 32 clients and some RPC single clients have poor capabilities, we only set 2500QPS for each client, which is a number that can be achieved by a real business system.

In this test, 1% of long-tail requests took 15 milliseconds. The delay of long-tail requests is not included in the result, because we are investigating whether ordinary requests are processed in time.

(X-axis is the delay (microseconds), Y-axis is the proportion of requests less than the X-axis delay)

![img](../images/multi_client_latency_cdf.png)

**analyze**
-brpc: The average delay is short, almost unaffected by the long tail.
-UB and thrift: Short average delay, less affected by long tail, average delay higher than brpc
-sofa-pbrpc: 14% of ordinary requests are severely interfered by Long Tail.
-hulu-pbrpc: 15% of ordinary requests are severely interfered by Nagao.
-gRPC: It's completely out of control, very bad.

## Cross-machine multi-client→multi-server delay under fixed QPS [CDF] (vars.md#statistics and view quantile value) (the left the better, the straighter the better)

This test is run on [多机2](#环境). Each of 20 computers runs 4 clients, and multi-threaded access to 10 servers simultaneously. The load balancing algorithm is provided by default by round-robin or RPC. Because gRPC accesses multiple servers is troublesome and there is a high probability that it still performs poorly, this test does not include gRPC.

In this test, 1% of long-tail requests took 10 milliseconds. The delay of long-tail requests is not included in the result, because we are investigating whether ordinary requests are processed in a timely manner.

(X-axis is the delay (microseconds), Y-axis is the proportion of requests less than the X-axis delay)

![img](../images/multi_server_latency_cdf.png)

**analyze**
-brpc and UB: The average delay is short, almost unaffected by the long tail.
-thrift: The average delay is significantly higher than brpc and UB.
-sofa-pbrpc: 2.5% of ordinary requests are severely disturbed by Nagao.
-hulu-pbrpc: 22% of ordinary requests are severely disturbed by Nagao.

## Cross-machine multi-client→multi-server→multi-server delay under fixed QPS [CDF](vars.md#statistics and view quantile value) (the left the better, the straighter the better)

This test is run on [多机2](#环境). Each of the 14 servers runs 4 clients, and multi-threaded accesses 8 servers synchronously, and these servers also synchronously access another 8 servers. The load balancing algorithm is provided by default by round-robin or RPC. Because gRPC accesses multiple servers is troublesome and there is a high probability that it still performs poorly, this test does not include gRPC.

In this test, 1% of long-tail requests took 10 milliseconds. The delay of long-tail requests is not included in the result, because we are investigating whether ordinary requests are processed in a timely manner.

(X-axis is the delay (microseconds), Y-axis is the proportion of requests less than the X-axis delay)

![img](../images/twolevel_server_latency_cdf.png)

**analyze**
-brpc: The average delay is short, almost unaffected by the long tail.
-UB: The average delay is short, and the long tail area is slightly worse than brpc.
-thrift: The average delay is significantly higher than brpc and UB.
-sofa-pbrpc: 17% of ordinary requests are severely interfered by the long tail, and 2% of the requests have extremely long delays.
-hulu-pbrpc: Basically disappeared from the field of vision, and can no longer work normally.

# in conclusion

brpc: Excellent performance in throughput, average delay, and long tail processing.

UB: The average latency and long-tail processing performance are good, and the throughput scalability is poor. Increasing the number of threads and clients can hardly increase the throughput.

thrift: The average delay and throughput of a single machine is acceptable, and the average delay of multiple machines is significantly higher than that of brpc and UB. The scalability of throughput is poor, and increasing the number of threads and clients can hardly increase throughput.

sofa-pbrpc: The throughput of processing small packets is acceptable, the throughput of large packets is significantly lower than other RPCs, and the delay is greatly affected by the long tail.

hulu-pbrpc: The performance of a single machine is similar to that of sofa-pbrpc, but the delay performance of multiple machines is extremely poor.

gRPC: Almost at the bottom of all participating tests, it may be positioned to provide users of the google cloud platform with a multi-language, network-friendly implementation, and performance is not a priority.
