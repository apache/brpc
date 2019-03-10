# 熔断功能
当我们发起一个rpc之后，brpc首先会从名字服务(naming service)拿到一个可用节点列表，之后根据负载均衡策略挑选出一个节点作为实际访问的节点。当某个节点出现故障时，brpc能够自动将它从可用节点列表中剔除，并周期性的对故障节点进行健康检查。

# 保守的熔断策略
brpc 默认会提供保守的熔断策略，在保守的熔断策略下，brpc只有在发现节点无法建立连接时才会将该节点熔断。当某一次rpc返回以下错误时，brpc会认为目标节点无法建立连接，并进行熔断：ECONNREFUSED 、ENETUNREACH、EHOSTUNREACH、EINVAL。

这里需要指出的是，假如brpc发现某个节点出现连续三次连接超时(而不是rpc超时)，那么也会把第三次超时当做ENETUNREACH来处理。所以假如rpc的超时时间设置的比连接超时更短，那么当节点无法建立连接时，rpc超时会比连接超时更早触发，最终导致永远触发不了熔断。所以在自定义超时时间时，需要保证rpc的超时时间大于连接超时时间。即ChannelOptions.timeout_ms > ChannelOptions.connect_timeout_ms。

保守的熔断策略是一直开启的，并不需要做任何配置，也无法关闭。

# 更激进的熔断策略
仅仅依赖上述保守的熔断策略有时候并不能完全满足需求，举个极端的例子: 假如某个下游节点逻辑线程全部卡死，但是io线程能够正常工作，那么所有的请求都会超时，但是tcp连接却能够正常建立。对于这类情况，brpc提供了更加激进的熔断策略：当某个节点的出错率高于预期值时，也会主动将该节点进行摘除。

## 开启方法
激进的熔断策略默认是关闭的，用户可以根据需要在ChannelOptions中手动开启：
```
brpc::ChannelOptions option;
option.enable_circuit_breaker = true;
```

## 工作原理
激动的熔断由CircuitBreaker实现，在开启了熔断之后，CircuitBreaker会记录每一个请求的处理结果，并维护一个累计出错时长，记为acc_error_cost，当acc_error_cost > max_error_cost时，熔断该节点。


**error_cost的计算过程如下：**
1. 如果请求处理成功，则令 acc_error_cost = alpha * acc_error_cost (alpha 为常数，由window_size决定)
2. 如果请求处理失败，则令 acc_error_cost = acc_error_cost + 该次请求的latency


**max_error_cost的计算如下:**
1. 计算出当前latency的EMA值，记为 ema_latency，当请求处理成功时， ema_latency = ema_latency * alpha + (1 - alpha) * latency，否则不更新ema_latency
2. max_error_cost = window_size * max_error_rate * ema_latency (window_size和max_error_rate均为常量，通过gflag配置)
考虑到超时等错误的latency往往会远远大于平均latency，当请求处理失败时，会对其latency进行修正之后再进行error_cost的计算，修正之后的latency不超过ema_latency的两倍。(最大的倍率可以通过gflag配置)

**根据实际需要配置熔断参数：**

为了允许某个节点在短时间内抖动，同时又能够剔除长期错误率较高的节点，CircuitBreaker同时维护了长短两个窗口，长窗口阈值较低，短窗口阈值较高。长窗口的主要作用是剔除那些长期错误率较高的服务。我们可以根据实际的qps及对于错误的容忍程度来调整circuit_breaker_long_window_size及circuit_breaker_long_window_error_percent。 

短窗口则允许我们更加精细的控制熔断的灵敏度，在一些对抖动很敏感的场景，可以通过调整circuit_breaker_short_window_size和circuit_breaker_long_window_short_percent来缩短短窗口的长度、降低短窗口对于错误的容忍程度，使得出现抖动时能够更快的进行熔断。

此外，circuit_breaker_epsilon_value可以调整窗口对于**连续抖动的容忍程度**，circuit_breaker_epsilon_value的值越低，acc_error_cost下降的速度越快，当circuit_breaker_epsilon_value的值达到0.001时，若一整个窗口的请求都没有出错，那么正好可以把acc_error_cost降低到0。

由于计算ema需要积累一定量的数据，在熔断的初始阶段（即目前已经收集到的请求 < 窗口大小)，会直接使用错误数量来判定是否该熔断，即： acc_error_count > window_size * max_error_rate 为真，则熔断节点。

## 熔断的范围
brpc在决定熔断某个节点时，会熔断掉整个连接，即：
1. 假如我们使用pooled模式，那么会熔断掉所有的连接。
2. brpc的tcp连接是会被所有的channel所共享的，当某个连接被熔断之后，所有的channel都不能再使用这个故障的连接。
3. 假如想要避免2中所述的情况，可以通过设置ChannelOptions.connection_group，不同ConnectionGroup的channel并不会共享连接。

## 熔断数据的收集
只有通过开启了enable_circuit_breaker的channel发送的请求，才会将请求的处理结果提交到CircuitBreaker。所以假如我们决定对下游某个服务开启单节点熔断，最好是在所有的连接到该服务的channel里都开启enable_circuit_breaker。

## 熔断的恢复
目前brpc使用通用的健康检查来判定某个节点是否已经恢复，即只要能够建立tcp连接则认为该节点已经恢复。为了能够正确隔离那些能够建立tcp连接的故障节点，每次熔断之后会先对节点进行一段时间的隔离。当节点在短时间内被连续熔断，则隔离时间翻倍。最大的隔离时间和判断两次熔断是否为连续熔断的时间间隔都使用circuit_breaker_max_isolation_duration_ms控制，默认为30秒

## 数据体现
节点的熔断次数、最近一次恢复之后的累积错误数都可以在监控页面的connections里找到，即便我们没有在ChannelOptions里开启了enable_circuit_breaker，都会对这些数据进行统计。nBreak表示进程启动之后该节点的总熔断次数，RecentErr则表示节点最近一次从熔断中回复之后，出错的次数。

假如没有开启enable_circuit_breaker，那么熔断次数就是brpc自带的保守熔断策略发生的熔断，这通常是tcp连接失败/连续三次连接超时导致的。
