# 编译

GDR: GPU Direct Rdma, gdr 是rdma的一种特殊模式，其通过rdma将数据直接收到了gpu的显存上。

由于GDR对驱动与硬件有要求，目前仅支持在Linux系统编译并运行GDR功能。

目前GDR只支持baidu std protocol。

使用config_brpc：
```bash
sh config_brpc.sh --with-rdma --with-gdr --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
make

cd example/rdma_performance  # 示例程序
make
```

使用bazel:
```bash
# Server
bazel build --define=BRPC_WITH_RDMA=true --define=BRPC_WITH_GDR=true example:rdma_performance_server
# Client
bazel build --define=BRPC_WITH_RDMA=true --define=BRPC_WITH_GDR=true example:rdma_performance_client
```

# 基本实现

GDR是RDMA的一种特殊形式，在使用GDR之前，必须对RDMA和GDR都进行Global Init。
GDR新增了一个显存池，类似于RDMA内存池，显存池的数据也是按照block进行组织的。
当打开GDR功能后，框架通过DoPostRecvGDR来发起显存上的WQE。
在接收到数据后，我们将header、meta、body（不包括attachment）copy回内存进行处理。
AttachMent位于显存上，用户可以调用IOBuf::copy_from_gpu接口将attachment从brpc框架层copy到应用层进行处理。


注意：
1. 在使用gdr功能时，需要将环境变量MLX5_SCATTER_TO_CQE设置为0.


# 参数

可配置参数说明：
* gdr_block_size_kb: 使用gdr传送数据时，block的大小（单位为KB），默认为512；
* max_gdr_regions: gdr显存池所使用Region的最大个数，每个Region大小为1GB； 
