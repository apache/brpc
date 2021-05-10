## Compile brpc with RDMA support

To use brpc with RDMA, please make sure you have built brpc with WITH_RDMA option first.

```bash

$ cmake -DWITH_RDMA=on . # in project's directory

$ make -j

```

## Using RDMA

To use rdma, please set the ChannelOptions and ServerOptions like this:

```c++
ChannelOptions chan_options;
chan_options.use_rdma = true;

ServerOptions serv_options;
serv_options.use_rdma = true;
```

Please remember that you can use RDMA only when your server has RDMA NIC and OFED suite.
