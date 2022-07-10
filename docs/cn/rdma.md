# 编译

由于RDMA对驱动与硬件有要求，目前仅支持在Linux系统编译并运行RDMA功能。

使用config_brpc：
```bash
sh config_brpc.sh --with-rdma
make

cd example/rdma_performance  # 示例程序
make
```

使用cmake：
```bash
mkdir bld && cd bld && cmake -DWITH_RDMA=ON ..
make

cd example/rdma_performance  # 示例程序
mkdir bld && cd bld && cmake ..
make
```

# 基本实现

brpc内部使用RDMA RC模式，每个Socket对应一个QP。RDMA连接建立依赖于前置TCP建连，TCP建连后双方交换必要参数（含GID、QPN等），再发起RDMA连接并实现数据传输。建连用的TCP连接在RDMA连接活跃期间并不传输数据，但仍保持EST状态。一旦TCP连接中断，其上对应的RDMA连接同样会置错。

RDMA要求数据收发所使用的内存空间必须被注册（memory register），这一操作非常耗时，所以通常都会使用内存池方案来加速。brpc内部的数据收发都使用IOBuf，为了在兼容IOBuf的情况下实现完全零拷贝，整个IOBuf所使用的内存空间整体由统一内存池接管。注意，由于IOBuf内存池不由用户直接控制，因此实际使用中需要注意IOBuf所消耗的总内存，建议根据实际业务需求，一次性注册足够的内存池以实现性能最大化。

# 参数

可配置参数说明：
* rdma_trace_verbose: 日志中打印RDMA建连相关信息，默认false
* rdma_recv_zerocopy: 是否启用接收零拷贝，默认true
* rdma_zerocopy_min_size: 接收零拷贝最小的msg大小，默认512B
* rdma_recv_block_type: 为接收数据预准备的block类型，分为三类default(8KB)/large(64KB)/huge(2MB)，默认为default
* rdma_prepared_qp_size: 程序启动预生成的QP的大小，默认128
* rdma_prepared_qp_cnt: 程序启动预生成的QP的数量，默认1024
* rdma_max_sge: 允许的最大发送SGList长度，默认为0，即采用硬件所支持的最大长度
* rdma_sq_size: SQ大小，默认128
* rdma_rq_size: RQ大小，默认128
* rdma_cqe_poll_once: 从CQ中一次性poll出的CQE数量，默认32
* rdma_gid_index: 使用本地GID表中的Index，默认为-1，即选用最大的可用GID Index
* rdma_port: 使用IB设备的port number，默认为1
* rdma_device: 使用IB设备的名称，默认为空，即使用第一个active的设备
* rdma_memory_pool_initial_size_mb: 内存池的初始大小，单位MB，默认1024
* rdma_memory_pool_increase_size_mb: 内存池每次动态增长的大小，单位MB，默认1024
* rdma_memory_pool_max_regions: 最大的内存池块数，默认16
* rdma_memory_pool_buckets: 内存池中为避免竞争采用的bucket数目，默认为4
* rdma_memory_pool_tls_cache_num: 内存池中thread local的缓存block数目，默认为128
