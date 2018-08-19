"Avalanche" refers to the phenomenon that the vast majority of requests are timed out when accessing a service cluster and can not be recovered when the traffic decreases. Next we explain the source of this phenomenon.

When the number of request exceeds the maximum qps of service, the service will not work properly; when the traffic is back to normal(less than the service processing capacity), the backlog requests will be processed. Although most of them may be timed out due to not being processed timely, the service itself will generally return to normal. This is just like a pool has a water inlet and a water outlet, if the amount of water in is greater than that of water out, the pool will eventually be full and more water will overflow. However, if the amount of water in is less than that of water out, the pool will be eventually empty after a period of time.


