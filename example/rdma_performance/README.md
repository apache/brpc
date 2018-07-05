This example shows how to use RDMA in brpc.
It is also an evaluation to the performance of RDMA.
You can run this example in your own environment.

# How to build it

To build this example, please make sure you have built brpc with BRPC_WITH_RDMA option first.

```bash
$ cmake -DBRPC_WITH_RDMA=on . # in project's directory
$ make -j
$ cd example/rdma_performance
$ cmake .
$ make -j
```

Then there will be two executable binaries in example/rdma_performance: perf_server and perf_client.

# How to use it

Please run perf_server in one or more servers by using:

```bash
perf_server --port=PORT
```

The 'port' flag is not required. If you do not set it explicitly, the perf_server will use port number 8002.

After that, run perf_client in one or more servers by using:

```bash
perf_client --servers=IP:PORT+IP:PORT+IP:PORT \
            --thread_num=THREADS \
            --attachment_size=SIZE \
            --echo_attachment=(true|false) \
            --test_seconds=SECONDS
```

The 'servers' flag includes one or more IP:PORT to specify the perf_server processes used. Different IP:PORT should be separated by '+'.
The 'thread_num' flag is the number of threads (connections/QPs) to use. Different threads choose different perf_server randomly.
The 'attachment_size' flag is the size (in KB) of attachment in the rpc message.
The 'echo_attachment' flag specifies whether to echo the attachment back from the perf_servers.
The 'test_seconds' flag is the test duration.

After the test, the avarage and long-tail latency, throughput and CPU consumption of perf_server will be displayed at standard output.

# Advanced usage

You can tune the memory pool size with the following flags in case that there is a warning like 'Fail to extend new region'.
* The 'rdma_memory_pool_initial_size_mb' flag is to change the initial size of memory pool.
* The 'rdma_memory_pool_increase_size_mb' flag is to change the increased size when there is no spared memory in the pool.
* The 'rdma_memory_pool_max_regions' flag is to specify how many times the memory pool can be extended.
Remember if the memory pool is not enough and cannot be extended, the process will not exit.
Instead, brpc tries to register memory every time, which makes performance degrade dramatically.

You can also try polling mode as well by set the flags 'rdma_cq_num', 'rdma_use_polling', 'rdma_use_inplace', 'rdma_cq_offset'.
More details of these flags, including default values, can be displayed if you use '--help'.

If you try to use 'specify_attachment_addr' flag, please make sure you have turned on the 'IOBUF_WITH_HUGE_BLOCK' option in cmake.

You can also run TCP as a comparison. You should only use the flag '--use_rdma=false' both on perf_servers (optional) and perf_clients (required).

# Known problems

Currently, brpc cannot support multiple active RDMA ports very well.
Therefore, if your servers has two or more active RDMA ports, please disable extra ones or make them bonding together.
