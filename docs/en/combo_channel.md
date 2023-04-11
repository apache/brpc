[中文版](../cn/combo_channel.md)

With the growth of services, access patterns to downstream servers become increasingly complicated and often contain multiple RPCs in parallel or layered accesses. The complications could easily introduce tricky bugs around multi-threaded programming, which may even not be sensed by users and difficult to debug and reproduce. Moreover, implementations either support synchronous patterns only, or have totally different code for asynchronous patterns. Take running some code after completions of multiple asynchronous RPCs as an example, the synchronous pattern is often implemented as issuing multiple RPCs asynchronously and waiting for the completions respectively, while the asynchronous pattern is often implemented by a callback plus a referential count, which is decreased by one when one RPC completes. The callback is called when the count hits zero. Let's see drawbacks of the solution:

- The code is inconsistent between synchronous and asynchronous pattern and it's not trivial for users to move from one pattern to another. From the designing point of view, inconsistencies implies that  the essence is probably not grasped yet.
- Cancellation is unlikely to be supported. It's not easy to cancel a single RPC correctly, let alone combinations of RPC. However cancellations are necessary to end pointless waiting in many scenarios.
- Not composable. It's hard to enclose the implementations above as one part of a "larger" pattern. The code can hardly be reused in a different scenario.

We need a better abstraction. If several channels are combined into a larger one with different access patterns enclosed, users would be able to do synchronous, asynchronous, cancelling operations with consistent and unified interfaces. The kind of channels are called combo channels in brpc.

# ParallelChannel

`ParallelChannel` (referred to as "pchan" sometimes) sends requests to all internal sub channels in parallel and merges the responses. Users can modify requests via `CallMapper` and merge responses with `ResponseMerger`. `ParallelChannel` looks like a `Channel`:

- Support synchronous and asynchronous accesses.
- Can be destroyed immediately after initiating an asynchronous operation.
- Support cancellation.
- Support timeout.

