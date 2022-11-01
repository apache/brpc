# background

The Cloud Platform Department transformed modules that used ubrpc to use brpc. Due to the use of the conversion function of mcpack2pb, this module can be accessed by the old ubrpc client or through the protobuf protocol (baidu_std, sofa_pbrpc, etc.).

Originally, 43 machines were used (there is surplus for ubrpc), and brpc can use 3 machines (at this time, access to redis io reaches the bottleneck). The current traffic is 4w qps, which supports traffic growth, and considers cross-machine room redundancy to avoid redis and VIP bottlenecks. brpc actually uses 8 machines to provide services.

After brpc's transformation, the connector has obvious benefits, and it can provide better services with fewer machines. The income is divided into 3 areas:

# Comparison of qps and latency of machines with the same configuration

By gradually shrinking the capacity and continuously increasing the pressure on the connector, the corresponding data of the single machine qps and latency is obtained as follows:
![img](../images/ubrpc_compare_1.png)

Machine configuration: cpu: 24 Intel(R) Xeon(R) CPU E5645 @ 2.40GHz || mem: 64G

Mixed deployment situation: Logic layer 2.0/3.0 and C logic layer are deployed on the same machine, and both have traffic

As you can see in the figure as the pressure increases:
* The delay of brpc is minimal, providing a more consistent delay experience
* The delay of ubrpc increases rapidly. When it reaches 6000~8000qps, *queue full* appears and the service is unavailable.

# Comparison of qps and delay of different configuration machines
The qps is fixed at 6500, and the delay is observed.

|  Machine Name  | Omit  | Omit                                        |
| --- | --- |---------------------------------------------|
| cpu | 24 Intel(R) Xeon(R) CPU E5645 @ 2.40GHz | 24 Intel(R) Xeon(R) CPU E5-2620 0 @ 2.00GHz |
| ubrpc | 8363.46 (us) | 12649.5 (us)                                |
| brpc | 3364.66 (us) | 3382.15 (us)                                |

This can be seen:

* ubrpc has a big difference in performance under different configurations, and it performs poorly on a machine with a lower configuration.
* The performance of brpc is better than that of ubrpc, and it can also perform well on machines with lower configuration, but the difference caused by different machines is not big.

# Comparison of idle distribution of machines with the same configuration

Machine configuration: cpu: 24 Intel(R) Xeon(R) CPU E5645 @ 2.40GHz || mem: 64G

![img](../images/ubrpc_compare_2.png)

In the process of shrinking on-line and increasing pressure:

* ubrpc cpu idle is distributed in 35%~60%, 55% is the most concentrated, and the lowest is 30%;
* brpc cpu idle is distributed in 60%~85%, the most concentrated in 75%, the lowest is 50%; brpc consumes less CPU than ubrpc.