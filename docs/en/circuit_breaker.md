# Fuse function
When we initiate an rpc, brpc will first get a list of available nodes from the naming service, and then select a node as the actually visited node according to the load balancing strategy. When a node fails, brpc can automatically remove it from the list of available nodes, and periodically perform health checks on the failed node.

# Default circuit breaker strategy
By default, brpc will provide a simple fuse strategy. Under the default fuse strategy, if brpc detects that a node cannot establish a connection, it will fuse the node. When a certain rpc returns the following errors, brpc will think that the target node cannot establish a connection: ECONNREFUSED, ENETUNREACH, EHOSTUNREACH, EINVAL.

What needs to be pointed out here is that if brpc finds that a node has three consecutive connection timeouts (instead of rpc timeout), then the third timeout will also be treated as ENETUNREACH. So if the rpc timeout is set to be shorter than the connection timeout, then when the node cannot establish a connection, the rpc timeout will be triggered earlier than the connection timeout, which will eventually cause the fuse to never be triggered. Therefore, when customizing the timeout time, you need to ensure that the rpc timeout time is greater than the connection timeout time. That is, ChannelOptions.timeout_ms> ChannelOptions.connect_timeout_ms.

The default fusing strategy is always on, no configuration is required, and it cannot be turned off.

# Optional fusing strategy
Relying only on the above-mentioned default circuit breaker strategy sometimes cannot fully meet the demand. Take an extreme example: if a downstream node logic thread is all stuck, but the io thread can work normally, then all requests will time out, but the tcp connection Can be established normally. For such situations, brpc provides a more radical fusing strategy on the basis of the default fusing strategy. After being turned on, brpc will determine whether the node is in a fault state based on the error rate.

## How to open
The optional circuit breaker strategy is turned off by default, and users can turn it on in ChannelOptions according to actual needs:
```
brpc::ChannelOptions option;
option.enable_circuit_breaker = true;
```

## working principle
Optional fusing is implemented by CircuitBreaker. After fusing is turned on, CircuitBreaker will record the processing result of each request and maintain a cumulative error duration, which is recorded as acc_error_cost. When acc_error_cost> max_error_cost, the node will be fused.

**After each request returns successfully, update max_error_cost:**
1. First, you need to update the [EMA](https://en.wikipedia.org/wiki/Moving_average) value of latency, which is recorded as ema_latency: ema_latency = ema_latency * alpha + (1-alpha) * latency.
2. Then update max_error_cost according to ema_latency: max_error_cost = window_size * max_error_rate * ema_latency.


The above window_size and max_error_rate are constants specified by gflag, and alpha is a constant slightly less than 1, and its value is determined by window_size and circuit_breaker_epsilon_value mentioned below. Latency refers to the time consumed by the request.

**After each request is returned, acc_error_cost will be updated:**
1. If the request is processed successfully, let acc_error_cost = alpha * acc_error_cost
2. If the request processing fails, let acc_error_cost = acc_error_cost + min(latency, ema_latency * 2)


The above alpha is the same value as the alpha used to calculate max_error_cost. Taking into account the timeout and other errors, latency is often much greater than ema_latency. Therefore, when calculating acc_error_cost, the latency of the failed request is corrected so that its value does not exceed twice ema_latency. This magnification can also be configured through gflag.


**Configure the fusing parameters according to actual needs:**

In order to allow a node to jitter in a short period of time and at the same time eliminate nodes with a high long-term error rate, CircuitBreaker maintains both long and short windows at the same time, with a lower threshold for the long window and a higher threshold for the short window. The main function of the long window is to eliminate those services with a high long-term error rate. We can adjust circuit_breaker_long_window_size and circuit_breaker_long_window_error_percent according to the actual qps and tolerance for errors.

The short window allows us to more finely control the sensitivity of the fuse. In some scenes that are sensitive to jitter, you can shorten the length of the short window and reduce the tolerance of the short window to errors by adjusting the circuit_breaker_short_window_size and circuit_breaker_short_window_error_percent, so that it can be fast when jitter occurs. Fuse the faulty node.

In addition, circuit_breaker_epsilon_value can adjust the tolerance of the window to **continuous jitter**. The lower the value of circuit_breaker_epsilon_value, the smaller the alpha in the calculation formula, and the faster the acc_error_cost will decrease. When the value of circuit_breaker_epsilon_value reaches 0.001, if a whole There is no error in the request of the window, so the acc_error_cost can be reduced to 0.

Since the calculation of EMA needs to accumulate a certain amount of data, in the initial stage of fusing (that is, the requests that have been collected so far <window size), the number of errors will be directly used to determine whether the fusing should be performed, that is: if acc_error_count> window_size * max_error_rate is true, Then fuse.

## Fuse range
When brpc decides to fuse a node, it will fuse the entire connection, namely:
1. If we use the pooled mode, all connections will be blown off.
2. The tcp connection of brpc will be shared by the channel. When a connection is broken, all channels can no longer use the faulty connection.
3. If you want to avoid the situation described in 2, you can put the channel into different ConnectionGroup by setting ChannelOptions.connection_group, and the channels of different ConnectionGroup will not share the connection.

## Fuse data collection
Only the request sent through the channel with enable_circuit_breaker turned on will the processing result of the request be submitted to CircuitBreaker. So if we decide to enable an optional circuit breaker strategy for a downstream service, it is best to enable enable_circuit_breaker in all channels connected to the service.

## Fuse recovery
At present, brpc uses a general health check to determine whether a node has been restored, that is, as long as a tcp connection can be established, the node is considered to have been restored. In order to correctly remove those faulty nodes that can establish a tcp connection, the faulty node will be isolated for a period of time after each fuse. During the isolation period, the faulty node will not be selected by lb, and no health check will be performed. If the node is continuously blown in a short period of time, the isolation time is doubled. The initial isolation time is 100ms. The maximum isolation time and the time interval for judging whether two fusing is continuous fusing are controlled by circuit_breaker_max_isolation_duration_ms, and the default is 30 seconds.

## Data reflection
The number of fusing times of the node and the cumulative number of errors after the last recovery from the fusing can be found in the /connections of the monitoring page. Even if we do not enable the optional fusing strategy, brpc will count these data. nBreak represents the total number of fusing times of the node after the process is started, and RecentErr represents the cumulative number of error requests after the node was last recovered from the fusing.

Since brpc's default fuse strategy is always on, even if we do not enable the optional fuse strategy, nBreak may still be greater than 0. At this time, nBreak is usually caused by the failure of tcp connection establishment.