Check [example/parallel_echo_c++](https://github.com/apache/brpc/tree/master/example/parallel_echo_c++/) for an example.

Any subclasses of `brpc::ChannelBase` can be added into `ParallelChannel`, including `ParallelChannel` and other combo channels. Set `ParallelChannelOptions.fail_limit` to control maximum number of failures. When number of failed responses reaches the limit, the RPC is ended immediately rather than waiting for timeout.

A sub channel can be added to the same `ParallelChannel` more than once, which is useful when you need to initiate multiple asynchronous RPC to the same service and wait for their completions.

Following picture shows internal structure of `ParallelChannel` (Chinese in red: can be different from request/response respectively)

![img](../images/pchan.png)

## Add sub channel

A sub channel can be added into `ParallelChannel` by following API:

```c++
int AddChannel(brpc::ChannelBase* sub_channel,
               ChannelOwnership ownership,
               CallMapper* call_mapper,
               ResponseMerger* response_merger);
```

When `ownership` is `brpc::OWNS_CHANNEL`, `sub_channel` is destroyed when the `ParallelChannel` destructs. Although a sub channel may be added into a `ParallelChannel` multiple times, it's deleted for at most once when `ownership` in one of the additions is `brpc::OWNS_CHANNEL`.

Calling ` AddChannel` during a RPC over `ParallelChannel` is **NOT thread safe**.

## CallMapper

This class converts RPCs to `ParallelChannel`  to the ones to `sub channel`. If `call_mapper` is NULL, requests to the sub channel is just the ones to `ParallelChannel`, and responses are created by calling `New()` on the responses to `ParallelChannel`. `call_mapper` is deleted when `ParallelChannel` destructs. Due to the reference counting inside, one `call_mapper` can be associated with multiple sub channels.

```c++
class CallMapper {
public:
    virtual ~CallMapper();
 
    virtual SubCall Map(int channel_index/*starting from 0*/,
                        int channel_count,
                        const google::protobuf::MethodDescriptor* method,
                        const google::protobuf::Message* request,
                        google::protobuf::Message* response) = 0;
};
```

`channel_index`: The position of the sub channel inside `ParallelChannel`, starting from zero.

`channel_count`: The sub channel count inside `ParallelChannel`.

`method/request/response`: Parameters to `ParallelChannel::CallMethod()`.

The returned `SubCall` configures the calls to the corresponding sub channel and has two special values:

- `SubCall::Bad()`: The call to ParallelChannel fails immediately and `Controller::ErrorCode()` is set to `EREQUEST`.
- `SubCall::Skip()`: Skip the call to this sub channel. If all sub channels are skipped, the call to ParallelChannel fails immediately and `Controller::ErrorCode()` is set to `ECANCELED`.

Common implementations of `Map()` are listed below:

- Broadcast the request. This is also the behavior when `call_mapper` is NULL:

```c++
  class Broadcaster : public CallMapper {
  public:
      SubCall Map(int channel_index/*starting from 0*/,
                  const google::protobuf::MethodDescriptor* method,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response) {
          // Use the method/request to pchan.
          // response is created by `new` and the last flag tells pchan to delete response after completion of the RPC
          return SubCall(method, request, response->New(), DELETE_RESPONSE);
      }
  };
```

- Modify some fields in the request before sending:

```c++
  class ModifyRequest : public CallMapper {
  public:
    SubCall Map(int channel_index/*starting from 0*/,
                const google::protobuf::MethodDescriptor* method,
                const google::protobuf::Message* request,
                google::protobuf::Message* response) {
        FooRequest* copied_req = brpc::Clone<FooRequest>(request);
        copied_req->set_xxx(...);
        // Copy and modify the request
        // The last flag tells pchan to delete the request and response after completion of the RPC
        return SubCall(method, copied_req, response->New(), DELETE_REQUEST | DELETE_RESPONSE);
    }
  };
```

- request/response already contains sub requests/responses, use them directly.

```c++
  class UseFieldAsSubRequest : public CallMapper {
  public:
    SubCall Map(int channel_index/*starting from 0*/,
                const google::protobuf::MethodDescriptor* method,
                const google::protobuf::Message* request,
                google::protobuf::Message* response) {
        if (channel_index >= request->sub_request_size()) {
            // Not enough sub_request. The caller doesn't provide same number of requests as number of sub channels in pchan
            // Return Bad() to end this RPC immediately
            return SubCall::Bad();
        }
        // Fetch the sub request and add a new sub response.
        // The last flag(0) tells pchan that there is nothing to delete.
        return SubCall(sub_method, request->sub_request(channel_index), response->add_sub_response(), 0);
    }
  };
```

## ResponseMerger

`response_merger` merges responses from all sub channels into one for the `ParallelChannel`. When it's NULL, `response->MergeFrom(*sub_response)` is used instead, whose behavior can be summarized as "merge repeated fields and overwrite the rest". If you need more complex behavior, implement `ResponseMerger`. Multiple `response_merger` are called one by one to merge sub responses so that you do not need to consider the race conditions between merging multiple responses simultaneously. The object is deleted when `ParallelChannel ` destructs. Due to the reference counting inside, `response_merger ` can be associated with multiple sub channels.

Possible values of `Result` are:

- MERGED: Successfully merged.
- FAIL: The `sub_response` was not merged successfully, counted as one failure. For example, there are 10 sub channels and `fail_limit` is 4, if 4 merges return FAIL, the RPC would reach fail_limit and end soon.
- FAIL_ALL: Directly fail the RPC.

## Get the controller to each sub channel

Sometimes users may need to know the details around sub calls. `Controller.sub(i)` gets the controller corresponding to a sub channel.

```c++
// Get the controllers for accessing sub channels in combo channels.
// Ordinary channel:
//   sub_count() is 0 and sub() is always NULL.
// ParallelChannel/PartitionChannel:
//   sub_count() is #sub-channels and sub(i) is the controller for
//   accessing i-th sub channel inside ParallelChannel, if i is outside
//    [0, sub_count() - 1], sub(i) is NULL.
//   NOTE: You must test sub() against NULL, ALWAYS. Even if i is inside
//   range, sub(i) can still be NULL:
//   * the rpc call may fail and terminate before accessing the sub channel
//   * the sub channel was skipped
// SelectiveChannel/DynamicPartitionChannel:
//   sub_count() is always 1 and sub(0) is the controller of successful
//   or last call to sub channels.
int sub_count() const;
const Controller* sub(int index) const;
```

# SelectiveChannel

[SelectiveChannel](https://github.com/apache/brpc/blob/master/src/brpc/selective_channel.h) (referred to as "schan" sometimes) accesses one of the internal sub channels with a load balancing algorithm. It's more high-level compared to ordinary channels: The requests are sent to sub channels instead of servers directly. `SelectiveChannel` is mainly for load balancing between groups of machines and shares basic properties of `Channel`:

- Support synchronous and asynchronous accesses.
- Can be destroyed immediately after initiating an asynchronous operation.
- Support cancellation.
- Support timeout.

Check [example/selective_echo_c++](https://github.com/apache/brpc/tree/master/example/selective_echo_c++/) for an example.

Any subclasses of `brpc::ChannelBase` can be added into `SelectiveChannel`, including `SelectiveChannel` and other combo channels. 

Retries done by `SelectiveChannel` are independent from the ones in its sub channels. When a call to one of the sub channels fails(which may have been retried), other sub channels are retried.

Currently `SelectiveChannel` requires **the request remains valid before completion of the RPC**, while other combo or regular channels do not. If you plan to use `SelectiveChannel` asynchronously, make sure that the request is deleted inside `done`.

## Using SelectiveChannel

The initialization of `SelectiveChannel` is almost the same as regular `Channel`, except that it doesn't need a naming service in `Init`, because `SelectiveChannel` adds sub channels dynamically by `AddChannel`, while regular `Channel` adds servers in the naming service. 

```c++
#include <brpc/selective_channel.h>
...
brpc::SelectiveChannel schan;
brpc::ChannelOptions schan_options;
schan_options.timeout_ms = ...;
schan_options.backup_request_ms = ...;
schan_options.max_retry = ...;
if (schan.Init(load_balancer, &schan_options) != 0) {
    LOG(ERROR) << "Fail to init SelectiveChannel";
    return -1;
}
```

After successful initialization, add sub channels with `AddChannel`.

```c++
// The second parameter ChannelHandle is used to delete sub channel,
// which can be NULL if this isn't necessary.
if (schan.AddChannel(sub_channel, NULL/*ChannelHandle*/) != 0) { 
    LOG(ERROR) << "Fail to add sub_channel";
    return -1;
}
```

Note that:

- Unlike `ParallelChannel`, `SelectiveChannel::AddChannel` can be called at any time, even if a RPC over the SelectiveChannel is going on. (newly added channels take effects at the next RPC).
- `SelectiveChannel` always owns sub channels, which is different from `ParallelChannel`'s configurable ownership.
- If the second parameter to `AddChannel` is not NULL, it's filled with a value typed `brpc::SelectiveChannel::ChannelHandle`, which can be used as the parameter to `RemoveAndDestroyChannel` to remove and destroy a channel dynamically.
- `SelectiveChannel` overrides timeouts in sub channels. For example, having timeout set to 100ms for a sub channel and 500ms for `SelectiveChannel`, the actual timeout is 500ms.

`SelectiveChannel`s are accessed same as regular channels.

## Example: divide traffic to multiple naming services

Sometimes we need to divide traffic to multiple naming services, because:

- Machines for one service are listed in multiple naming services.
- Machines are split into multiple groups. Requests are sent to one of the groups and then routed to one of the machines inside the group, and traffic are divided differently between groups and machines in a group.

Above requirements can be achieved by `SelectiveChannel`.

Following code creates a `SelectiveChannel` and inserts 3 regular channels for different naming services respectively.

```c++
brpc::SelectiveChannel channel;
brpc::ChannelOptions schan_options;
schan_options.timeout_ms = FLAGS_timeout_ms;
schan_options.max_retry = FLAGS_max_retry;
if (channel.Init("c_murmurhash", &schan_options) != 0) {
    LOG(ERROR) << "Fail to init SelectiveChannel";
    return -1;
}
 
for (int i = 0; i < 3; ++i) {
    brpc::Channel* sub_channel = new brpc::Channel;
    if (sub_channel->Init(ns_node_name[i], "rr", NULL) != 0) {
        LOG(ERROR) << "Fail to init sub channel " << i;
        return -1;
    }
    if (channel.AddChannel(sub_channel, NULL/*handle for removal*/) != 0) {
        LOG(ERROR) << "Fail to add sub_channel to channel";
        return -1;
    } 
}
...
XXXService_Stub stub(&channel);
stub.FooMethod(&cntl, &request, &response, NULL);
...
```

# PartitionChannel

[PartitionChannel](https://github.com/apache/brpc/blob/master/src/brpc/partition_channel.h) is a specialized `ParallelChannel` to add sub channels automatically based on tags defined in the naming service. As a result, users can list all machines in one naming service and partition them by tags. Check [example/partition_echo_c++](https://github.com/apache/brpc/tree/master/example/partition_echo_c++/) for an example.

`ParititonChannel` only supports one kind to partitioning method. When multiple methods need to coexist, or one method needs to be changed to another smoothly, try `DynamicPartitionChannel`, which creates corresponding sub `PartitionChannel` for different partitioning methods, and divide traffic to partitions according to capacities of servers. Check [example/dynamic_partition_echo_c++](https://github.com/apache/brpc/tree/master/example/dynamic_partition_echo_c++/) for an example.

If partitions are listed in different naming services, users have to implement the partitioning by `ParallelChannel` and include sub channels to corresponding naming services respectively. Refer to [the previous section](#ParallelChannel) for usages of `ParellelChannel`.

## Using PartitionChannel

First of all, implement your own `PartitionParser`. In this example, the tag's format is `N/M`, where N is index of the partition and M is total number of partitions. `0/3` means that there're 3 partitions and this is the first one of them.

```c++
#include <brpc/partition_channel.h>
...
class MyPartitionParser : public brpc::PartitionParser {
public:
    bool ParseFromTag(const std::string& tag, brpc::Partition* out) {
        // "N/M" : #N partition of M partitions.
        size_t pos = tag.find_first_of('/');
        if (pos == std::string::npos) {
            LOG(ERROR) << "Invalid tag=" << tag;
            return false;
        }
        char* endptr = NULL;
        out->index = strtol(tag.c_str(), &endptr, 10);
        if (endptr != tag.data() + pos) {
            LOG(ERROR) << "Invalid index=" << butil::StringPiece(tag.data(), pos);
            return false;
        }
        out->num_partition_kinds = strtol(tag.c_str() + pos + 1, &endptr, 10);
        if (endptr != tag.c_str() + tag.size()) {
            LOG(ERROR) << "Invalid num=" << tag.data() + pos + 1;
            return false;
        }
        return true;
    }
};
```

Then initialize the `PartitionChannel`.

```c++
#include <brpc/partition_channel.h>
...
brpc::PartitionChannel channel;
 
brpc::PartitionChannelOptions options;
options.protocol = ...;   // PartitionChannelOptions inherits ChannelOptions
options.timeout_ms = ...; // Same as above
options.fail_limit = 1;   // PartitionChannel's own settting, which means the same as that of
                          // ParalellChannel. fail_limit=1 means the overall RPC will fail 
                          // as long as only 1 paratition fails
 
if (channel.Init(num_partition_kinds, new MyPartitionParser(),
                 server_address, load_balancer, &options) != 0) {
    LOG(ERROR) << "Fail to init PartitionChannel";
    return -1;
}
// The RPC interface is the same as regular Channel
```

## Using DynamicPartitionChannel

`DynamicPartitionChannel` and `PartitionChannel` are basically same on usages. Implement `PartitionParser` first then initialize the channel, which does not need `num_partition_kinds` since `DynamicPartitionChannel` dynamically creates sub `PartitionChannel` for each partition.

Following sections demonstrate how to use `DynamicPartitionChannel` to migrate from 3 partitions to 4 partitions smoothly.

First of all, start 3 servers serving on port 8004, 8005, 8006 respectively.

```
$ ./echo_server -server_num 3
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8004
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8005
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8006
TRACE: 09-06 10:40:40:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
TRACE: 09-06 10:40:41:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
TRACE: 09-06 10:40:42:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
```

Note that each server prints summaries on traffic received in last second, which is all 0 now. 

Start a client using `DynamicPartitionChannel`, which is initialized as follows:

```c++
    ...
    brpc::DynamicPartitionChannel channel;
    brpc::PartitionChannelOptions options;
    // Failure on any single partition fails the RPC immediately. You can use a more relaxed value
    options.fail_limit = 1;                         
    if (channel.Init(new MyPartitionParser(), "file://server_list", "rr", &options) != 0) {
        LOG(ERROR) << "Fail to init channel";
        return -1;
    }
    ...
```

The content inside the naming service `file://server_list` is:

```
0.0.0.0:8004  0/3  # The first partition of the three
0.0.0.0:8004  1/3  # and so forth
0.0.0.0:8004  2/3
```

All 3 partitions are put on the server on port 8004, so the client begins to send requests to 8004 once started.

```
$ ./echo_client            
TRACE: 09-06 10:51:10:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 3 unique addresses from `server_list'
TRACE: 09-06 10:51:10:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8004 via fd=3 SocketId=0 self_port=46544
TRACE: 09-06 10:51:11:   * 0 client.cpp:226] Sending EchoRequest at qps=132472 latency=371
TRACE: 09-06 10:51:12:   * 0 client.cpp:226] Sending EchoRequest at qps=132658 latency=370
TRACE: 09-06 10:51:13:   * 0 client.cpp:226] Sending EchoRequest at qps=133208 latency=369
```

At the same time, the server on 8004 received tripled traffic due to the 3 partitions.

```
TRACE: 09-06 10:51:11:   * 0 server.cpp:192] S[0]=398866 S[1]=0 S[2]=0 [total=398866]
TRACE: 09-06 10:51:12:   * 0 server.cpp:192] S[0]=398117 S[1]=0 S[2]=0 [total=398117]
TRACE: 09-06 10:51:13:   * 0 server.cpp:192] S[0]=398873 S[1]=0 S[2]=0 [total=398873]
```

Add new 4 partitions on the server on port 8005.

```
0.0.0.0:8004  0/3
0.0.0.0:8004  1/3   
0.0.0.0:8004  2/3 
 
