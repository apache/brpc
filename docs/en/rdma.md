# Build

Since RDMA requires driver and hardware support, only the build on linux is verified.

With config_brpc：
```bash
sh config_brpc.sh --with-rdma
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

# Basic Implementation

brpc uses RDMA RC mode. Every Socket has its own QP. Before establishing RDMA connection, a TCP connection is necessary to exchange some information such as GID and QPN. The TCP connection will keep in EST state but not be used for data transmission after RDMA connection is established. Once the TCP connection is closed, the corresponding RDMA connection will be set error.

All the memory used for data transmission in RDMA must be registered, which is very inefficient. Generally, a memory pool is employed to avoid frequent memory registration. In fact, brpc uses IOBuf for data transmission. In order to realize total zerocopy and compatibility with IOBuf, the memory used by IOBuf is taken over by the RDMA memory pool. Since IOBuf buffer cannot be controlled by user directly, the total memory consumption in IOBuf should be carefully managed. It is suggested that the application registers enough memory at one time according to its requirement.

# Parameters

Congifurable parameterss：
* rdma_trace_verbose: to print RDMA connection information in log，default is false
* rdma_recv_zerocopy: enable zero copy in receive side，default is true
* rdma_zerocopy_min_size: the min message size for receive zero copy (in Byte)，default is 512
* rdma_recv_block_type: the block type used for receiving, can be default(8KB)/large(64KB)/huge(2MB)，default is default
* rdma_prepared_qp_size: the size of QP created at the begining of the application，default is 128
* rdma_prepared_qp_cnt: the number of QPs created at the begining of the application，default is 1024
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
