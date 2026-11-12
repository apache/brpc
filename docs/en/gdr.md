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

When using GDR, the environment variable MLX5_SCATTER_TO_CQE must be set to 0. This is because GPUDirect RDMA requires that all received data be written to the GPU memory buffer specified in the post‑receive operation. However, when MLX5_SCATTER_TO_CQE is enabled, small messages are directly inlined into host memory, which violates this requirement and consequently causes operation failures.

Using baidu master service (the proxy-forwarding mode) on a server with GDR enabled is not supported. The master service forwards each request verbatim to a backend, which is usually not GDR-capable, so the GPU-resident payload cannot be written out over a plain TCP link. Requests arriving on GDR connections are therefore rejected directly (failing with "baidu_master_service is not supported with GDR"). GDR targets clients directly connecting to GPU services (e.g. training/inference servers) so that data goes GPU-to-GPU; for scenarios that require proxy forwarding, use a plain TCP/RDMA link instead.

GDR does not support rpc dump yet. rpc dump samples requests to disk, but the underlying writev cannot read GPU memory addresses; GPU-resident requests on GDR connections are skipped from sampling (a WARNING is logged) and are not recorded.

Parameters

Configurable parameters:

gdr_block_size_kb: The block size (in KB) used when transferring data via GDR. Default is 512.

max_gdr_regions: The maximum number of regions used by the GDR GPU memory pool. Each region is 1 GB.
