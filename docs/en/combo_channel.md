With the growth of the number of business products, the access pattern to downstream becomes increasingly complicate, which often contains multiple simultaneous RPCs or subsequent asynchronous ones. However, these could easily introduce very tricky bugs under multi-threaded environment, of which users may not even aware, and it's also difficult to debug and reproduce. Moreover, implementations may not provide full support for various access patterns, in which case you have to write your own. Take semi-synchronous RPC as an example, which means waiting for multiple asynchronous RPCs to complete. A common implementation for synchronous access would be issuing multiple requests asynchronously and waiting for their completion, while the implementation for asynchronous access makes use of a callback with a counter. Each time an asynchronous RPC finishes, the counter decrement itself until zero in which case the callback is called. Now let's analyze their weakness:

- The code is inconsistent between synchronous pattern and asynchronous one. It's difficult for users to move from one pattern to another. From the design point of view, inconsistencies suggest lose of essence.
- Cancellation is not supported in general. It's not easy to cancel an RPC in time correctly, let alone a combination of access. Most implementations do not support cancellation of a combo access. However, it's a must for some speed up technique such as backup request.
- Cascading is not supported, which means it's hard to turn a semi-synchronous access into one part of a "larger" access. Code may meet the current needs, but it's not generic.

As a result, we need a better abstraction. If there is a structure whose combination is still the same structure, the interface to synchronous access, asynchronous one, cancellation and other operations would be the same for users. In fact, we already have this structure `Channel`. If we can combine some channels into larger and more complex ones in different ways along with different access patterns, then users will be armed with a consistent and modular building block. Welcome to this powerful tool.

# ParallelChannel

`ParallelChannel` (referred as "pchan") sends requests to all the sub channels inside at the same time and merges their results. The user can modify the request via `CallMapper` and merge the results with `ResponseMerger`. `ParallelChannel` looks like a `Channel`:

- Support synchronous and asynchronous access.
- Can be destroyed immediately after initiating an asynchronous operation.
- Support cancellation.
- Support timeout.

The sample code is shown in [example/parallel_echo_c++](https://github.com/brpc/brpc/tree/master/example/parallel_echo_c++/).

Any subclasses of `brpc::ChannelBase` can join `ParallelChannel`, including `ParallelChannel` and other combo channels. The user can set `ParallelChannelOptions.fail_limit` to control the maximum number of acceptable failure. When the failed results reach this number, RPC will end immediately without waiting for timeout.

A sub channel can be added to the same `ParallelChannel` for multiple times. This is useful when you need to initiate multiple asynchronous visits to the same service and wait for them to complete.

The following picture shows the internal structure of the `ParallelChannel`:

![img](../images/pchan.png)

## Add sub channel

You can add a sub channel into `ParallelChannel` using the following API:

```c++
int AddChannel(brpc::ChannelBase* sub_channel,
               ChannelOwnership ownership,
               CallMapper* call_mapper,
               ResponseMerger* response_merger);
```

When `ownership` is `brpc::OWNS_CHANNEL`, the `sub_channel` will be destroyed when the `ParallelChannel` destructs. Since a sub channel can be added to a `ParallelChannel` multiple times, it will be deleted (only once) as long as one of the parameter `ownership` is `brpc::OWNS_CHANNEL`.

Calling ` AddChannel` during a `ParallelChannel` RPC  is **NOT thread safe**.

## CallMapper

This class converts `ParallelChannel` requests to `sub channel` ones. If `call_mapper` is NULL, the request for the sub channel is exactly the same as that for `ParallelChannel`, and the response is created by calling  `New()` on `ParallelChannel`'s response. If `call_mapper` is not NULL, it will be deleted when `ParallelChannel` destructs. Due to the reference count inside, `call_mapper` can be associated with multiple sub channels.

```c++
class CallMapper {
public:
    virtual ~CallMapper();
 
    virtual SubCall Map(int channel_index/*starting from 0*/,
                        const google::protobuf::MethodDescriptor* method,
                        const google::protobuf::Message* request,
                        google::protobuf::Message* response) = 0;
};
```

`channel_index`: The position of the sub channel inside `ParallelChannel`, starting from zero.

`method/request/response`: Parameters fro `ParallelChannel::CallMethod()`.

Return `SubCall` to control the corresponding sub channel. It has two special values:

- `SubCall::Bad()`: The current visit to `ParallelChannel` fails immediately with `Controller::ErrorCode()` being `EREQUEST`.
- `SubCall::Skip()`: To skip RPC to this sub channel. If all sub channels have been skipped, the request fails immediately with `Controller::ErrorCode()` being `ECANCELED`.

The common implementations of `Map()` are listed below:

- Broadcast request, which is also the behavior when `call_mapper` is NULL:

```c++
  class Broadcaster : public CallMapper {
  public:
      SubCall Map(int channel_index/*starting from 0*/,
                  const google::protobuf::MethodDescriptor* method,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response) {
          // Keep method/request to be same as those of pchan
          // response is created by `new`
          // The last flag tells pchan to delete response after RPC
          return SubCall(method, request, response->New(), DELETE_RESPONSE);
      }
  };
```

- Modify some fields in request before sending:

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
        // The last flag tells pchan to delete request and response after RPC
        return SubCall(method, copied_req, response->New(), DELETE_REQUEST | DELETE_RESPONSE);
    }
  };
