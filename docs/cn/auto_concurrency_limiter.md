# 自适应限流

服务的处理能力是有客观上限的。当请求速度超过服务的处理速度时，服务就会过载。

如果服务持续过载，会导致越来越多的请求积压，最终所有的请求都必须等待较长时间才能被处理，从而使整个服务处于瘫痪状态。

与之相对的，如果直接拒绝掉一部分请求，反而能够让服务能够"及时"处理更多的请求。对应的方法就是[设置最大并发](https://github.com/brpc/brpc/blob/master/docs/cn/server.md#%E9%99%90%E5%88%B6%E6%9C%80%E5%A4%A7%E5%B9%B6%E5%8F%91)。

自适应限流能动态调整服务的最大并发，在保证服务不过载的前提下，让服务尽可能多的处理请求。

## 使用场景
通常情况下要让服务不过载，只需在上线前进行压力测试，并通过little's law计算出最大并发度就可以了。但在服务数量多，拓扑复杂，且处理能力会逐渐变化的局面下，使用固定的最大并发会带来巨大的测试工作量，很不方便。自适应限流就是为了解决这个问题。

使用自适应限流前建议做到：
1. 客户端开启了重试功能。

2. 服务端有多个节点。

这样当一个节点返回过载时，客户端可以向其他的节点发起重试，从而尽量不丢失流量。

## 开启方法
目前只有method级别支持自适应限流。如果要为某个method开启自适应限流，只需要将它的最大并发设置为"auto"即可。

```c++
// Set auto concurrency limiter for all method
brpc::ServerOptions options;
options.method_max_concurrency = "auto";

// Set auto concurrency limiter for specific method
server.MaxConcurrencyOf("example.EchoService.Echo") = "auto";
```

## 基本原理

### 名词
**concurrency**: 同时处理的请求数，又被称为“并发度”。

**max_concurrency**: 允许的最大concurrency，又被称为“最大并发度”。

**noload_latency**: 处理任务的平均延时，不包括排队时间。

**min_latency**: 实际测定的latency中的较小值，当预估的最大并发度没有显著高于真实的并发度时，min_latency和noload_latency接近。

**peak_qps**: 极限qps。注意是处理或回复的qps而不是接收的qps。

### Little's Law
在服务处于稳定状态时: concurrency = latency * qps。 这是自适应限流的理论基础。

当服务没有超载时，随着流量的上升，latency基本稳定(接近noload_latency)，qps和concurrency呈线性关系一起上升。

当流量超过服务的极限qps时，则concurrency和latency会一起上升，而qps会稳定在极限qps。

假如一个服务的peak_qps和noload_latency都比较稳定，那么它的最佳max_concurrency = noload_latency * peak_qps。

自适应限流就是要找到服务的noload_latency和peak_qps， 并将最大并发设置为靠近 noload_latency * peak_qps的一个值。

### 计算公式

自适应限流会不断的对请求进行采样，当采样窗口的样本数量足够时，会根据样本的平均延迟和服务当前的qps计算出下一个采样窗口的max_concurrency:

> max_concurrency = peak_qps * ((2+alpha) * min_latency - avg_latency)

alpha为可接受的延时上升幅度，默认0.3。

avg_latency是当前采样窗口内所有请求的平均latency。

peak_qps是最近一段时间测量到的qps的极大值。

min_latency是最近一段时间测量到的latency较小值的ema，是noload_latency的估算值。

当服务处于低负载时，min_latency约等于noload_latency，此时计算出来的max_concurrency会高于实际并发，但低于真实并发，给流量上涨留探索空间。而当服务过载时，服务的qps约等于peak_qps，同时avg_latency开始明显超过min_latency，此时max_concurrency则会接近或小于实际并发，并通过定期衰减避免远离真实并发，保证服务不会过载。


### 使用采样窗口的平均latency而非最小latency来估计最佳并发

一些实现使用采样窗口内的最小latency和最近一段时间内的最小latency进行比较，并以此来调整服务的最大并发。

这种做法有两个缺陷，第一是很容易出现bad case，假如最近一段时间内所有的请求都大于保存的min_latency，会直接导致max_concurrency不断的缩小。第二点是该方案只考虑了latency和concurrency，而忽略了实际的qps值，很容易在noload_latency发生变化时出问题。


### 估算noload_latency
服务的noload_latency并非是一成不变的，自适应限流必须能够正确的探测noload_latency的变化。当noload_latency下降时，是很容感知到的，因为这个时候avg_latency也会下降。难点在于当avg_latency上涨时，需要能够正确的辨别到底是服务过载了，还是noload_latency上涨了。

我们尝试过多种方案：
1. 取最近一段时间的最小avg_latency来近似noload_latency
2. 取最近一段时间的avg_latency的平均值或者指数平均值来预测noload_latency
3. 收集请求的平均排队等待时间，使用avg_latency-avg_queue_time作为noload_latency
4. 每隔一段时间缩小max_concurrency，使服务进入低负载，以此时的avg_latency作为noload_latency


经过大量实验之后发现方案4是最通用的，虽然在重新测量noload_latency时需要缩小max_concurrency，从而损失一些流量，但是是可以通过其他的技术手段将这个损失减到最小。

方案1和方案2的存在同一个问题：尽管真正的noload_latency的上涨时，算法能够在一段时间之后正确的感知到，但即使noload_latency没有上涨，假如服务持续处于高负载，那么最近的所有avg_latency都会高出noload_latency，从而使得算法估计的noload_latency不断升高。而方案3的问题在于，假如服务的性能瓶颈在下游服务，那么请求的在服务本身的排队等待时间就无法反应服务的负载情况了。每隔一段时间缩小max_concurrency来测量noload_latency的方案是最不容易出现badcase的。

### 减少重新测量noload_latency时的流量损失

每隔一段时间，自适应限流算法都会将max_concurrency缩小25%。并持续一段时间，然后将此时的avg_latency作为服务的noload_latency，以处理noload_latency上涨了的情况。测量noload_latency时，必须让先服务处于低负载的状态，因此对max_concurrency的缩小时难以避免的。

为了减少max_concurrency缩小之后所带来的流量损失，需要尽可能的缩短测量的时间。根据前面的Little's Law，假如服务处于高负载状态，所有的请求都需要排队一段时间才能得到处理。所以缩小max_concurrency的最短持续的时间就是:

> 排空所有的经历过排队等待的请求的时间 + 收集足够样本以计算min_latency的时间

由于max_concurrency < concurrency时，服务会拒绝掉所有的请求，限流算法将"排空所有的经历过排队等待的请求的时间" 设置为 avg_latency * 2 ，以确保用于计算min_latency的样本绝大部分都是没有经过排队等待的。


由于服务的avg_latency通常都不会太长，这种做法所带来的流量损失也很小。


### 应对latency的抖动
即使服务自身没有过载，avg_latency也会发生波动，根据Little's Law，avg_latency的波动会导致server的concurrency发生波动。

我们在设计自适应限流的计算公式时，考虑到了latency发生抖动的情况:
当服vg_latency与min_latency很接近时，根据计算公式会得到一个较高max_concurrency来适应concurrency的波动，从而尽可能的减少“误杀”。同时，随着avg_latency的升高，max_concurrency会逐渐降低，以保护服务不会过载。

从另一个角度来说，当avg_latency也开始升高时，通常意味着某处(不一定是服务本身，也有可能是下游服务)消耗了大量CPU资源，这个时候缩小max_concurrency也是合理的。


### noload_latency 和 qps 的平滑处理
为了减少个别窗口的抖动对限流算法的影响，同时尽量降低计算开销，在计算min_latency和peak_qps时，会通过使用[EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)来进行平滑处理：

```
if avg_latency > min_latency:
    min_latency = avg_latency * ema_alpha  + (1 - ema_alpha) * min_latency
else:
    do_nothing
    
if current_qps > peak_qps:
    peak_qps = current_qps
else: 
    peak_qps = current_qps * ema_alpha / 10 + (1 - ema_alpha / 10) * peak_qps

```

将peak_qps的ema参数置为min_latency的ema参数的十分之一的原因是: peak_qps 下降了通常并不意味着极限qps也下降了。而min_latency下降了，通常意味着noload_latency确实下降了。


### 提高qps增长的速度
当服务启动时，由于服务本身需要进行一系列的初始化，tcp本身也有慢启动等一系列原因。服务在刚启动时的qps一定会很低。这就导致了服务启动时的max_concurrency也很低。而按照上面的计算公式，当max_concurrency很低的时候，预留给qps增长的冗余concurrency也很低(即：alpha * peak_qps * min_latency)。从而会影响当流量增加时，服务max_concurrency的增加速度。

假如从启动到打满qps的时间过长，这期间会损失大量流量。在这里我们采取的措施有两个，

1. 采样方面，一旦采到的请求数量足够多，直接提交当前采样窗口，而不是等待采样窗口的到时间了才提交
2. 计算公式方面，当current_qps > 保存的peak_qps时，直接进行更新，不进行平滑处理。

在进行了这两个处理之后，绝大部分情况下都能够在2秒左右将qps打满。
