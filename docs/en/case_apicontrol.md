# Progress

| Time | Content | Description |
| ----------- | ------------------------------------- | -------------- |
| 8.11-8.28 | Research + R&D + Self-test | See the attachment for the self-test performance report |
| 9.8-9.22 | QA test | QA test report see attachment |
| 10.8 | 1 machine in Beijing computer room goes online | |
| 10.14 | One machine in Beijing computer room goes online | Fix URL encoding problem |
| 10.19 | Beijing computer room 7/35 machines go online, 2 machines in Hangzhou and Nanjing each go online | Start small traffic online |
| 10.22 | Beijing computer room 10/35 machine online Hangzhou computer room 5/26 machine online Nanjing computer room 5/19 machine online | fix http response data compression problem |
| 11.3 | Beijing computer room 10/35 machine online | Fix RPC memory leak problem |
| 11.6 | Hangzhou computer room 5/26 machine online, Nanjing computer room 5/19 machine online | same as Beijing computer room version |
| 11.9 | Beijing computer room is online with full data | |

Up to now, the performance of online services has been stable.

# QA test conclusion

1. [Performance test] Single machine supports maximum QPS: **9000+**. It can effectively solve the problem of a slow service in the original hulu_pbrpc that drags down all services. The performance is very good.
2. [Stability Test] There is no problem with the long-term pressure test.

QA test conclusion: passed

# Performance improvement real-time statistics

Statistics time 2015.11.3 15:00 – 2015.11.9 14:30, a total of **143.5** hours (nearly 6 days) of uninterrupted operation. Noah monitoring data of 12 machines in the same machine room before and after the upgrade in the Beijing machine room.

| Indicators | Before the upgrade ** mean hulu_pbrpc | After the upgrade ** mean brpc | Income comparison | Description                                                            |
| --- | --- | --- | --- |------------------------------------------------------------------------|
| CPU usage | 67.35% | 29.28% | Reduce **56.53**% |                                                                        |
| Memory usage | 327.81MB | 336.91MB | Basically the same |                                                                        |
| Authentication level (ms) | 0.605 | 0.208 | Reduce **65.62**% |                                                                        |
| Flat forwarding (ms) | 22.49 | 23.18 | Basically the same | Rely on the performance of various back-end services                   |
| Total number of threads | 193 | 132 | Reduced **31.61**% | Baidu RPC version has a lower usage rate of threads and can be reduced |
| Extreme QPS | 3000 | 9000 | Improved **3** times | Offline testing using Geoconv and Geocoder services                    |

**CPU Usage (%)** (Red is before upgrade, blue is after upgrade)
![img](../images/apicontrol_compare_1.png)

**Memory usage (KB)** (red is before upgrade, blue is after upgrade)
![img](../images/apicontrol_compare_2.png)

**Authentication level (ms)** (red is before the upgrade, blue is after the upgrade)
![img](../images/apicontrol_compare_3.png)

** Forwarding sound (ms) ** (red is before the upgrade, blue is after the upgrade)
![img](../images/apicontrol_compare_4.png)

**Total number of threads (a)** (red is before upgrade, blue is after upgrade)
![img](../images/apicontrol_compare_5.png)