```

- request/response already contains sub request/response. Use them to access sub channel directly.

```c++
  class UseFieldAsSubRequest : public CallMapper {
  public:
    SubCall Map(int channel_index/*starting from 0*/,
                const google::protobuf::MethodDescriptor* method,
                const google::protobuf::Message* request,
                google::protobuf::Message* response) {
        if (channel_index >= request->sub_request_size()) {
            // Not enough sub_request
            // The caller doesn't provide the same number of requests 
            // as number of sub channels in pchan
            // Return Bad() to end this RPC immediately with EREQUEST
            return SubCall::Bad();
        }
        // Fetch the corresponding sub request
        // Add a new sub response
        // The last flag tells pchan there is no need to delete anything 
        // since sub request/response will be destroyed with request/response
        return SubCall(sub_method, request->sub_request(channel_index), response->add_sub_response(), 0);
    }
  };
```

## ResponseMerger

`response_merger` merges the response of all sub channels into the overall one. When it's NULL, `response->MergeFrom(*sub_response)` will be used instead, whose behavior can be summarized as "merge all the repeated fields and overwrite the rest". Your can implement `ResponseMerger` to achieve more complex behavior. `response_merger` will be used to merge sub response one by one so that you do not need to consider merging multiple response at the same time. It will be deleted when `ParallelChannel ` destructs if it's not NULL. Due to the reference count inside, `response_merger ` can be associated with multiple sub channels.

The accepted values of `Result` are:

- MERGED: Successful merged.
- FAIL (known as IGNORE): Count as one failure of merging. For example, if there are 10 sub channels & the `fail_limit=4` while 3 of which has already failed, a final merging failure will end this RPC with error at once due to the `fail_limit`.
- FAIL_ALL (known as CALL_FAILED): Immediately fails this RPC.

## Get the controller object of each sub channel

Sometimes users may need the details of each sub channel. This can be done by `Controller.sub(i)` to get the controller corresponding to a specific sub channel.

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

[SelectiveChannel](https://github.com/brpc/brpc/blob/master/src/brpc/selective_channel.h) ("referred as schan") wraps multiple `Channel` using a specific load balancing algorithm to achieve a higher level of `Channel`. The requests will be sent to the sub channel rather than the specific Server. `SelectiveChannel` is mainly used to do load balancing between groups of machines. It has some basic properties of `Channel`:

- Support synchronous and asynchronous access.
- Can be destroyed immediately after initiating an asynchronous operation.
- Support cancellation.
- Support timeout.

The sample code is shown in [example/selective_echo_c++](https://github.com/brpc/brpc/tree/master/example/selective_echo_c++/).

Any subclasses of `brpc::ChannelBase` can join `SelectiveChannel`, including `SelectiveChannel` and other combo channels. 

The retry mechanism of `SelectiveChannel` is independent of its sub channels. When the access between `SelectiveChannel ` and one of its sub channel fails (Note that the sub channel may already retried for a couple of times), it will retry another sub channel.

Currently `SelectiveChannel` demands all requests remain valid until the end of RPC, while other channels do not have this requirement. If you plan to use `SelectiveChannel` asynchronously, make sure that the request is deleted inside `done`.

## Use SelectiveChannel

The initialization of `SelectiveChannel` is almost the same as regular `Channel`, while it doesn't need a naming service parameter in `Init`. The reason is that `SelectiveChannel` is sub channel oriented and sub channels can be added into by `AddChannel` dynamically, but regular `Channel` is server oriented which has to be recorded in naming service. 

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

After a successful initialization, add sub channel using `AddChannel`.

```c++
// The second parameter ChannelHandle is used to delete sub channel,
// which can be NULL if this isn't necessary.
if (schan.AddChannel(sub_channel, NULL/*ChannelHandle*/) != 0) { 
    LOG(ERROR) << "Fail to add sub_channel";
    return -1;
}
```

Note that:

- Unlike `ParallelChannel`, `SelectiveChannel::AddChannel` can be called at any time, even if the it's being used during RPC (which takes effect at the next access).
- `SelectiveChannel` always owns the sub channel objects, which is different from `ParallelChannel`'s configurable ownership.
- If the second parameter of `AddChannel` is not NULL, it will be filled using `brpc::SelectiveChannel::ChannelHandle`, which can be used as a parameter to `RemoveAndDestroyChannel` to delete a channel dynamically.
- `SelectiveChannel` overrides the timeout value of sub channel's using its own one. For example, having timeout set to 100ms for a sub channel and 500ms for `SelectiveChannel`, the actual request  timeout is 500ms rather than 100ms.

The way of using `SelectiveChannel` is exactly the same as that of regular channels.

## Divide requests into multiple DNS

Sometimes we need to divide requests into multiple DNS node.  The reasons may be:

- Machines of the same service are mounted under different DNS.
- Machines are split into multiple groups. Requests will be sent to one of the groups first and then travel inside that group. There is a difference in the way of traffic division between groups or inside a single group.

The above can be achieved through `SelectiveChannel`.

The following code creates a `SelectiveChannel` and inserts three regular channels which access different DNS nodes.

```c++
brpc::SelectiveChannel channel;
brpc::ChannelOptions schan_options;
schan_options.timeout_ms = FLAGS_timeout_ms;
schan_options.backup_request_ms = FLAGS_backup_ms;
schan_options.max_retry = FLAGS_max_retry;
if (channel.Init("c_murmurhash", &schan_options) != 0) {
    LOG(ERROR) << "Fail to init SelectiveChannel";
    return -1;
}
 
