# Adaptive current limit

The processing capacity of the service has an objective upper limit. When the request speed exceeds the processing speed of the service, the service becomes overloaded.

If the service continues to be overloaded, it will cause more and more request backlogs, and eventually all requests have to wait a long time to be processed, thus making the entire service paralyzed.

In contrast, if you directly reject some of the requests, it will enable the service to process more requests in a "timely" manner. The corresponding method is [Set Maximum Concurrency](https://github.com/brpc/brpc/blob/master/docs/cn/server.md#%E9%99%90%E5%88%B6%E6%9C %80%E5%A4%A7%E5%B9%B6%E5%8F%91).

Adaptive current limiting can dynamically adjust the maximum concurrency of the service, allowing the service to process as many requests as possible under the premise of ensuring that the service is not overloaded.

## scenes to be used
Under normal circumstances, if the service is not overloaded, you only need to perform a stress test before going online and calculate best_max_concurrency through little's law. However, in a situation where the number of services is large, the topology is complex, and the processing capacity will gradually change, using a fixed maximum concurrency will bring a huge test workload, which is very inconvenient. Adaptive current limiting is to solve this problem.

It is recommended to do the following before using adaptive current limiting:
1. The client has enabled the retry function.

2. There are multiple nodes on the server side.

In this way, when a node returns to overload, the client can initiate a retry to other nodes, so as not to lose traffic as much as possible.

## How to open
Currently, only the method level supports adaptive current limiting. If you want to enable adaptive current limiting for a method, you only need to set its maximum concurrency to "auto".

```c++
// Set auto concurrency limiter for all methods
brpc::ServerOptions options;
options.method_max_concurrency = "auto";

// Set auto concurrency limiter for specific method
server.MaxConcurrencyOf("example.EchoService.Echo") = "auto";
```

## Principle

### noun
**concurrency**: The number of requests processed at the same time, also known as "concurrency".

**max_concurrency**: Set the maximum concurrency. Requests exceeding concurrency will be rejected (return ELIMIT error). At the cluster level, the client should retry to another server.

**best_max_concurrency**: The physical meaning of concurrency is the task processing slot, there is a natural upper limit, this upper limit is best_max_concurrency. If max_concurrency is set too large, concurrency may be greater than best_max_concurrency, tasks will not be processed in time and temporarily queued in various queues, and the system will enter a congested state. If max_concurrency is set too small, concurrency will always be less than best_max_concurrency, limiting the system to a higher throughput that could have been achieved.

**noload_latency**: Simply processing the delay of the task, excluding the queuing time. Another explanation is the low load delay. Due to the necessary steps to process the task correctly, which will consume CPU or wait for downstream return, noload_latency is an inherent attribute of the service, but it may gradually change over time (due to memory fragmentation, pressure changes, business data changes, etc.).

**min_latency**: The ema of the smaller value in the actual measured latency. When concurrency is not greater than best_max_concurrency, min_latency and noload_latency are close (may increase slightly).

**peak_qps**: The upper limit of qps. Note that it is the qps that is processed or replied, not the qps that is received. The value depends on best_max_concurrency / noload_latency. These two quantities are inherent attributes of the service, so peak_qps is also an inherent attribute of the service, which has nothing to do with congestion, but may change gradually over time.

**max_qps**: The larger value of the actual measured qps. Since qps has an upper limit, max_qps will always be less than peak_qps, regardless of congestion or not.

### Little's Law
When the service is in a stable state: concurrency = latency * qps. This is the theoretical basis of adaptive current limiting.

When the service is not overloaded, as the traffic increases, latency is basically stable (close to noload_latency), and qps and concurrency rise together in a linear relationship.

When the traffic exceeds the peak_qps of the service, concurrency and latency will rise together, and qps will stabilize at peak_qps.

If the peak_qps and noload_latency of a service are relatively stable, then its best_max_concurrency = noload_latency * peak_qps.

Adaptive current limiting is to find the noload_latency and peak_qps of the service, and set the maximum concurrency to a value close to the product of the two.

### Calculation formula

The adaptive current limit will continuously sample the request. When the number of samples in the sampling window is sufficient, the max_concurrency of the next sampling window will be calculated based on the average delay of the sample and the current qps of the service:

> max_concurrency = max_qps * ((2+alpha) * min_latency-latency)

Alpha is the acceptable increase in delay, and the default is 0.3.

Latency is the average latency of all requests in the current sampling window.

max_qps is the maximum value of qps measured in the most recent period of time.

min_latency is the ema of the smaller latency value measured in the most recent period, and is the estimated value of noload_latency.

When the service is under low load, min_latency is approximately equal to noload_latency. At this time, the calculated max_concurrency will be higher than concurrency, but lower than best_max_concurrency, leaving room for exploration. When the service is overloaded, the qps of the service is approximately equal to max_qps, and the latency begins to significantly exceed min_latency. At this time, max_concurrency will be close to concurrency, and it will be attenuated regularly to avoid staying away from best_max_concurrency to ensure that the service will not be overloaded.


### Estimate noload_latency
The noload_latency of the service is not static, and the adaptive current limit must be able to correctly detect the change of noload_latency. When noload_latency decreases, it is easy to perceive, because the latency will also decrease at this time. The difficulty is that when the latency rises, it is necessary to be able to correctly distinguish whether the service is overloaded or noload_latency has risen.

The possible solutions are:
1. Take the minimum latency of the most recent period of time to approximate noload_latency
2. Take the various averages of the recent latency to predict noload_latency
3. Collect the average queue waiting time of requests, use latency-queue_time as noload_latency
4. Reduce max_concurrency at regular intervals, and use the current latency as noload_latency after a short period of time

The problem with solutions 1 and 2 is that if the service continues to be under high load, all recent latency will be higher than noload_latency, which makes the noload_latency estimated by the algorithm continue to rise.

The problem with scheme 3 is that if the performance bottleneck of the service is in the downstream service, the waiting time of the request in the service itself cannot reflect the overall load situation.

Scheme 4 is the most versatile and has been tested by a large number of experiments. There is a correlation between shrinking max_concurrency and the alpha in the formula. Let us do a hypothetical experiment. If the latency is extremely stable and equal to min_latency, then the formula is simplified to max_concurrency = max_qps * latency * (1 + alpha). According to little's law, qps is at most max_qps * (1 + alpha). Alpha is the "exploration space" of qps. If alpha is 0, qps is locked to max_qps, and the algorithm may not be able to explore peak_qps. But when qps has reached peak_qps, alpha will increase the delay (congested). At this time, the measured min_latency will be greater than noload_latency, and one round will eventually cause min_latency to fail to converge. Decreasing max_concurrency regularly is to prevent this process and provide "exploration space" for min_latency to decrease.

#### Reduce traffic loss during retest

Every once in a while, the adaptive current limiting algorithm will reduce the max_concurrency and continue for a period of time, and then use the latency at this time as the noload_latency of the service to deal with the situation where the noload_latency rises. When measuring noload_latency, the first service must be in a low load state, so the reduction of max_concurrency is unavoidable.

Since max_concurrency <concurrency, the service will reject all requests, and the current limiting algorithm sets the "time to drain all requests that have experienced queuing" to latency * 2 to ensure that most of the samples used to calculate min_latency are There is no waiting in line.

Since the latency of the service is usually not too long, the traffic loss caused by this approach is also very small.

#### Coping with jitter
Even if the service itself is not overloaded, latency will fluctuate. According to Little's Law, latency fluctuations will cause the server's concurrency to fluctuate.

When designing the calculation formula of adaptive current limit, we took into account the jitter of latency:
When the latency is very close to min_latency, a higher max_concurrency will be obtained according to the calculation formula to adapt to the fluctuation of concurrency, thereby reducing "manslaughter" as much as possible. At the same time, as the latency increases, max_concurrency will gradually decrease to protect the service from overload.

From another perspective, when the latency also starts to increase, it usually means that somewhere (not necessarily the service itself, but also a downstream service) consumes a lot of CPU resources. At this time, it is reasonable to reduce max_concurrency.

#### Smoothing
In order to reduce the impact of the jitter of individual windows on the current-limiting algorithm, and at the same time minimize the computational overhead, the calculation of min_latency will use [EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average) for smoothing processing:

```
if latency> min_latency:
    min_latency = latency * ema_alpha + (1-ema_alpha) * min_latency
else:
    do_nothing
```

### Estimate peak_qps

#### Improve the speed of qps growth
When the service starts, because the service itself needs to be initialized, tcp itself also has a series of reasons such as slow start. The qps of the service will be very low when it is just started. This leads to a very low max_concurrency when the service starts. According to the above calculation formula, when max_concurrency is very low, the redundant concurrency reserved for qps growth is also very low (ie: alpha * max_qps * min_latency). This will affect the rate of increase in service max_concurrency when the traffic increases.

If the time from start to full qps is too long, a lot of traffic will be lost during this period. There are two measures we have taken here,

1. In terms of sampling, once the number of requests collected is sufficient, submit directly to the current sampling window instead of waiting for the sampling window to expire before submitting
2. In terms of calculation formula, when current_qps> saved max_qps, update directly without smoothing.

After performing these two processes, in most cases, the qps can be filled in about 2 seconds.

#### Smoothing
In order to reduce the impact of the jitter of individual windows on the current-limiting algorithm, and to minimize the computational overhead, when calculating max_qps, it will be smoothed by using [EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average) deal with:

```    
if current_qps> max_qps:
    max_qps = current_qps
else: 
    max_qps = current_qps * ema_alpha / 10 + (1-ema_alpha / 10) * max_qps
```
The reason for setting the ema parameter of max_qps to one-tenth of the ema parameter of min_latency is: a decrease in max_qps usually does not mean that the limit qps has also decreased. While min_latency has dropped, it usually means that noload_latency has indeed dropped.

### Comparison with netflix gradient algorithm

The gradient algorithm formula in netflix is: max_concurrency = min_latency / latency * max_concurrency + queue_size.

Among them, latency is the minimum latency of the sampling window, and min_latency is the minimum latency of the most recent sampling windows. Min_latency / latency is the "gradient" in the algorithm. When latency is greater than min_latency, max_concurrency will gradually decrease; conversely, max_concurrency will gradually rise, so that max_concurrency will be around best_max_concurrency.

This formula can be compared with the algorithm of this article:

* The latency in the gradient algorithm is different from this algorithm. The latency of the former is the minimum value and the latter is the average value. The original intention of netflix is ​​that the minimum value can better represent noload_latency, but in fact, as long as the max_concurrency is not decayed regularly, both the minimum value and the average value may continue to rise and the algorithm will not converge. The minimum value does not bring additional benefits, but makes the algorithm more unstable.
* The max_concurrency / latency in the gradient algorithm is conceptually related to qps (according to little's law), but it may be seriously out of touch. Such as retesting
  Before min_latency, if all latency is less than min_latency, then max_concurrency will continue to decrease or even to 0; but according to this algorithm, max_qps and min_latency are still stable, and their calculated max_concurrency will not change drastically. In essence, when the gradient algorithm is iterating max_concurrency, latency does not represent the delay when the actual concurrency is max_concurrency. The two are disconnected. Therefore, the actual physical meaning of max_concurrency / latency is unclear. It may be very different from qps. Big deviation.
* The queue_size of the gradient algorithm is recommended as sqrt(max_concurrency), which is unreasonable. Netflix's understanding of queue_size probably represents the cache of various uncontrollable links, such as the socket in the socket, and there is a certain positive relationship with max_concurrency, which is excusable. But in our understanding, this part of queue_size has little effect, no or constants can be used. The queue_size we pay attention to is the exploration space left for the rise of concurrency: the update of max_concurrency is delayed. In the process of increasing concurrency from low to high, the role of queue_size is to not limit the increase of qps before max_concurrency is updated. When the concurrency is high, the service may be overloaded, and the queue_size should be smaller to prevent further deterioration of the delay. Here queue_size and concurrency are inversely related.