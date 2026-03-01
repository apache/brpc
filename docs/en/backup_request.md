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

## Rate-limited backup requests

To limit the ratio of backup requests sent, use the built-in factory function or implement the `BackupRequestPolicy` interface yourself.

Priority order: `backup_request_policy` > `backup_request_ms`.

### Using the built-in rate-limiting policy

Call `CreateRateLimitedBackupPolicy` and set the result on `ChannelOptions.backup_request_policy`:

```c++
#include "brpc/backup_request_policy.h"
#include <memory>

brpc::RateLimitedBackupPolicyOptions opts;
opts.backup_request_ms = 10;       // send backup if RPC does not complete within 10ms
opts.max_backup_ratio = 0.3;       // cap backup requests at 30% of total
opts.window_size_seconds = 10;     // sliding window width in seconds
opts.update_interval_seconds = 5;  // how often the cached ratio is refreshed

// The caller owns the returned pointer.
// Use unique_ptr to manage the lifetime; ensure the policy outlives the channel.
std::unique_ptr<brpc::BackupRequestPolicy> policy(
    brpc::CreateRateLimitedBackupPolicy(opts));

brpc::ChannelOptions options;
options.backup_request_policy = policy.get(); // NOT owned by channel
channel.Init(..., &options);
// policy is released automatically when unique_ptr goes out of scope,
// as long as it outlives the channel.
```

`RateLimitedBackupPolicyOptions` fields:

| Field | Default | Description |
|-------|---------|-------------|
| `backup_request_ms` | -1 | Timeout threshold in ms; -1 means inherit from `ChannelOptions.backup_request_ms`; must be >= -1. **Only effective when the policy is set via `ChannelOptions.backup_request_policy`; controller-level injection always requires an explicit >= 0 value.** |
| `max_backup_ratio` | 0.1 | Max backup ratio; range (0, 1] |
| `window_size_seconds` | 10 | Sliding window width in seconds; range [1, 3600] |
| `update_interval_seconds` | 5 | Cached-ratio refresh interval in seconds; must be >= 1 |

`CreateRateLimitedBackupPolicy` returns `NULL` if any parameter is invalid.

### Using a custom BackupRequestPolicy

For full control, implement the `BackupRequestPolicy` interface and set it on `ChannelOptions.backup_request_policy`:

```c++
#include "brpc/backup_request_policy.h"

class MyBackupPolicy : public brpc::BackupRequestPolicy {
public:
    int32_t GetBackupRequestMs(const brpc::Controller*) const override {
        return 10; // send backup after 10ms
    }
    bool DoBackup(const brpc::Controller*) const override {
        return should_allow_backup(); // your logic here
    }
    void OnRPCEnd(const brpc::Controller*) override {
        // called on every RPC completion; update stats if needed
    }
};

MyBackupPolicy my_policy;
brpc::ChannelOptions options;
options.backup_request_policy = &my_policy; // NOT owned by channel; must outlive channel
channel.Init(..., &options);
```

### Implementation notes

- The ratio is computed over a sliding time window using bvar counters. The cached value is refreshed at most once per `update_interval_seconds` using a lock-free CAS election, so the overhead per RPC is very low (two atomic loads in the common path).
- Backup decisions are counted immediately at decision time (before the RPC completes) to provide faster feedback during latency spikes. Total RPCs are counted on completion. This means the ratio may transiently lag during a spike, but this is intentional — the limiter is designed for approximate, best-effort throttling, not exact enforcement.
- Each channel using rate limiting maintains two `bvar::Window` sampler tasks. Keep this in mind in deployments with a very large number of channels.

# When backend servers cannot be hung in a naming service

[Recommended] Define a SelectiveChannel that sets backup request, in which contains two sub channel. The visiting process of this SelectiveChannel is similar to the above situation. It will visit one sub channel first. If the response is not returned after channelOptions.backup_request_ms ms, then another sub channel is visited. If a sub channel corresponds to a cluster, this method does backups between two clusters. An example of SelectiveChannel can be found in [example/selective_echo_c++](https://github.com/apache/brpc/tree/master/example/selective_echo_c++). More details please refer to the above program.

[Not Recommended] Issue two asynchronous RPC calls and join them. They cancel each other in their done callback. An example is in [example/cancel_c++](https://github.com/apache/brpc/tree/master/example/cancel_c++). The problem of this method is that the program always sends two requests, doubling the pressure to back-end services. It is uneconomical in any sense and should be avoided as much as possible.
