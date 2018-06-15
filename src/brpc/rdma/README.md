# Using RDMA

To use rdma, please set the ChannelOptions and ServerOptions like this:

```c++
ChannelOptions chan_options;
chan_options.use_rdma = true;

ServerOptions serv_options;
serv_options.use_rdma = true;
```

