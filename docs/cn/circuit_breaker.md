# 熔断功能
当我们发起一个rpc之后，brpc首先会从命名服务(naming service)拿到一个可用节点列表，之后根据负载均衡策略挑选出一个节点作为实际访问的节点。当某个节点出现故障时，brpc能够自动将它从可用节点列表中剔除，并周期性的对故障节点进行健康检查。

# 默认的熔断策略
brpc 默认会提供一个简单的熔断策略，在的默认的熔断策略下，brpc若检测到某个节点无法建立连接,则会将该节点熔断。当某一次rpc返回以下错误时，brpc会认为目标节点无法建立连接：ECONNREFUSED 、ENETUNREACH、EHOSTUNREACH、EINVAL。

这里需要指出的是，假如brpc发现某个节点出现连续三次连接超时(而不是rpc超时)，那么也会把第三次超时当做ENETUNREACH来处理。所以假如rpc的超时时间设置的比连接超时更短，那么当节点无法建立连接时，rpc超时会比连接超时更早触发，最终导致永远触发不了熔断。所以在自定义超时时间时，需要保证rpc的超时时间大于连接超时时间。即ChannelOptions.timeout_ms > ChannelOptions.connect_timeout_ms。

默认的熔断策略是一直开启的，并不需要做任何配置，也无法关闭。

# 可选的熔断策略
仅仅依赖上述默认的熔断策略有时候并不能完全满足需求，举个极端的例子: 假如某个下游节点逻辑线程全部卡死，但是io线程能够正常工作，那么所有的请求都会超时，但是tcp连接却能够正常建立。对于这类情况，brpc在默认的熔断策略的基础上，提供了更加激进的熔断策略。开启之后brpc会根据出错率来判断节点是否处于故障状态。

## 开启方法
可选的熔断策略默认是关闭的，用户可以根据实际需要在ChannelOptions中开启：
```
brpc::ChannelOptions option;
option.enable_circuit_breaker = true;
```

## 工作原理
可选的熔断由CircuitBreaker实现，在开启了熔断之后，CircuitBreaker会记录每一个请求的处理结果，并维护一个累计出错时长，记为acc_error_cost，当acc_error_cost > max_error_cost时，熔断该节点。

**每次请求返回成功之后，更新max_error_cost:**
1. 首先需要更新latency的EMA值，记为ema_latency:  ema_latency = ema_latency * alpha + (1 - alpha) * latency。
2. 之后根据ema_latency更新max_error_cost: max_error_cost = window_size * max_error_rate * ema_latency。


上面的window_size和max_error_rate均为gflag所指定的常量, alpha则是一个略小于1的常量，其值由window_size和下面提到的circuit_breaker_epsilon_value决定。latency则指该次请求所的耗时。

**每次请求返回之后，都会更新acc_error_cost：**
1. 如果请求处理成功，则令 acc_error_cost = alpha * acc_error_cost 
2. 如果请求处理失败，则令 acc_error_cost = acc_error_cost + min(latency, ema_latency * 2)


上面的alpha与计算max_error_cost所用到的alpha为同一个值。考虑到出现超时等错误时，latency往往会远远大于ema_latency。所以在计算acc_error_cost时对失败请求的latency进行了修正，使其值不超过ema_latency的两倍。这个倍率同样可以通过gflag配置。


**根据实际需要配置熔断参数：**

为了允许某个节点在短时间内抖动，同时又能够剔除长期错误率较高的节点，CircuitBreaker同时维护了长短两个窗口，长窗口阈值较低，短窗口阈值较高。长窗口的主要作用是剔除那些长期错误率较高的服务。我们可以根据实际的qps及对于错误的容忍程度来调整circuit_breaker_long_window_size及circuit_breaker_long_window_error_percent。

短窗口则允许我们更加精细的控制熔断的灵敏度，在一些对抖动很敏感的场景，可以通过调整circuit_breaker_short_window_size和circuit_breaker_long_window_short_percent来缩短短窗口的长度、降低短窗口对于错误的容忍程度，使得出现抖动时能够快速对故障节点进行熔断。

此外，circuit_breaker_epsilon_value可以调整窗口对于**连续抖动的容忍程度**，circuit_breaker_epsilon_value的值越低，计算公式中的alpha越小，acc_error_cost下降的速度就越快，当circuit_breaker_epsilon_value的值达到0.001时，若一整个窗口的请求都没有出错，那么正好可以把acc_error_cost降低到0。

由于计算EMA需要积累一定量的数据，在熔断的初始阶段（即目前已经收集到的请求 < 窗口大小)，会直接使用错误数量来判定是否该熔断，即：若 acc_error_count > window_size * max_error_rate 为真，则进行熔断。

## 熔断的范围
brpc在决定熔断某个节点时，会熔断掉整个连接，即：
1. 假如我们使用pooled模式，那么会熔断掉所有的连接。
2. brpc的tcp连接是会被channel所共享的，当某个连接被熔断之后，所有的channel都不能再使用这个故障的连接。
3. 假如想要避免2中所述的情况，可以通过设置ChannelOptions.connection_group将channel放进不同的ConnectionGroup，不同ConnectionGroup的channel并不会共享连接。

## 熔断数据的收集
只有通过开启了enable_circuit_breaker的channel发送的请求，才会将请求的处理结果提交到CircuitBreaker。所以假如我们决定对下游某个服务开启可选的熔断策略，最好是在所有的连接到该服务的channel里都开启enable_circuit_breaker。

## 熔断的恢复
目前brpc使用通用的健康检查来判定某个节点是否已经恢复，即只要能够建立tcp连接则认为该节点已经恢复。为了能够正确的摘除那些能够建立tcp连接的故障节点，每次熔断之后会先对故障节点进行一段时间的隔离，隔离期间故障节点即不会被lb选中，也不会进行健康检查。若节点在短时间内被连续熔断，则隔离时间翻倍。初始的隔离时间为100ms，最大的隔离时间和判断两次熔断是否为连续熔断的时间间隔都使用circuit_breaker_max_isolation_duration_ms控制，默认为30秒。

## 数据体现
节点的熔断次数、最近一次从熔断中恢复之后的累积错误数都可以在监控页面的/connections里找到，即便我们没有开启可选的熔断策略，brpc也会对这些数据进行统计。nBreak表示进程启动之后该节点的总熔断次数，RecentErr则表示该节点最近一次从熔断中恢复之后，累计的出错请求数。

由于brpc默认熔断策略是一直开启的，即便我们没有开启可选的熔断策略，nBreak还是可能会大于0，这时nBreak通常是因为tcp连接建立失败而产生的。

