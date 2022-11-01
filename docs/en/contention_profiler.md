brpc can analyze the time spent waiting for the lock and the functions that waited for.

# How to open

Turn on on demand. No configuration, no tcmalloc dependency, no need to link frame pointer or libunwind. If only brpc client or brpc is not used, see [here](dummy_server.md).

# Icon

When many threads compete for the same lock, some threads cannot obtain the lock immediately, and must sleep until a thread exits the critical section. We call this contention process **contention**. On a multi-core machine, when multiple threads need to operate the same resource but are blocked by a lock, the concurrency capabilities of multiple cores cannot be fully utilized. By providing synchronization primitives at a lower level than locks, modern OS makes competition-free locks do not require system calls at all, just one or two wait-free atomic operations that take 10-20ns, which is very fast. Once the lock contention occurs, some threads will fall into sleep and wake up again to trigger the scheduling code of the OS, at a cost of at least 3-5us. Therefore, making locks as free as possible and allowing all threads to "fly together" is an eternal topic for servers that require high performance.

After r31906, brpc supports contention profiler, which can analyze how much time is spent waiting for the lock. During the waiting process, the thread is asleep and does not occupy the CPU, so the time in the contention profiler is not the cpu time, nor does it appear in the [cpu profiler](cpu_profiler.md). The cpu profiler can catch very frequent locks (so that it costs a lot of cpu), but the time-consuming and really huge critical section is often not so frequent and cannot be found by the cpu profiler. **Contention profiler and cpu profiler seem to have a complementary relationship. The former analyzes waiting time (passive), and the latter analyzes busy time. **There is also a type of active waiting time initiated by the user based on condition or sleep, without analysis.

At present, the contention profiler supports pthread_mutex_t (non-recursive) and bthread_mutex_t. After opening, it can collect up to 1000 competing locks per second. This number is controlled by the parameter -bvar_collector_expected_per_second (which also affects rpc_dump).

| Name | Value | Description | Defined At |
| ---------------------------------- | ----- | -------- -------------------------------- | ----------------- -|
| bvar_collector_expected_per_second | 1000 | Expected number of samples to be collected per second | bvar/collector.cpp |

If the number of competing locks N exceeds 1000 in one second, then each lock will have a probability of 1000/N to be collected. In our various test scenarios (qps ranging from 100,000 to 600,000), no significant changes in the performance of the collected programs have been observed.

Let's see how to use the contention profiler through practical examples. After clicking the "contention" button (on the left side of more), the analysis process of 10 seconds by default will be started. The following figure shows the lock status of an example program in libraft. This program is the leader of the 3 node replication group, and the qps is about 100,000 to 120,000. **Total seconds: 2.449** in the upper left corner is all the waiting time spent on the lock during the collection time (10 seconds). Note that it is "waiting", the lock without contention will not be collected and will not appear in the figure below. Follow the arrow down to see which functions each time comes from.

![img](../images/raft_contention_1.png)

The picture above is a bit big, let's zoom in to see a part. The 0.768 in the red box in the figure below is the largest number in this part, which represents that raft::LogManager::get_entry waited for a total of 0.768 seconds (within 10 seconds) while waiting for the function involving bvar::detail::UniqueLockBase. If we feel that this time does not meet expectations, we can go to troubleshoot the code.

![img](../images/raft_contention_2.png)

Click the count selection box above to view the number of lock competitions. After selection, the upper left corner becomes **Total samples: 439026**, which represents the total number of lock competitions (estimated) during the acquisition time. Correspondingly, the number on the arrow in the figure has become the number of times, not the time. Comparing the time and frequency of the same result can provide a deeper understanding of the competition.

![img](../images/raft_contention_3.png)