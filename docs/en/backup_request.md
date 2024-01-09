Sometimes in order to ensure availability, we need to visit two services at the same time and get the result coming back first. There are several ways to achieve this in brpc:

# When backend servers can be hung in a naming service

Channel opens backup request. Channel sends the request to one of the servers and when the response is not returned after ChannelOptions.backup_request_ms ms, it sends to another server, taking the response that coming back first. After backup_request_ms is set up properly, in most of times only one request should be sent, causing no extra pressure to back-end services.

Read [example/backup_request_c++](https://github.com/apache/brpc/blob/master/example/backup_request_c++) as example code. In this example, client sends backup request after 2ms and server sleeps for 20ms on purpose when the number of requests is even to trigger backup request.

After running, the log in client and server is as following. "Index" is the number of request. After the server receives the first request, it will sleep for 20ms on purpose. Then the client sends the request with the same index. The final delay is not affected by the intentional sleep.

![img](../images/backup_request_1.png)

![img](../images/backup_request_2.png)

/rpcz also shows that the client triggers backup request after 2ms and sends the second request.

![img](../images/backup_request_3.png)

## Choose proper backup_request_ms

You can look the default cdf(Cumulative Distribution Function) graph of latency provided by brpc, or add it by your own. The y-axis of the cdf graph is a latency(us by default), and the x-axis is the proportion of requests whose latencies are less than the corresponding value in y-aixs. In the following graph, Choosing backup_request_ms=2ms could approximately cover 95.5% of the requests, while choosing backup_request_ms=10ms could cover 99.99% of the requests.

![img](../images/backup_request_4.png)

The way of adding it by yourself:

```c++
#include <bvar/bvar.h>
#include <butil/time.h>
...
bvar::LatencyRecorder my_func_latency("my_func");
...
butil::Timer tm;
tm.start();
my_func();
tm.stop();
my_func_latency << tm.u_elapsed();  // u represents for microsecond, and s_elapsed(), m_elapsed(), n_elapsed() correspond to second, millisecond, nanosecond.
 
// All work is done here. My_func_qps, my_func_latency, my_func_latency_cdf and many other counters would be shown in /vars.
```

# When backend servers cannot be hung in a naming service

[Recommended] Define a SelectiveChannel that sets backup request, in which contains two sub channel. The visiting process of this SelectiveChannel is similar to the above situation. It will visit one sub channel first. If the response is not returned after channelOptions.backup_request_ms ms, then another sub channel is visited. If a sub channel corresponds to a cluster, this method does backups between two clusters. An example of SelectiveChannel can be found in [example/selective_echo_c++](https://github.com/apache/brpc/tree/master/example/selective_echo_c++). More details please refer to the above program.

[Not Recommended] Issue two asynchronous RPC calls and join them. They cancel each other in their done callback. An example is in [example/cancel_c++](https://github.com/apache/brpc/tree/master/example/cancel_c++). The problem of this method is that the program always sends two requests, doubling the pressure to back-end services. It is uneconomical in any sense and should be avoided as much as possible.
