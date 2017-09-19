[bvar](https://github.com/brpc/brpc/tree/master/src/bvar/) is a counting utility for multi threaded scenario, it stores data in thread local storage which avoids cache bouncing. It is much faster than UbMonitor(a legacy counting utility used inside Baidu) and atomic operations in highly contended scenario. bvar is builtin within brpc, through [/vars](http://brpc.baidu.com:8765/vars) you can access all the exposed bvars, or a very single one specified by [/vars/VARNAME](http://brpc.baidu.com:8765/vars/rpc_socket_count). Check out [bvar](bvar.md) if you'd like add some bvars for you own services. bvar is widely used inside brpc to calculate indicators. bvar is **almost free** in most scenarios to collect data.  If you are finding a utility to count and show internal status of a multi threaded apllication, you shoud try bvar at the first time. bvar is not suitable for general purpose counters, the read process of a single bvar have to combines all the TLS data in the threads that the very bvar has been written so that it's very slow(compared to the write process and atomic operations).

## Check out bvars

[/vars](http://brpc.baidu.com:8765/vars) : List all the bvars

[/vars/NAME](http://brpc.baidu.com:8765/vars/rpc_socket_count)：Lookup for the bvar whose name is `NAME`

[/vars/NAME1,NAME2,NAME3](http://brpc.baidu.com:8765/vars/pid;process_cpu_usage;rpc_controller_count)：Lookup the bvars whose name are `NAME1`, `NAME2` or `NAME3`.

[/vars/foo*,b$r](http://brpc.baidu.com:8765/vars/rpc_server*_count;iobuf_blo$k_*)：Lookup for the bvar whose name matches the given pattern. Note that `$` replaces `?` to represent a single character since `?` is reserved in URL.

The following animation shows how you can lookup bvars with pattern. You can paste the URI to other forks who will see excatcly the same contents through this URI.

![img](../images/vars_1.gif)

There's also a search box in the front of /vars. You can lookup for bvars with parts of the names. Different names can be specareted by `,` `:` or ` `.

![img](../images/vars_2.gif)

It's OK to access /vars throught terminal with curl as well：

```
$ curl brpc.baidu.com:8765/vars/bthread*
bthread_creation_count : 125134
bthread_creation_latency : 3
bthread_creation_latency_50 : 3
bthread_creation_latency_90 : 5
bthread_creation_latency_99 : 7
bthread_creation_latency_999 : 12
bthread_creation_latency_9999 : 12
bthread_creation_latency_cdf : "click to view"
bthread_creation_latency_percentiles : "[3,5,7,12]"
bthread_creation_max_latency : 7
bthread_creation_qps : 100
bthread_group_status : "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
bthread_num_workers : 24
bthread_worker_usage : 1.01056
```

## Check out the historical values

You can click for almost all the numerical bvar to check out their historical values. Every clickable bvar store values in the recent `60s/60m/24h/30d`, *174* numbers in total。It takes about 1M memory when there are 1000 clickable bvars.

![img](../images/vars_3.gif)

## 统计和查看分位值

A percentile indicats the value below which a given percentage of samples in a group of samples. E.g. there are 1000 in a very time window，The 500-th in the sorted set(1000 * 50%) is the value 50%-percentile(says median),  the number at the 990-th is 99%-percentile(1000 * 99%)，the number at 999-th is 99.9%-percentile. Percentiles shows more formation about the latency distribution than average latency, which is very important for you are calculating the SAL of the service. 对于最常见的延时统计，平均值很难反映出实质性的内容，99.9%分位值往往更加关键，它决定了系统能做什么。

分位值可以绘制为CDF曲线和按时间变化时间。

![img](../images/vars_4.png)

上图是CDF曲线。纵轴是延时。横轴是小于纵轴数值的数据比例。很明显地，这个图就是由从10%到99.99%的所有分位值组成。比如横轴=50%处对应的纵轴值便是50%分位值。那为什么要叫它CDF？CDF是[Cumulative Distribution Function](https://en.wikipedia.org/wiki/Cumulative_distribution_function)的缩写。当我们选定一个纵轴值x时，对应横轴的含义是"数值 <= x的比例”，如果数值是来自随机采样，那么含义即为“数值 <= x的概率”，这不就是概率的定义么？CDF的导数是[概率密度函数](https://en.wikipedia.org/wiki/Probability_density_function)，换句话说如果我们把CDF的纵轴分为很多小段，对每个小段计算两端对应的横轴值之差，并把这个差作为新的横轴，那么我们便绘制了PDF曲线，就像（横着的）正态分布，泊松分布那样。但密度会放大差距，中位数的密度往往很高，在PDF中很醒目，这使得边上的长尾相对很扁而不易查看，所以大部分系统测试结果选择CDF曲线而不是PDF曲线。

可以用一些简单规则衡量CDF曲线好坏：

- 越平越好。一条水平线是最理想的，这意味着所有的数值都相等，没有任何等待，拥塞，停顿。当然这是不可能的。
- 99%之后越窄越好：99%之后是长尾的聚集地，对大部分系统的SLA有重要影响，越少越好。如果存储系统给出的性能指标是"99.9%的读请求在xx毫秒内完成“，那么你就得看下99.9%那儿的值；如果检索系统给出的性能指标是”99.99%的请求在xx毫秒内返回“，那么你得关注99.99%分位值。

一条真实的好CDF曲线的特征是”斜率很小，尾部很窄“。 

![img](../images/vars_5.png)

上图是按时间变化曲线。包含了4条曲线，横轴是时间，纵轴从上到下分别对应99.9%，99%，90%，50%分位值。颜色从上到下也越来越浅（从橘红到土黄）。滑动鼠标可以阅读对应数据点的值，上图中显示是”39秒种前的99%分位值是330微秒”。这幅图中不包含99.99%的曲线，因为99.99%分位值常明显大于99.9%及以下的分位值，画在一起的话会使得其他曲线变得很”矮“，难以辨认。你可以点击以"_latency_9999"结尾的bvar独立查看99.99%曲线，当然，你也可以独立查看50%,90%,99%,99.9%等曲线。按时间变化曲线可以看到分位值的变化趋势，对分析系统的性能变化很实用。

brpc的服务都会自动统计延时分布，用户不用自己加了。如下图所示：

![img](../images/vars_6.png)

你可以用bvar::LatencyRecorder统计非brpc服务的延时，这么做(更具体的使用方法请查看[bvar-c++](bvar_c++.md)):

```c++
#include <bvar/bvar.h>
 
...
bvar::LatencyRecorder g_latency_recorder("client");  // expose this recorder
... 
void foo() {
    ...
    g_latency_recorder << my_latency;
    ...
}
```

如果这个程序使用了brpc server，那么你应该已经可以在/vars看到client_latency, client_latency_cdf等变量，点击便可查看动态曲线。如下图所示：

![img](../images/vars_7.png)

## Non brpc server

如果这个程序只是一个brpc client或根本没有使用brpc，并且你也想看到动态曲线，看[这里](dummy_server.md)。
