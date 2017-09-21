[/status](http://brpc.baidu.com:8765/status)  shows primary statistics of services. They shares the same sources with [/vars](../cn/vars.md) , except that they are grouped by services.

![img](../images/status.png)

Meaning of the fields above:

- **non_service_error**: the count of errors except the ones raised by the services. For example, it is a *non_service_error* that the client closes connection when the service is processing requests since no response could be written back, while it counts in the *service error* when the connection established internally in the service is broken and the service fails to get reponse from the remote side.
- **connection_count**: The number of connections to the server, excluded the ones connected to remote.
- **example.EchoService**: Full name of the service, including the package name。
- **Echo (EchoRequest) returns (EchoResponse)**: Signature of the method, a services can have multiple methods, click request/response and you can check out the corresponding protobuf message.
- **count**: Number of requests that are succesfully processed.
- **error**: Number of requests that meet failure.
- **latency**: On the web page it shows average latency in the recent *60s/60m/24h/30d* from *right to left*. On plain text page it is the average latency in recent 10s(by default, specified by [-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval).
- **latency_percentiles**: The percentail of latency at 50%, 90%, 99%, 99.9% in 10 seconds(specified by[-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval)），it shows adtional historical values on the web page.
- **latency_cdf**: Anther view of percentiles like histogram，only available on web page.
- **max_latency**: On the web page it shows the max latency in the recent *60s/60m/24h/30d* from *right to left*. On plain text page it is the max latency in recent 10s(by default, specified by [-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval).
- **qps**: On the web page it shows the qps in the recent *60s/60m/24h/30d* from *right to left*. On plain text page it is the qps in recent 10s(by default, specified by [-bvar_dump_interval](http://brpc.baidu.com:8765/flags/bvar_dump_interval).
- **processing**: The number of requests that is being processed by the service. If this is 


You can extends your servcies with [brpc::Describable](https://github.com/brpc/brpc/blob/master/src/brpc/describable.h) to customize /status page.

```c++
class MyService : public XXXService, public brpc::Describable {
public:
    ...
    void DescribeStatus(std::ostream& os, const brpc::DescribeOptions& options) const {
        os << "my_status: blahblah";
    }
};
```

An example:

![img](../images/status_2.png)
