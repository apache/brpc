# background

baidu-dsp is a demand-side platform of the alliance based on Ad Exchange and RTB models, serving large customers and agency products. We have transformed a number of modules, all of which have achieved remarkable results. This article only introduces the changes to super-nova-as. super-nova-as is the AS of baidu-dsp. It was previously written using ub-aserver. In order to minimize changes, we did not modify the entire as, but only connected the super-nova-as downstream (ctr-server, cvr-server, The client of super-nova-bs) is upgraded from ubrpc to brpc.

# in conclusion

1. The throughput of as has been significantly improved (less than 1500 -> 2500+)
2. CPU optimization: from 1500qps 50% cpu_idle to 2000qps 50% cpu_idle;
3. The timeout rate has improved significantly.

# Testing process

1. Environment: 1 as, 1 bs, 1 ctr, 1 cvr; deployment situation: bs stand-alone deployment, mixed as+ctr+cvr; ctr and cvr are brpc versions
2. Using 1000 and 1500 pressures to perform pressure test on the ubrpc version of as, it is found that under 1500 pressure, as has a large amount of timeout to bs, and as reaches the bottleneck;
3. Using 2000 and 2500 pressures to perform pressure test on the brpc version of as, it is found that under 2500 pressure, the cpu_idle of the as machine is lower than 30%, and the as reaches the bottleneck. brpc makes full use of resources.

| | ubrpc | brpc |
| -------- | ---------------------------------------- | ---------------------------------------- |
| Traffic | ![img](../images/baidu_dsp_compare_1.png) | ![img](../images/baidu_dsp_compare_2.png) |
| bs success rate | ![img](../images/baidu_dsp_compare_3.png) | ![img](../images/baidu_dsp_compare_4.png) |
| cpu_idle | ![img](../images/baidu_dsp_compare_5.png) | ![img](../images/baidu_dsp_compare_6.png) |
| ctr success rate | ![img](../images/baidu_dsp_compare_7.png) | ![img](../images/baidu_dsp_compare_8.png) |
| cvr success rate| ![img](../images/baidu_dsp_compare_9.png) | ![img](../images/baidu_dsp_compare_10.png) |