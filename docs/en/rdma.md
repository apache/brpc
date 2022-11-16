# Build

Since RDMA requires driver and hardware support, only the build on linux is verified.

With config_brpc：
```bash
sh config_brpc.sh --with-rdma --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
make

cd example/rdma_performance  # example for rdma
make
```

With cmake：
```bash
mkdir bld && cd bld && cmake -DWITH_RDMA=ON ..
make

cd example/rdma_performance  # example for rdma
mkdir bld && cd bld && cmake ..
make
```

With bazel:
```bash
# Server
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_server
# Client
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_client
```

# Basic Implementation

RDMA does not use socket API like TCP. However, the brpc::Socket class is still used. If a user sets ChannelOptions.use_rdma or ServerOptions.use_rdma to true, the Socket class created has RdmaEndpoint (see src/brpc/rdma/rdma_endpoint.cpp). When RDMA is enabled, the data which need to transmit will be posted to RDMA QP with verbs API, not written to TCP fd. For data receiving, RdmaEndpoint will get completions from RDMA CQ with verbs API (the event will be generated from a dedicated fd and be added into EventDispatcher, the handling function is RdmaEndpoint::PollCq) before parsing RPC messages with InputMessenger.

brpc uses RDMA RC mode. Every RdmaEndpoint has its own QP. Before establishing RDMA connection, a TCP connection is necessary to exchange some information such as GID and QPN. We call this procedure handshake. Since handshake needs TCP connection, the TCP fd in the corresponding Socket is still valid. The handshake procedure is completed in the AppConnect way in brpc. The TCP connection will keep in EST state but not be used for data transmission after RDMA connection is established. Once the TCP connection is closed, the corresponding RDMA connection will be set error.

The first key feature in RdmaEndpoint data transmission is zero copy. All data which need to transmit is in the Blocks of IOBuf. Thus all the Blocks need to be released after the remote side completes the receiving. The reference of these Blocks are stored in RdmaEndpoint::_sbuf. In order to realize receiving zero copy, the receive side must post receive buffers in Blocks of IOBuf, which are stored in RdmaEndpoint::_rbuf. Note that all the Blocks posted in the receive side has a fixed size (recv_block_size). The transmit side can only send message smaller than that. Otherwise the receive side cannot receive data successfully.

The second key feature in RdmaEndpoint data transmission is sliding window flow control. The flow control is to avoid fast transmit side overwhelming slow receive side. TCP has similar mechanism in kernel TCP stack. RdmaEndpoint implements this mechanism with explicit ACKs from receive side. to reduce the overhead of ACKs, the ACK number can be piggybacked in ordinary data message as immediate data.

The third key feature in RdmaEndpoint data transmission is event suppression. The size of every message is limited to recv_block_size (default is 8KB). If every message will generate an event, the performance will be very poor, even worse than TCP (TCP has GSO/GRO). Therefore, RdmaEndpoint set solicited flag for every message according to data size, window and ACKS. The flag can control whether to generate an event in remove side or not.

All the memory used for data transmission in RDMA must be registered, which is very inefficient. Generally, a memory pool is employed to avoid frequent memory registration. In fact, brpc uses IOBuf for data transmission. In order to realize total zerocopy and compatibility with IOBuf, the memory used by IOBuf is taken over by the RDMA memory pool (see src/brpc/rdma/block_pool.cpp). Since IOBuf buffer cannot be controlled by user directly, the total memory consumption in IOBuf should be carefully managed. It is suggested that the application registers enough memory at one time according to its requirement.

The application can manage memory by itself and send data with IOBuf::append_user_data_with_meta. In this case, the application should register memory by itself with rdma::RegisterMemoryForRdma (see src/brpc/rdma/rdma_helper.h). Note that RegisterMemoryForRdma returns the lkey for registered memory. Please provide this lkey with data together when calling append_user_data_with_meta.

RDMA is hardware-related. It has some different concepts such as device, port, GID, LID, MaxSge and so on. These parameters can be read from NICs at initialization, and brpc will make the default choice (see src/brpc/rdma/rdma_helper.cpp). Sometimes the default choice is not the expectation, then it can be changed in the flag way.

# Parameters

Configurable parameters:
* rdma_trace_verbose: to print RDMA connection information in log，default is false
* rdma_recv_zerocopy: enable zero copy in receive side，default is true
* rdma_zerocopy_min_size: the min message size for receive zero copy (in Byte)，default is 512
* rdma_recv_block_type: the block type used for receiving, can be default(8KB)/large(64KB)/huge(2MB)，default is default
* rdma_prepared_qp_size: the size of QPs created at the beginning of the application，default is 128
* rdma_prepared_qp_cnt: the number of QPs created at the beginning of the application，default is 1024
* rdma_max_sge: the max length of sglist, default is 0, which is the max length allowed by the device
* rdma_sq_size: the size of SQ，default is 128
* rdma_rq_size: the size of RQ，default is 128
* rdma_cqe_poll_once: the number of CQE pooled from CQ once，default is 32
* rdma_gid_index: the index of local GID table used，default is -1，which is the maximum GID index
* rdma_port: the port number used，default is 1
* rdma_device: the IB device name，default is empty，which is the first active device
* rdma_memory_pool_initial_size_mb: the initial region size of RDMA memory pool (in MB)，default is 1024
* rdma_memory_pool_increase_size_mb: the step increase region size of RDMA memory pool (in MB)，default is 1024
* rdma_memory_pool_max_regions: the max number of regions in RDMA memory pool，default is 16
* rdma_memory_pool_buckets: the number of buckets for avoiding mutex contention in RDMA memory pool，default is 4
* rdma_memory_pool_tls_cache_num: the number of thread local cached blocks in RDMA memory pool，default is 128