0.0.0.0:8005  0/4        
0.0.0.0:8005  1/4        
0.0.0.0:8005  2/4        
0.0.0.0:8005  3/4
```

Notice how summaries change. The client is aware of the modification to `server_list` and reloads it, but the QPS hardly changes.

```
TRACE: 09-06 10:57:10:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 7 unique addresses from `server_list'
TRACE: 09-06 10:57:10:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8005 via fd=7 SocketId=768 self_port=39171
TRACE: 09-06 10:57:11:   * 0 client.cpp:226] Sending EchoRequest at qps=135346 latency=363
TRACE: 09-06 10:57:12:   * 0 client.cpp:226] Sending EchoRequest at qps=134201 latency=366
TRACE: 09-06 10:57:13:   * 0 client.cpp:226] Sending EchoRequest at qps=137627 latency=356
TRACE: 09-06 10:57:14:   * 0 client.cpp:226] Sending EchoRequest at qps=136775 latency=359
TRACE: 09-06 10:57:15:   * 0 client.cpp:226] Sending EchoRequest at qps=139043 latency=353
```

The server-side summary changes more obviously. The server on port 8005 has received requests and the proportion between traffic to 8004 and 8005 is roughly 3:4.

```
TRACE: 09-06 10:57:09:   * 0 server.cpp:192] S[0]=398597 S[1]=0 S[2]=0 [total=398597]
TRACE: 09-06 10:57:10:   * 0 server.cpp:192] S[0]=392839 S[1]=0 S[2]=0 [total=392839]
TRACE: 09-06 10:57:11:   * 0 server.cpp:192] S[0]=334704 S[1]=83219 S[2]=0 [total=417923]
TRACE: 09-06 10:57:12:   * 0 server.cpp:192] S[0]=206215 S[1]=273873 S[2]=0 [total=480088]
TRACE: 09-06 10:57:13:   * 0 server.cpp:192] S[0]=204520 S[1]=270483 S[2]=0 [total=475003]
TRACE: 09-06 10:57:14:   * 0 server.cpp:192] S[0]=207055 S[1]=273725 S[2]=0 [total=480780]
TRACE: 09-06 10:57:15:   * 0 server.cpp:192] S[0]=208453 S[1]=276803 S[2]=0 [total=485256]
```

