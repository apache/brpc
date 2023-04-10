# 基于请求超时时间的限流

服务的处理能力是有客观上限的。当请求速度超过服务的处理速度时，服务就会过载。

如果服务持续过载，会导致越来越多的请求积压，最终所有的请求都必须等待较长时间才能被处理，从而使整个服务处于瘫痪状态。

与之相对的，如果直接拒绝掉一部分请求，反而能够让服务能够"及时"处理更多的请求。对应的方法就是[设置最大并发](https://github.com/apache/brpc/blob/master/docs/cn/server.md#%E9%99%90%E5%88%B6%E6%9C%80%E5%A4%A7%E5%B9%B6%E5%8F%91)。


## 算法描述
在服务正常运营过程中，流量的增减、请求体的大小变化，磁盘的顺序、随机读写，这些都会影响请求的延迟，用户一般情况下不希望请求延迟的波动造成错误，即使会有一些请求的排队造成请求延迟增加，因此，一般用户设置的请求超时时间都会是服务平均延迟的3至4倍。基于请求超时时间的限流是根据统计服务平均延迟和请求设置的超时时间相比较，来估算请求是否能够在设置的超时时间内完成处理，如果能够能完成则接受请求，如果不能完成则拒绝请求。由于统计服务平均延迟和当前请求的实际延迟会有一定的时间差，因此需要设置一个比较宽泛的最大并发度，保证服务不会因为突然的慢请求造成短时间内服务堆积过多的请求。

## 开启方法
目前只有method级别支持基于超时的限流。如果要为某个method开启基于超时的限流，只需要将它的最大并发设置为"timeout"即可，如果客户端没有开启FLAGS_baidu_std_protocol_deliver_timeout_ms，可以设置FLAGS_timeout_cl_default_timeout_ms来调整一个默认的请求超时时间，可以设置FLAGS_timeout_cl_max_concurrency来调整最大并发度。也可以通过设置brpc::TimeoutConcurrencyConf为每个method指定不同的配置。

```c++
// Set timeout concurrency limiter for all methods
brpc::ServerOptions options;
options.method_max_concurrency = "timeout";
options.method_max_concurrency = brpc::TimeoutConcurrencyConf{1, 100};

// Set timeout concurrency limiter for specific method
server.MaxConcurrencyOf("example.EchoService.Echo") = "timeout";
server.MaxConcurrencyOf("example.EchoService.Echo") = brpc::TimeoutConcurrencyConf{1, 100};
```