for (int i = 0; i < 3; ++i) {
    brpc::Channel* sub_channel = new brpc::Channel;
    if (sub_channel->Init(dns_node_name[i], "rr", NULL) != 0) {
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

[PartitionChannel](https://github.com/brpc/brpc/blob/master/src/brpc/partition_channel.h) is a specialized `ParallelChannel`, in which it can add sub channels automatically based on the tag value inside a naming service. As a result, users can group machines together inside one naming service and use tags to partition them apart. The sample code is shown in [example/partition_echo_c++](https://github.com/brpc/brpc/tree/master/example/partition_echo_c++/).

`ParititonChannel` only supports one way to partition channels. When you need multiple scheme or replace the current one smoothly, you should try `DynamicPartitionChannel`. It will create the corresponding sub `PartitionChannel` based on different partition methods, and divide traffic into these partition channels. The sample code is shown in [example/dynamic_partition_echo_c++](https://github.com/brpc/brpc/tree/master/example/dynamic_partition_echo_c++/).

If partitions belong to different name services, you have to write your own channel, which should create and add a sub channel for each different naming service by means of `ParallelChannel`. Please refer to the previous section for `ParellelChannel`'s usage.

## Use PartitionChannel

First of all, implement your own `PartitionParser`. For this example, the tag format is `N/M`, where N represents the partition index and M for the total number of partitions. As a result, `0/3` means it's the first partition of the three.

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

Then initialize the `PartitionChannel`

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

## Use DynamicPartitionChannel

`DynamicPartitionChannel` and `PartitionChannel` are basically the same in usage. Implementing  `PartitionParser` first followed by initialization, where the `Init` does not need `num_partition_kinds` since `DynamicPartitionChannel` dynamically creates sub `PartitionChannel` for each partitions.

Now we demonstrate how to use `DynamicPartitionChannel` to migrate from 3-partition scheme to 4-partition scheme.

First of all we start three `Server` objects on port 8004, 8005, 8006 respectively.

```
$ ./echo_server -server_num 3
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8004
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8005
TRACE: 09-06 10:40:39:   * 0 server.cpp:159] EchoServer is serving on port=8006
TRACE: 09-06 10:40:40:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
TRACE: 09-06 10:40:41:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
TRACE: 09-06 10:40:42:   * 0 server.cpp:192] S[0]=0 S[1]=0 S[2]=0 [total=0]
```

Note that each server will print a flow summary every second, which is all 0 now. Then we start a client using `DynamicPartitionChannel`, whose initialization code is shown below:

```c++
    ...
    brpc::DynamicPartitionChannel channel;
    brpc::PartitionChannelOptions options;
    // Allow server_list to be empty when calling DynamicPartitionChannel::Init
    options.succeed_without_server = true;   
    // Failure on any single partition terminates the RPC immediately.
    // You can use a more relaxed value
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

Now all 3 partitions correspond to the same `Server` on port 8004, so the client begins to send requests to 8004 once started.

```
$ ./echo_client            
TRACE: 09-06 10:51:10:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 3 unique addresses from `server_list'
TRACE: 09-06 10:51:10:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8004 via fd=3 SocketId=0 self_port=46544
TRACE: 09-06 10:51:11:   * 0 client.cpp:226] Sending EchoRequest at qps=132472 latency=371
TRACE: 09-06 10:51:12:   * 0 client.cpp:226] Sending EchoRequest at qps=132658 latency=370
TRACE: 09-06 10:51:13:   * 0 client.cpp:226] Sending EchoRequest at qps=133208 latency=369
```

At the same time, the server received triple flow due to the access of three partition for each request.

```
TRACE: 09-06 10:51:11:   * 0 server.cpp:192] S[0]=398866 S[1]=0 S[2]=0 [total=398866]
TRACE: 09-06 10:51:12:   * 0 server.cpp:192] S[0]=398117 S[1]=0 S[2]=0 [total=398117]
TRACE: 09-06 10:51:13:   * 0 server.cpp:192] S[0]=398873 S[1]=0 S[2]=0 [total=398873]
```

Now we change the partition: adding the new 4-partition scheme on port 8005 in `server_list`:

```
0.0.0.0:8004  0/3
0.0.0.0:8004  1/3   
0.0.0.0:8004  2/3 
 
0.0.0.0:8005  0/4        
0.0.0.0:8005  1/4        
0.0.0.0:8005  2/4        
0.0.0.0:8005  3/4
```

Notice the changes in the summary. The client found the modification of `server_list` and reloaded it, while it's QPS doesn't change.

```
TRACE: 09-06 10:57:10:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 7 unique addresses from `server_list'
TRACE: 09-06 10:57:10:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8005 via fd=7 SocketId=768 self_port=39171
TRACE: 09-06 10:57:11:   * 0 client.cpp:226] Sending EchoRequest at qps=135346 latency=363
TRACE: 09-06 10:57:12:   * 0 client.cpp:226] Sending EchoRequest at qps=134201 latency=366
TRACE: 09-06 10:57:13:   * 0 client.cpp:226] Sending EchoRequest at qps=137627 latency=356
TRACE: 09-06 10:57:14:   * 0 client.cpp:226] Sending EchoRequest at qps=136775 latency=359
TRACE: 09-06 10:57:15:   * 0 client.cpp:226] Sending EchoRequest at qps=139043 latency=353
```

Change on the server's side is much bigger. Traffic appeared on port 8005 and its proportion against 8004 is roughly 4 : 3.

```
TRACE: 09-06 10:57:09:   * 0 server.cpp:192] S[0]=398597 S[1]=0 S[2]=0 [total=398597]
TRACE: 09-06 10:57:10:   * 0 server.cpp:192] S[0]=392839 S[1]=0 S[2]=0 [total=392839]
TRACE: 09-06 10:57:11:   * 0 server.cpp:192] S[0]=334704 S[1]=83219 S[2]=0 [total=417923]
TRACE: 09-06 10:57:12:   * 0 server.cpp:192] S[0]=206215 S[1]=273873 S[2]=0 [total=480088]
TRACE: 09-06 10:57:13:   * 0 server.cpp:192] S[0]=204520 S[1]=270483 S[2]=0 [total=475003]
TRACE: 09-06 10:57:14:   * 0 server.cpp:192] S[0]=207055 S[1]=273725 S[2]=0 [total=480780]
TRACE: 09-06 10:57:15:   * 0 server.cpp:192] S[0]=208453 S[1]=276803 S[2]=0 [total=485256]
```

The reason is that each request needs 3 access to 8004 or 4 access to 8005. Note that the flow ratio between 8004 and 8005 is 3 : 4, so the client issues requests to both partition schemes with the same probability. This flow ratio depends on capacity, which can be calculated recursively:

- The capacity of a regular `Channel` using `NamingService` equals to the number of servers in the naming service, as the capacity of a single-server `Channel` is 1.
- The capacity of `ParallelChannel` or `PartitionChannel` equals to the minimum value of its sub channel's.
- The capacity of `SelectiveChannel` equals to the sum of all its sub channel's.
- The capacity of `DynamicPartitionChannel` equals to the sum of all its sub `PartitionChannel`'s.

In this case, the capacity of the 3-partition channel and the 4-partition one both equal to 1 (only 1 regular channel in each partition such as 1/3). As all 3-partitions are on 8004 and all 4-partitions are on 8005, the traffic proportion between the two servers is the capacity ratio of the two partition channels.

We can add more partitions on 8006 to 4-partition scheme by changing `server_list`:

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

The client still remains unchanged. 

```
TRACE: 09-06 11:11:51:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 11 unique addresses from `server_list'
TRACE: 09-06 11:11:51:   * 0 src/brpc/socket.cpp:779] Connected to 0.0.0.0:8006 via fd=8 SocketId=1280 self_port=40759
TRACE: 09-06 11:11:51:   * 0 client.cpp:226] Sending EchoRequest at qps=131799 latency=372
TRACE: 09-06 11:11:52:   * 0 client.cpp:226] Sending EchoRequest at qps=136217 latency=361
TRACE: 09-06 11:11:53:   * 0 client.cpp:226] Sending EchoRequest at qps=133531 latency=368
TRACE: 09-06 11:11:54:   * 0 client.cpp:226] Sending EchoRequest at qps=136072 latency=361
```

