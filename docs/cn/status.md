[English version](../en/status.md)

[/status](http://brpc.baidu.com:8765/status)可以访问服务的主要统计信息。这些信息和/vars是同源的，但按服务重新组织方便查看。

![img](../images/status.png)

上图中字段的含义分别是：

- **non_service_error**: 在service处理过程之外的错误个数。当获取到合法的service，之后发生的错误就算*service_error*，否则算*non_service_error*（比如请求解析失败，service名称不存在，请求并发度超限被拒绝等）。作为对比，服务过程中对后端服务的访问错误不是*non_service_error*。即使写出的response代表错误，此error也被记入对应的service，而不是*non_service_error*。
- **connection_count**: 向该server发起请求的连接个数。不包含记录在/vars/rpc_channel_connection_count的对外连接的个数。
- **example.EchoService**: 服务的完整名称，包含proto中的包名。
- **Echo (EchoRequest) returns (EchoResponse)**: 方法签名，一个服务可包含多个方法，点击request/response上的链接可查看对应的protobuf结构体。
- **count**: 成功处理的请求总个数。
- **error**: 失败的请求总个数。
- **latency**: 在html下是*从右到左*分别是过去60秒，60分钟，24小时，30天的平均延时。纯文本下是10秒内([-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval)控制)的平均延时。
- **latency_percentiles**: 是延时的80%, 90%, 99%, 99.9%分位值，统计窗口默认10秒([-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval)控制)，在html下有曲线。
- **latency_cdf**: 用[CDF](https://en.wikipedia.org/wiki/Cumulative_distribution_function)展示分位值, 只能在html下查看。
- **max_latency**: 在html下*从右到左*分别是过去60秒，60分钟，24小时，30天的最大延时。纯文本下是10秒内([-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval)控制)的最大延时。
- **qps**: 在html下从右到左分别是过去60秒，60分钟，24小时，30天的平均qps(Queries Per Second)。纯文本下是10秒内([-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval)控制)的平均qps。
- **processing**: (新版改名为concurrency)正在处理的请求个数。在压力归0后若此指标仍持续不为0，server则很有可能bug，比如忘记调用done了或卡在某个处理步骤上了。


用户可通过让对应Service实现[brpc::Describable](https://github.com/brpc/brpc/blob/master/src/brpc/describable.h)自定义在/status页面上的描述.

```c++
class MyService : public XXXService, public brpc::Describable {
public:
    ...
    void Describe(std::ostream& os, const brpc::DescribeOptions& options) const {
        os << "my_status: blahblah";
    }
};
```

比如:

![img](../images/status_2.png)
