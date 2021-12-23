# Overview

In some scenarios, we want the same request to fall on one machine as much as possible. For example, when accessing a cache cluster, we often hope that the same request can fall on the same backend to make full use of the existing cache. Different machines carry different loads. The stable working set. Rather than randomly scattered on all machines, that would force all machines to cache all the content, and eventually perform poorly due to not being able to store it and forming a bump. We all know that hash can meet this requirement. For example, when there are n servers, the input x will always be sent to the hash(x)% n servers. But when the server becomes m, hash(x)% n and hash(x)% m are likely to be different, which will change the sending destination of almost all requests. If the destination is a cache service, all The cache will become invalid, causing a request storm for the database or computing service that was originally blocked by the cache, triggering an avalanche. Consistent hashing is a special hashing algorithm. When adding servers, only a part of the requests sent to each old node will be transferred to the new node, thus realizing a smooth migration. [This paper](http://blog.phpdr.net/wp-content/uploads/2012/08/Consistent-Hashing-and-Random-Trees.pdf) puts forward the concept of consistent hash.

The consistent hash satisfies the following four properties:

-Balance: The probability of each node being selected is O(1/n).
-Monotonicity: When a new node joins, there will be no request to move between the old nodes, but only move from the old node to the new node. When a node is deleted, it will not affect the requests that fall on other nodes.
-Spread: When the upstream machine sees different downstream lists (common in online and unstable networks), the same request is mapped to a small number of nodes as much as possible.
-Load: When upstream machines see different downstream lists, ensure that the number of requests allocated to each downstream machine is as consistent as possible.

# Method to realize

The 32-bit hash values ​​of all servers form a ring (Hash Ring) on ​​the 32-bit integer value range. Each interval on the ring corresponds to a server uniquely. If a key falls within a certain interval, it will be diverted to the corresponding On the server.

![img](../images/chash.png)

When deleting a server, its corresponding interval will belong to the adjacent server, and all requests will run through. When a server is added, it will divide the interval of a certain server and carry all the requests falling in this interval. It is difficult to satisfy the attributes mentioned in the previous section by using Hash Ring alone. There are two main problems:

-When the number of machines is small, the interval size will be unbalanced.
-When a machine fails, its pressure will be completely transferred to another machine and may not be able to carry it.

In order to solve this problem, we calculate m hash values ​​for each server, thereby dividing the 32-bit integer value range into n*m intervals. When the key falls into a certain interval, it is distributed to the corresponding server. Those extra hash values ​​make the interval division more even and are called virtual nodes. When a server is deleted, its corresponding m intervals will be merged into adjacent intervals, and the requests on that server will be transferred to other servers more evenly. When a server is added, it will divide m existing intervals and transfer some requests from the corresponding server.

Because node failures and changes do not occur frequently, we choose to modify the ordered array with O(n) complexity to store the hash ring. Each time the split is performed, a binary search is used to select the corresponding machine. Since the storage is continuous, the search efficiency is better than High realization based on balanced binary tree. For thread safety, please refer to the [Double Buffered Data](lalb.md#doublybuffereddata) chapter.

# How to use

We have built-in implementations of two hash algorithms based on murmurhash3 and md5 respectively. Two things must be done to use:

-Specify *load_balancer_name* as "c_murmurhash" or "c_md5" in Channel.Init.
-When initiating rpc, fill in the requested hash code through Controller::set_request_code(uint64_t).

> The hash algorithm of request does not need to be consistent with the hash algorithm of lb, only that the value range of the hash is a 32-bit unsigned integer. Since memcache uses md5 by default, please choose c_md5 to ensure compatibility when accessing memcached clusters. For other scenarios, you can choose c_murmurhash to obtain higher performance and more even distribution.

# Number of virtual nodes

The default number of virtual nodes can be set through -chash\_num\_replicas, and the default value is 100. For some special occasions, there are custom requirements for the number of virtual nodes, which can be configured by adding *load_balancer_name* to the parameter replicas=<num>, such as:
```c++
channel.Init("http://...", "c_murmurhash:replicas=150", &options);
```