The traffic proportion between 8004 and 8005 is 3:4,  considering that each RPC needs 3 calls to 8004 or 4 calls to 8005, the client issues requests to both partitioning methods in 1:1 manner, which depends on capacities calculated recursively:

- The capacity of a regular `Channel` is sum of capacities of servers that it addresses. Capacity of a server is 1 by default if the naming services does not configure weights.
- The capacity of `ParallelChannel` or `PartitionChannel` is the minimum of all its sub channel's.
- The capacity of `SelectiveChannel` is the sum of all its sub channel's.
- The capacity of `DynamicPartitionChannel` is the sum of all its sub `PartitionChannel`'s.

In this example, capacities of the 3-partition method and the 4-partition method are both 1, since the 3 partitions are all on the server on 8004 and the 4 partitions are all on the server on 8005.

Add the server on 8006 to the 4-partition method:

```
0.0.0.0:8004  0/3
0.0.0.0:8004  1/3   
0.0.0.0:8004  2/3
 
0.0.0.0:8005  0/4   
0.0.0.0:8005  1/4   
0.0.0.0:8005  2/4   
0.0.0.0:8005  3/4    
 
0.0.0.0:8006 0/4
0.0.0.0:8006 1/4
0.0.0.0:8006 2/4
0.0.0.0:8006 3/4
```

