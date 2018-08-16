# 自适应限流

每个服务的处理能力都是有客观上限的。当服务接收请求的速度超过服务的处理速度时，服务就会过载。

如果服务持续过载，会导致越来越多的请求积压在服务端，最终所有的请求都必须等待较长时间才能得到处理，从而使整个服务处于瘫痪状态。

与之相对的，如果直接拒绝掉一部分请求，反而能够让服务能够"及时"处理更多的请求。自适应限流能够动态的调整服务的最大并发，在保证服务不过载的前提下，让服务尽可能多的处理请求。

## 使用场景
通常情况下，要做到服务不过载的同时，尽可能的不浪费服务器的处理能力，只需要在上线前进行压力测试，设置一个合适的最大并发值就可以了。但是在微服务和集群被广泛应用的现在，服务的处理能力是会动态变化的。这个时候如果使用固定的最大并发，很可能带来各种问题。

自适应限流就是为了解决配置的固定最大并发可能会过时的问题。但是动态的估算服务的处理能力是一件很困难的事情，目前使用自适应限流的前提有两个：
1. 客户端开启了重试功能
2. 服务端有多个节点。当一个节点返回过载时，客户端可以向其他的节点发起重试

这两点是自适应限流能够良好工作的前提。

## 开启方法
直接使用"auto"替换掉之前的最大并发值：

```
//constant max_concurrency
brpc::ServerOptions options;
options.max_concurrency = 100;   

// auto concurrency limiter
brpc::ServerOptions options;
options.max_concurrency = "auto";   
```

**假如需要使用自适应限流，建议仅在Server级别开启自适应限流**，各个method都不限流(即使用默认值)，或者使用固定的最大并发。不要同时在Server和method都开启自适应限流:

```
brpc::Server server;
brpc::ServerOptions options;
options.max_concurrency = "auto";                       // Use auto concurrenty limiter only at the Server level
server.MaxConcurrencyOf("test.EchoService.Echo") = 100; // constant max concurrency
server.Start(FLAGS_echo_port, &options);
```

## 自适应限流的实现

### Little's Law
在服务处于稳定状态时: concurrency = latency * qps。 这是自适应限流的理论基础。

当服务没有超载时，随着流量的上升，latency基本稳定(我们把这个latency记为noload_latency)，qps和concurrency呈线性关系一起上升。当流量超过服务的极限qps时，则concurrency和latency会一起上升，而qps会稳定在极限qps。所以假如一个服务的peakqps和noload_latency都比较稳定，那么它的最佳max_concurrency = noload_latency * peakqps。

自适应限流所做的工作就是找到服务在低负载时的平均延迟 noload_latency 和peakqps， 并将最大并发设置为靠近 noload_latency * peakqps的一个值。 假如服务所设置的最大并发超过noload_latency * peakqps，那么 所有的请求都需要等待一段时间才能得到服务的处理。

自适应限流的工作流程类似TCP的[拥塞控制算法](https://en.wikipedia.org/wiki/TCP_congestion_control#TCP_BBR)，自适应限流算法会交替的测试noload_latency 和 peak_qps： 绝大部分时候通过尽可能的调高max_concurrency来接收并处理更多的请求，同时测量peakqps。少部分时候会主动降低max_concurrency来测量server的noload_latency. 和TCP的拥塞控制算法不同的是，服务端无法主动的控制请求的流量，只能在发现自身即将超载时，拒绝掉一部分请求。

## 计算公式:

自适应限流会不断的对请求进行采样，当采样窗口的样本数量足够时，会根据样本的平均延迟和服务当前的qps计算出下一个采样窗口的max_concurrency:

> max_concurrency = peak_qps * ((2+alpha) * min_latency - avg_latency)

alpha为预期内的avg_latency抖动幅度，默认0.3。

avg_latency是当前采样窗口内所有请求的平均latency。

peak_qps是最近一段时间实际测量到的qps的极大值。

min_latency是对实际的noload_latency的估算值。

按照Little's Law，当服务处于低负载时，avg_latency 约等于 min_latency，此时计算出来的最大并发会比实际并发高一些，保证了当流量上涨时，服务的max_concurrency能够和qps一起上涨。而当服务过载时，服务的当前qps约等于peakqps，同时avg_latency开始超过min_latency。此时max_concurrency则会逐渐缩小，保证服务不会过载。


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
为了减少个别窗口的抖动对限流算法的影响，同时尽量降低计算开销，在计算min_latency和peakqps时，会通过使用[EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)来进行平滑处理：

```
if avg_latency > min_latency:
    min_latency = avg_latency * ema_alpha  + (1 - ema_alpha) * min_latency
else:
    do_nothing
    
if current_qps > peakqps:
    peakqps = current_qps
else: 
    peakqps = current_qps * ema_alpha / 10 + (1 - ema_alpha / 10) * peakqps

```

将peakqps的ema参数置为min_latency的ema参数的十分之一的原因是: peakqps 下降了通常并不意味着极限qps也下降了。而min_latency下降了，通常意味着noload_latency确实下降了。


### 提高qps增长的速度
当服务启动时，由于服务本身需要进行一系列的初始化，tcp本身也有慢启动等一系列原因。服务在刚启动时的qps一定会很低。这就导致了服务启动时的max_concurrency也很低。而按照上面的计算公式，当max_concurrency很低的时候，预留给qps增长的冗余concurrency也很低(即：alpha * peakqps * min_latency)。从而会影响当流量增加时，服务max_concurrency的增加速度。

假如从启动到打满qps的时间过长，这期间会损失大量流量。在这里我们采取的措施有两个，

1. 采样方面，一旦采到的请求数量足够多，直接提交当前采样窗口，而不是等待采样窗口的到时间了才提交
2. 计算公式方面，当current_qps > 保存的peak_qps时，直接进行更新，不进行平滑处理。

在进行了这两个处理之后，绝大部分情况下都能够在2秒左右将qps打满。
