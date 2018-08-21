# 自适应限流

服务的处理能力是有客观上限的。当请求速度超过服务的处理速度时，服务就会过载。

如果服务持续过载，会导致越来越多的请求积压，最终所有的请求都必须等待较长时间才能被处理，从而使整个服务处于瘫痪状态。

与之相对的，如果直接拒绝掉一部分请求，反而能够让服务能够"及时"处理更多的请求。对应的方法就是[设置最大并发](https://github.com/brpc/brpc/blob/master/docs/cn/server.md#%E9%99%90%E5%88%B6%E6%9C%80%E5%A4%A7%E5%B9%B6%E5%8F%91)。

自适应限流能动态调整服务的最大并发，在保证服务不过载的前提下，让服务尽可能多的处理请求。

## 使用场景
通常情况下要让服务不过载，只需在上线前进行压力测试，并通过little's law计算出best_max_concurrency就可以了。但在服务数量多，拓扑复杂，且处理能力会逐渐变化的局面下，使用固定的最大并发会带来巨大的测试工作量，很不方便。自适应限流就是为了解决这个问题。

使用自适应限流前建议做到：
1. 客户端开启了重试功能。

2. 服务端有多个节点。

这样当一个节点返回过载时，客户端可以向其他的节点发起重试，从而尽量不丢失流量。

## 开启方法
目前只有method级别支持自适应限流。如果要为某个method开启自适应限流，只需要将它的最大并发设置为"auto"即可。

```c++
// Set auto concurrency limiter for all methods
brpc::ServerOptions options;
options.method_max_concurrency = "auto";

// Set auto concurrency limiter for specific method
server.MaxConcurrencyOf("example.EchoService.Echo") = "auto";
```

## 原理

### 名词
**concurrency**: 同时处理的请求数，又被称为“并发度”。

**max_concurrency**: 设置的最大并发度。超过并发的请求会被拒绝（返回ELIMIT错误），在集群层面，client应重试到另一台server上去。

**best_max_concurrency**: 并发的物理含义是任务处理槽位，天然存在上限，这个上限就是best_max_concurrency。若max_concurrency设置的过大，则concurrency可能大于best_max_concurrency，任务将无法被及时处理而暂存在各种队列中排队，系统也会进入拥塞状态。若max_concurrency设置的过小，则concurrency总是会小于best_max_concurrency，限制系统达到本可以达到的更高吞吐。

**noload_latency**: 单纯处理任务的延时，不包括排队时间。另一种解释是低负载的延时。由于正确处理任务得经历必要的环节，其中会耗费cpu或等待下游返回，noload_latency是一个服务固有的属性，但可能随时间逐渐改变（由于内存碎片，压力变化，业务数据变化等因素）。

**min_latency**: 实际测定的latency中的较小值的ema，当concurrency不大于best_max_concurrency时，min_latency和noload_latency接近(可能轻微上升）。

**peak_qps**: qps的上限。注意是处理或回复的qps而不是接收的qps。值取决于best_max_concurrency / noload_latency，这两个量都是服务的固有属性，故peak_qps也是服务的固有属性，和拥塞状况无关，但可能随时间逐渐改变。

**max_qps**: 实际测定的qps中的较大值。由于qps具有上限，max_qps总是会小于peak_qps，不论拥塞与否。

### Little's Law
在服务处于稳定状态时: concurrency = latency * qps。 这是自适应限流的理论基础。

当服务没有超载时，随着流量的上升，latency基本稳定(接近noload_latency)，qps和concurrency呈线性关系一起上升。

当流量超过服务的peak_qps时，则concurrency和latency会一起上升，而qps会稳定在peak_qps。

假如一个服务的peak_qps和noload_latency都比较稳定，那么它的best_max_concurrency = noload_latency * peak_qps。

自适应限流就是要找到服务的noload_latency和peak_qps， 并将最大并发设置为靠近两者乘积的一个值。

### 计算公式

自适应限流会不断的对请求进行采样，当采样窗口的样本数量足够时，会根据样本的平均延迟和服务当前的qps计算出下一个采样窗口的max_concurrency:

> max_concurrency = max_qps * ((2+alpha) * min_latency - latency)

alpha为可接受的延时上升幅度，默认0.3。

latency是当前采样窗口内所有请求的平均latency。

max_qps是最近一段时间测量到的qps的极大值。

min_latency是最近一段时间测量到的latency较小值的ema，是noload_latency的估算值。

当服务处于低负载时，min_latency约等于noload_latency，此时计算出来的max_concurrency会高于concurrency，但低于best_max_concurrency，给流量上涨留探索空间。而当服务过载时，服务的qps约等于max_qps，同时latency开始明显超过min_latency，此时max_concurrency则会接近concurrency，并通过定期衰减避免远离best_max_concurrency，保证服务不会过载。


### 估算noload_latency
服务的noload_latency并非是一成不变的，自适应限流必须能够正确的探测noload_latency的变化。当noload_latency下降时，是很容感知到的，因为这个时候latency也会下降。难点在于当latency上涨时，需要能够正确的辨别到底是服务过载了，还是noload_latency上涨了。

可能的方案有：
1. 取最近一段时间的最小latency来近似noload_latency
2. 取最近一段时间的latency的各种平均值来预测noload_latency
3. 收集请求的平均排队等待时间，使用latency - queue_time作为noload_latency
4. 每隔一段时间缩小max_concurrency，过一小段时间后以此时的latency作为noload_latency

方案1和方案2的问题在于：假如服务持续处于高负载，那么最近的所有latency都会高出noload_latency，从而使得算法估计的noload_latency不断升高。

方案3的问题在于，假如服务的性能瓶颈在下游服务，那么请求在服务本身的排队等待时间无法反应整体的负载情况。

方案4是最通用的，也经过了大量实验的考验。缩小max_concurrency和公式中的alpha存在关联。让我们做个假想实验，若latency极为稳定并都等于min_latency，那么公式简化为max_concurrency = max_qps * latency * (1 + alpha)。根据little's law，qps最多为max_qps * (1 + alpha). alpha是qps的"探索空间"，若alpha为0，则qps被锁定为max_qps，算法可能无法探索到peak_qps。但在qps已经达到peak_qps时，alpha会使延时上升（已拥塞），此时测定的min_latency会大于noload_latency，一轮轮下去最终会导致min_latency不收敛。定期降低max_concurrency就是阻止这个过程，并给min_latency下降提供"探索空间"。

#### 减少重测时的流量损失

每隔一段时间，自适应限流算法都会缩小max_concurrency，并持续一段时间，然后将此时的latency作为服务的noload_latency，以处理noload_latency上涨了的情况。测量noload_latency时，必须让先服务处于低负载的状态，因此对max_concurrency的缩小是难以避免的。

由于max_concurrency < concurrency时，服务会拒绝掉所有的请求，限流算法将"排空所有的经历过排队等待的请求的时间" 设置为 latency * 2 ，以确保用于计算min_latency的样本绝大部分都是没有经过排队等待的。

由于服务的latency通常都不会太长，这种做法所带来的流量损失也很小。

#### 应对抖动
即使服务自身没有过载，latency也会发生波动，根据Little's Law，latency的波动会导致server的concurrency发生波动。

我们在设计自适应限流的计算公式时，考虑到了latency发生抖动的情况:
当latency与min_latency很接近时，根据计算公式会得到一个较高max_concurrency来适应concurrency的波动，从而尽可能的减少“误杀”。同时，随着latency的升高，max_concurrency会逐渐降低，以保护服务不会过载。

从另一个角度来说，当latency也开始升高时，通常意味着某处(不一定是服务本身，也有可能是下游服务)消耗了大量CPU资源，这个时候缩小max_concurrency也是合理的。

#### 平滑处理
为了减少个别窗口的抖动对限流算法的影响，同时尽量降低计算开销，计算min_latency时会通过使用[EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)来进行平滑处理：

```
if latency > min_latency:
    min_latency = latency * ema_alpha  + (1 - ema_alpha) * min_latency
else:
    do_nothing
```

### 估算peak_qps

#### 提高qps增长的速度
当服务启动时，由于服务本身需要进行一系列的初始化，tcp本身也有慢启动等一系列原因。服务在刚启动时的qps一定会很低。这就导致了服务启动时的max_concurrency也很低。而按照上面的计算公式，当max_concurrency很低的时候，预留给qps增长的冗余concurrency也很低(即：alpha * max_qps * min_latency)。从而会影响当流量增加时，服务max_concurrency的增加速度。

假如从启动到打满qps的时间过长，这期间会损失大量流量。在这里我们采取的措施有两个，

1. 采样方面，一旦采到的请求数量足够多，直接提交当前采样窗口，而不是等待采样窗口的到时间了才提交
2. 计算公式方面，当current_qps > 保存的max_qps时，直接进行更新，不进行平滑处理。

在进行了这两个处理之后，绝大部分情况下都能够在2秒左右将qps打满。

#### 平滑处理
为了减少个别窗口的抖动对限流算法的影响，同时尽量降低计算开销，在计算max_qps时，会通过使用[EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)来进行平滑处理：

```    
if current_qps > max_qps:
    max_qps = current_qps
else: 
    max_qps = current_qps * ema_alpha / 10 + (1 - ema_alpha / 10) * max_qps
```
将max_qps的ema参数置为min_latency的ema参数的十分之一的原因是: max_qps 下降了通常并不意味着极限qps也下降了。而min_latency下降了，通常意味着noload_latency确实下降了。

### 与netflix gradient算法的对比

netflix中的gradient算法公式为：max_concurrency = min_latency / latency * max_concurrency + queue_size。

其中latency是采样窗口的最小latency，min_latency是最近多个采样窗口的最小latency。min_latency / latency就是算法中的"梯度"，当latency大于min_latency时，max_concurrency会逐渐减少；反之，max_concurrency会逐渐上升，从而让max_concurrency围绕在best_max_concurrency附近。

这个公式可以和本文的算法进行类比：

* gradient算法中的latency和本算法的不同，前者的latency是最小值，后者是平均值。netflix的原意是最小值能更好地代表noload_latency，但实际上只要不对max_concurrency做定期衰减，不管最小值还是平均值都有可能不断上升使算法不收敛。最小值并不能带来额外的好处，反而会使算法更不稳定。
* gradient算法中的max_concurrency / latency从概念上和qps有关联（根据little's law)，但可能严重脱节。比如在重测
min_latency前，若所有latency都小于min_latency，那么max_concurrency会不断下降甚至到0；但按照本算法，max_qps和min_latency仍然是稳定的，它们计算出的max_concurrency也不会剧烈变动。究其本质，gradient算法在迭代max_concurrency时，latency并不能代表实际并发为max_concurrency时的延时，两者是脱节的，所以max_concurrency / latency的实际物理含义不明，与qps可能差异甚大，最后导致了很大的偏差。
* gradient算法的queue_size推荐为sqrt(max_concurrency)，这是不合理的。netflix对queue_size的理解大概是代表各种不可控环节的缓存，比如socket里的，和max_concurrency存在一定的正向关系情有可原。但在我们的理解中，这部分queue_size作用微乎其微，没有或用常量即可。我们关注的queue_size是给concurrency上升留出的探索空间: max_concurrency的更新是有延迟的，在并发从低到高的增长过程中，queue_size的作用就是在max_concurrency更新前不限制qps上升。而当concurrency高时，服务可能已经过载了，queue_size就应该小一点，防止进一步恶化延时。这里的queue_size和并发是反向关系。
