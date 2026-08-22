Compile GDR:

GPU Direct RDMA. GDR is a special mode of RDMA that allows data to be received directly into the GPU’s memory through RDMA.
Because GDR requires specific drivers and hardware support, it is currently only available for compilation and execution on Linux systems.
At present, GDR only supports the Baidu STD protocol.

To use config_brpc:

sh config_brpc.sh --with-rdma --with-gdr --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
make
cd example/rdma_performance   # Example program
make

To use Bazel:

# Server
bazel build --define=BRPC_WITH_RDMA=true --define=BRPC_WITH_GDR=true example:rdma_performance_server

# Client
bazel build --define=BRPC_WITH_RDMA=true --define=BRPC_WITH_GDR=true example:rdma_performance_client


Basic Implementation:

GDR is a special form of RDMA. Before using GDR, both RDMA and GDR must be globally initialized.

GDR introduces a GPU memory pool, similar to the RDMA memory pool. Data in the GPU memory pool is also organized in blocks.

When GDR is enabled, the framework initiates WQEs on GPU memory through DoPostRecvGDR.

After receiving data, the header, meta, and body (excluding attachments) are copied back to host memory for processing.
Attachments remain in GPU memory, and users can call IOBuf::copy_from_gpu to copy attachments from the brpc framework layer to the application layer.

Note:

When using GDR, the environment variable MLX5_SCATTER_TO_CQE must be set to 0.

Parameters

Configurable parameters:

gdr_block_size_kb: The block size (in KB) used when transferring data via GDR. Default is 512.

max_gdr_regions: The maximum number of regions used by the GDR GPU memory pool. Each region is 1 GB.
