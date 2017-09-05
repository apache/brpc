# 背景

IAM是[公有云](http://cloud.baidu.com/)的认证服务，不同于内网服务，对外服务的权限是must，每个对公有云的请求都要访问该服务，其性能必须很高。

# 改造方法

1. 替换rpc框架，从hulu-rpc替换到baidu-rpc。
2. 优化rpc回调函数 (包括移除lock, 减少外部IO次数，优化热点函数等)。

# 结论

1. 仅从hulu-rpc替换至baidu-rpc，在rpc回调函数不做任何优化的情况下，qps有25%的涨幅(从8k提升至1w)。
2. 仅替换baidu-rpc后，qps=1w，latency=1850us。
   优化rpc回调函数，同时开启超线程，增加工作线程数之后，qps=10w， latency=1730us。
   在rpc回调函数性能理想时，baidu-rpc表现出良好的扩展性，多开线程可以线性增加吞吐能力。

# 性能对比报告

|         | hulu-rpc | 仅替换baidu-rpc | 优化rpc函数+超线程+增加工作线程数 |
| ------- | -------- | ------------ | ------------------- |
| qps     | 8k       | 1w           | 10w                 |
| latency | -        | 1850us       | 1730us              |