The client still hardly changes. 

```
TRACE: 09-06 11:11:51:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 11 unique addresses from `server_list'
TRACE: 09-06 11:11:51:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8006 via fd=8 SocketId=1280 self_port=40759
TRACE: 09-06 11:11:51:   * 0 client.cpp:226] Sending EchoRequest at qps=131799 latency=372
TRACE: 09-06 11:11:52:   * 0 client.cpp:226] Sending EchoRequest at qps=136217 latency=361
TRACE: 09-06 11:11:53:   * 0 client.cpp:226] Sending EchoRequest at qps=133531 latency=368
TRACE: 09-06 11:11:54:   * 0 client.cpp:226] Sending EchoRequest at qps=136072 latency=361
```

Notice the traffic on 8006 at the server side. The capacity of the 3-partition method is still 1 while capacity of the 4-partition method increases to 2 due to the addition of the server on 8006, thus the overall proportion between the methods becomes 3:8. Each partition inside the 4-partition method has 2 instances on 8005 and 8006 respectively, between which the round-robin load balancing is applied to split the traffic. As a result, the traffic proportion between the 3 servers becomes 3:4:4.

```
TRACE: 09-06 11:11:51:   * 0 server.cpp:192] S[0]=199625 S[1]=263226 S[2]=0 [total=462851]
TRACE: 09-06 11:11:52:   * 0 server.cpp:192] S[0]=143248 S[1]=190717 S[2]=159756 [total=493721]
TRACE: 09-06 11:11:53:   * 0 server.cpp:192] S[0]=133003 S[1]=178328 S[2]=178325 [total=489656]
TRACE: 09-06 11:11:54:   * 0 server.cpp:192] S[0]=135534 S[1]=180386 S[2]=180333 [total=496253]
```

See what happens if one partition in the 3-partition method is removed: (You can comment one line in file://server_list by #)

```
 0.0.0.0:8004  0/3
 0.0.0.0:8004  1/3
