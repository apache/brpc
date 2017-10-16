# Overview

Locality-aware load balancing(LALB) is an algorithm that can send requests to downstream servers with the lowest latency timely and automatically. The algorithm is originated from the DP system and is now added to brpc!

Problems that LALB can solve:

- Round-robin and random policy is not good at scheduling requests since configurations and latencies of downstream servers are different.
- Downstream servers, offline servers and other servers are deployed hybridly and it is hard to predict performance.
- Automatically schedule most of the requests to the server in the same machine. When a problem occurs, try the server acrossing the machines.
- Visit the server in the same server room first. When a problem occurs, try another server room.

**...**

# Background

The most common algorithm to redirect requests is round-robin and random. The premise of these two methods is that the downstream servers and networks are similar. But in the current online environment, especially the hybrid environment, it is difficult to achieve because:

- Each machine runs a different combination of programs, along with some offline tasks, the available resources of the machine are continuously changing.
- The configurations of machines are different.
- The latencies of network are different.

These problems have always been there, but are hidden by machine monitoring from hard-working OPs. There are also some attempts in the level of frameworks. For example, [WeightedStrategy](https://svn.baidu.com/public/trunk/ub/ub_client/ubclient_weightstrategy.h) in UB redirects requests based on cpu usage of downstream machines and obviously it cannot solve the latency-related issues, or even cpu issues: since it is implemented as regularly reloading a list of weights, one can imagine that the update frequency cannot be high. A lot of requests may timeout when the load balancer reacts. And there is a math problem here: how to change cpu usage to weight. 