Notice the traffic on 8006 at the server side. The flow ratio of the three servers is about 3 : 4 : 4, as the capacity of the 3-partition scheme is still 1 while capacity of 4-partition scheme increases to 2 (due to the addition of a regular channel on 8006). As a result, the overall proportion between the two schemes is 3 : 8. Each partition inside the 4-partition scheme has 2 instances on 8005 and 8006, between which the round-robin load balancing is applied to split the traffic equally. Finally, the proportion among the 3 servers is 3 : 4 : 4.

```
TRACE: 09-06 11:11:51:   * 0 server.cpp:192] S[0]=199625 S[1]=263226 S[2]=0 [total=462851]
TRACE: 09-06 11:11:52:   * 0 server.cpp:192] S[0]=143248 S[1]=190717 S[2]=159756 [total=493721]
TRACE: 09-06 11:11:53:   * 0 server.cpp:192] S[0]=133003 S[1]=178328 S[2]=178325 [total=489656]
TRACE: 09-06 11:11:54:   * 0 server.cpp:192] S[0]=135534 S[1]=180386 S[2]=180333 [total=496253]
```

Let's see what happens if we remove one partition of the 3-partition scheme:

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

The client noticed the changes in the `server_list`:

```
TRACE: 09-06 11:17:47:   * 0 src/brpc/policy/file_naming_service.cpp:83] Got 10 unique addresses from `server_list'
TRACE: 09-06 11:17:47:   * 0 client.cpp:226] Sending EchoRequest at qps=131653 latency=373
TRACE: 09-06 11:17:48:   * 0 client.cpp:226] Sending EchoRequest at qps=120560 latency=407
TRACE: 09-06 11:17:49:   * 0 client.cpp:226] Sending EchoRequest at qps=124100 latency=395
TRACE: 09-06 11:17:50:   * 0 client.cpp:226] Sending EchoRequest at qps=123743 latency=397
```

Notice the traffic drop on 8004 at the server side. The reason is that the 3-partition scheme is not complete anymore once the last 2/3 partition has been removed. The capacity of this scheme dropped down to zero so that there was no requests on 8004 anymore.

```
TRACE: 09-06 11:17:47:   * 0 server.cpp:192] S[0]=130864 S[1]=174499 S[2]=174548 [total=479911]
TRACE: 09-06 11:17:48:   * 0 server.cpp:192] S[0]=20063 S[1]=230027 S[2]=230098 [total=480188]
TRACE: 09-06 11:17:49:   * 0 server.cpp:192] S[0]=0 S[1]=245961 S[2]=245888 [total=491849]
TRACE: 09-06 11:17:50:   * 0 server.cpp:192] S[0]=0 S[1]=250198 S[2]=250150 [total=500348]
```

Under the production environment, we will gradually increase the number of instance on 4-partition scheme while terminating instance on 3-partition scheme. `DynamicParititonChannel` can divide the traffic based on the capacity of all partitions dynamically. When the capacity of 3-partition scheme drops down to 0, then we've smoothly migrated all the servers from 3-partition scheme to 4-partition one without changing the client's code.
