# 编译

由于RDMA对驱动与硬件有要求，目前仅支持在Linux系统编译并运行RDMA功能。

使用config_brpc：
```bash
sh config_brpc.sh --with-rdma --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
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

使用bazel:
```bash
# Server
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_server
# Client
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_client
```

# 基本实现

RDMA与TCP不同，不使用socket接口进行通信。但是在实现上仍然复用了brpc中原本的Socket类。当用户选择ChannelOptions或ServerOptions中的use_rdma为true时，创建出的Socket类中则有对应的RdmaEndpoint（参见src/brpc/rdma/rdma_endpoint.cpp）。当RDMA被使能时，写入Socket的数据会通过RdmaEndpoint提交给RDMA QP（通过verbs API），而非拷贝到fd。对于数据读取，RdmaEndpoint中则调用verbs API从RDMA CQ中获取对应完成信息（事件获取有独立的fd，复用EventDispatcher，处理函数采用RdmaEndpoint::PollCq），最后复用InputMessenger完成RPC消息解析。

brpc内部使用RDMA RC模式，每个RdmaEndpoint对应一个QP。RDMA连接建立依赖于前置TCP建连，TCP建连后双方交换必要参数，如GID、QPN等，再发起RDMA连接并实现数据传输。这个过程我们称为握手（参见RdmaEndpoint）。因为握手需要TCP连接，因此RdmaEndpoint所在的Socket类中，原本的TCP fd仍然有效。握手过程采用了brpc中已有的AppConnect逻辑。注意，握手用的TCP连接在后续数据传输阶段并不会收发数据，但仍保持为EST状态。一旦TCP连接中断，其上对应的RDMA连接同样会置错。

RdmaEndpoint数据传输逻辑的第一个重要特性是零拷贝。要发送的所有数据默认都存放在IOBuf的Block中，因此所发送的Block需要等到对端确认接收完成后才可以释放，这些Block的引用被存放于RdmaEndpoint::_sbuf中。而要实现接收零拷贝，则需要确保接受端所预提交的接收缓冲区必须直接在IOBuf的Block里面，被存放于RdmaEndpoint::_rbuf。注意，接收端预提交的每一段Block，有一个固定的大小（recv_block_size）。发送端发送时，一个请求最多只能有这么大，否则接收端则无法成功接收。

RdmaEndpoint数据传输逻辑的第二个重要特性是滑动窗口流控。这一流控机制是为了避免发送端持续在发送，其速度超过了接收端处理的速度。TCP传输中也有类似的逻辑，但是是由内核协议栈来实现的。RdmaEndpoint内实现了这一流控机制，通过接收端显式回复ACK来确认接收端处理完毕。为了减少ACK本身的开销，让ACK以立即数形式返回，可以被附在数据消息里。

RdmaEndpoint数据传输逻辑的第三个重要特性是事件聚合。每个消息的大小被限定在一个recv_block_size，默认为8KB。如果每个消息都触发事件进行处理，会导致性能退化严重，甚至不如TCP传输（TCP拥有GSO、GRO等诸多优化）。因此，RdmaEndpoint综合考虑数据大小、窗口与ACK的情况，对每个发送消息选择性设置solicited标志，来控制是否在发送端触发事件通知。

RDMA要求数据收发所使用的内存空间必须被注册（memory register），把对应的页表映射注册给网卡，这一操作非常耗时，所以通常都会使用内存池方案来加速。brpc内部的数据收发都使用IOBuf，为了在兼容IOBuf的情况下实现完全零拷贝，整个IOBuf所使用的内存空间整体由统一内存池接管(参见src/brpc/rdma/block_pool.cpp)。注意，由于IOBuf内存池不由用户直接控制，因此实际使用中需要注意IOBuf所消耗的总内存，建议根据实际业务需求，一次性注册足够的内存池以实现性能最大化。

应用程序可以自己管理内存，然后通过IOBuf::append_user_data_with_meta把数据发送出去。在这种情况下，应用程序应该自己使用rdma::RegisterMemoryForRdma注册内存（参见src/brpc/rdma/rdma_helper.h）。注意，RegisterMemoryForRdma会返回注册内存对应的lkey，请在append_user_data_with_meta时以meta形式提供给brpc。

RDMA是硬件相关的通信技术，有很多独特的概念，比如device、port、GID、LID、MaxSge等。这些参数在初始化时会从对应的网卡中读取出来，并且做出默认的选择（参见src/brpc/rdma/rdma_helper.cpp）。有时默认的选择并非用户的期望，则可以通过flag参数方式指定。

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