#0.0.0.0:8004  2/3
 
 0.0.0.0:8005  0/4   
 0.0.0.0:8005  1/4   
 0.0.0.0:8005  2/4   
 0.0.0.0:8005  3/4    
 
 0.0.0.0:8006 0/4
 0.0.0.0:8006 1/4
 0.0.0.0:8006 2/4
 0.0.0.0:8006 3/4
```

The client senses the change in `server_list`:

```
TRACE: 09-06 11:17:47:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 10 unique addresses from `server_list'
TRACE: 09-06 11:17:47:   * 0 client.cpp:226] Sending EchoRequest at qps=131653 latency=373
TRACE: 09-06 11:17:48:   * 0 client.cpp:226] Sending EchoRequest at qps=120560 latency=407
TRACE: 09-06 11:17:49:   * 0 client.cpp:226] Sending EchoRequest at qps=124100 latency=395
TRACE: 09-06 11:17:50:   * 0 client.cpp:226] Sending EchoRequest at qps=123743 latency=397
```

The traffic on 8004 drops to zero quickly at the server side. The reason is that the 3-partition method is not complete anymore once the last 2/3 partition has been removed. The capacity becomes zero and no requests are sent to the server on 8004 anymore.

```
TRACE: 09-06 11:17:47:   * 0 server.cpp:192] S[0]=130864 S[1]=174499 S[2]=174548 [total=479911]
TRACE: 09-06 11:17:48:   * 0 server.cpp:192] S[0]=20063 S[1]=230027 S[2]=230098 [total=480188]
TRACE: 09-06 11:17:49:   * 0 server.cpp:192] S[0]=0 S[1]=245961 S[2]=245888 [total=491849]
TRACE: 09-06 11:17:50:   * 0 server.cpp:192] S[0]=0 S[1]=250198 S[2]=250150 [total=500348]
```

In real online environments, we gradually increase the number of instances on the 4-partition method and removes instances on the 3-partition method. `DynamicParititonChannel` divides the traffic based on capacities of all partitions dynamically. When capacity of the 3-partition method drops to 0, we've smoothly migrated all servers from 3 partitions to 4 partitions without changing the client-side code.
