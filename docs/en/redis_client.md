[中文版](../cn/redis_client.md)

[redis](http://redis.io/) is one of the most popular caching service in recent years. Compared to memcached it provides users with more data structures and operations, speeding up developments. In order to access redis servers more conveniently and make full use of bthread's capability of concurrency, brpc directly supports the redis protocol. Check [example/redis_c++](https://github.com/brpc/brpc/tree/master/example/redis_c++/) for an example.

Advantages compared to [hiredis](https://github.com/redis/hiredis) (the official redis client):

- Thread safety. No need to set up separate clients for each thread.
- Support synchronous, asynchronous, semi-synchronous accesses etc. Support [ParallelChannel etc](combo_channel.md) to define access patterns declaratively.
- Support various [connection types](client.md#connection-type). Support timeout, backup request, cancellation, tracing, built-in services, and other benefits offered by brpc.
- All brpc clients in a process share a single connection to one redis-server, which is more efficient when multiple threads access one redis-server simultaneously (see [performance](#Performance)). Memory is allocated in blocks regardless of complexity of the reply, and short string optimization (SSO) is implemented to further improve performance.

Similarly with http, brpc guarantees that the time complexity of parsing redis replies is O(N) in worst cases rather than O(N^2) , where N is the number of bytes of reply. This is important when the reply consists of large arrays.

Turn on [-redis_verbose](#Debug) to print contents of all redis requests and responses, which is for debugging only.

# Request a redis server

Create a `Channel` for accessing redis:

```c++
#include <brpc/redis.h>
#include <brpc/channel.h>
  
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_REDIS;
brpc::Channel redis_channel;
if (redis_channel.Init("0.0.0.0:6379", &options) != 0) {  // 6379 is the default port for redis-server
   LOG(ERROR) << "Fail to init channel to redis-server";
   return -1;
}
...
```

Execute `SET`, then `INCR`:

```c++
std::string my_key = "my_key_1";
int my_number = 1;
...
// Execute "SET <my_key> <my_number>"
brpc::RedisRequest set_request;
brpc::RedisResponse response;
brpc::Controller cntl;
set_request.AddCommand("SET %s %d", my_key.c_str(), my_number);
redis_channel.CallMethod(NULL, &cntl, &set_request, &response, NULL/*done*/);
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access redis-server";
    return -1;
}
// Get a reply by calling response.reply(i)
if (response.reply(0).is_error()) {
    LOG(ERROR) << "Fail to set";
    return -1;
}
// A reply is printable in multiple ways
LOG(INFO) << response.reply(0).c_str()  // OK
          << response.reply(0)          // OK
          << response;                  // OK
...
 
// Execute "INCR <my_key>"
brpc::RedisRequest incr_request;
incr_request.AddCommand("INCR %s", my_key.c_str());
response.Clear();
cntl.Reset();
redis_channel.CallMethod(NULL, &cntl, &incr_request, &response, NULL/*done*/);
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access redis-server";
    return -1;
}
if (response.reply(0).is_error()) {
    LOG(ERROR) << "Fail to incr";
    return -1;
}
// A reply is printable in multiple ways
LOG(INFO) << response.reply(0).integer()  // 2
          << response.reply(0)            // (integer) 2
          << response;                    // (integer) 2
```

Execute `incr` and `decr` in batch:

```c++
brpc::RedisRequest request;
brpc::RedisResponse response;
brpc::Controller cntl;
request.AddCommand("INCR counter1");
request.AddCommand("DECR counter1");
request.AddCommand("INCRBY counter1 10");
request.AddCommand("DECRBY counter1 20");
redis_channel.CallMethod(NULL, &cntl, &request, &response, NULL/*done*/);
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access redis-server";
    return -1;
}
CHECK_EQ(4, response.reply_size());
for (int i = 0; i < 4; ++i) {
    CHECK(response.reply(i).is_integer());
    CHECK_EQ(brpc::REDIS_REPLY_INTEGER, response.reply(i).type());
}
CHECK_EQ(1, response.reply(0).integer());
CHECK_EQ(0, response.reply(1).integer());
CHECK_EQ(10, response.reply(2).integer());
CHECK_EQ(-10, response.reply(3).integer());
```

# Request redis with authenticator

Create a RedisAuthenticator, and set to ChannelOptions.

```c++
brpc::ChannelOptions options;
brpc::policy::RedisAuthenticator* auth = new brpc::policy::RedisAuthenticator("my_password");
options.auth = auth;
```


# RedisRequest

A [RedisRequest](https://github.com/brpc/brpc/blob/master/src/brpc/redis.h) may contain multiple commands by calling `AddCommand*`, which returns true on success and false otherwise. **The callsite backtrace is also printed on error**.

```c++
bool AddCommand(const char* fmt, ...);
bool AddCommandV(const char* fmt, va_list args);
bool AddCommandByComponents(const butil::StringPiece* components, size_t n);
```

Formatting is compatible with hiredis, namely `%b` corresponds to binary data (pointer + length), others are similar to those in `printf`. Some improvements have been made such as characters enclosed by single or double quotes are recognized as one field regardless of the spaces inside. For example, `AddCommand("Set 'a key with space' 'a value with space as well'")` sets value `a value with space as well` to key `a key with space`, while in hiredis the command must be written as `redisvCommand(..., "SET% s% s", "a key with space", "a value with space as well");`

`AddCommandByComponents` is similar to `redisCommandArgv` in hiredis. Users specify each part of the command in an array, which is immune to escaping issues often occurring in `AddCommand` and `AddCommandV`. If you encounter errors such as "Unmatched quote" or "invalid format" when using `AddCommand` and `AddCommandV`, try this method.

If `AddCommand*` fails, subsequent `AddCommand*` and `CallMethod` also fail. In general, there is no need to check return value of `AddCommand*`, since the RPC fails directly anyway.

Use `command_size()` to get number of commands added successfully.

Call `Clear()` before re-using the `RedisRequest` object.

# RedisResponse

A [RedisResponse](https://github.com/brpc/brpc/blob/master/src/brpc/redis.h) may contain one or multiple [RedisReply](https://github.com/brpc/brpc/blob/master/src/brpc/redis_reply.h)s. Use `reply_size()` for total number of replies and `reply(i)` for reference to the i-th reply (counting from 0). Note that in hiredis, if the request contains N commands, you have to call `redisGetReply` N times to get replies, while it's unnecessary in brpc as the `RedisResponse` already includes the N replies which are accessible by reply(i). As long as RPC is successful, `response.reply_size()` should be equal to `request.command_size()`, unless the redis-server is buggy. The precondition that redis works correctly is that replies correspond to commands one by one in the same sequence (positional correspondence).

Each `RedisReply` may be:

- REDIS_REPLY_NIL: NULL in redis, which means value does not exist. Testable by `is_nil()`.
- REDIS_REPLY_STATUS: Referred to `Simple String` in the redis document, usually used as the status of operations, such as the `OK` returned by `SET`. Testable by `is_string()` (same function for REDIS_REPLY_STRING). Use `c_str()` or `data()` to get the value.
- REDIS_REPLY_STRING: Referred to `Bulk String` in the redis document. Most return values are of this type, including those returned by `incr`. Testable by `is_string()`. Use `c_str()` or `data()` for the value.
- REDIS_REPLY_ERROR: The error message for a failed operation. Testable by `is_error()`. Use `error_message()` to get the message.
- REDIS_REPLY_INTEGER: A 64-bit signed integer. Testable by `is_integer()`. Use `integer()` to get the value.
- REDIS_REPLY_ARRAY: Array of replies. Testable by `is_array()`. Use `size()` for size of the array and `[i]` for the reference to the corresponding sub-reply.

If a response contains three replies: an integer, a string and an array with 2 items, we can use `response.reply(0).integer()`, `response.reply(1).c_str()`, and `repsonse.reply(2)[0]`, `repsonse.reply(2)[1]` to fetch values respectively. If the type is not correct, backtrace of the callsite is printed and an undefined value is returned.

Ownership of all replies belongs to `RedisResponse`. All relies are destroyed when response is destroyed.

Call `Clear()` before re-using the `RedisRespones` object.

# Request a redis cluster

Create a `Channel` using the consistent hashing as the load balancing algorithm(c_md5 or c_murmurhash) to access a redis cluster mounted under a naming service. Note that each `RedisRequest` should contain only one command or all commands have the same key. Under current implementation, multiple commands inside a single request are always sent to a same server. If the keys are located on different servers, the result must be wrong. In which case, you have to divide the request into multilple ones with one command each.

Another choice is to use the common [twemproxy](https://github.com/twitter/twemproxy) solution, which makes clients access the cluster just like accessing a single server, although the solution needs to deploy proxies and adds more latency.

# Debug

Turn on [-redis_verbose](http://brpc.baidu.com:8765/flags/redis_verbose) to print contents of all redis requests and responses. Note that this should only be used for debugging rather than online services.

Turn on [-redis_verbose_crlf2space](http://brpc.baidu.com:8765/flags/redis_verbose_crlf2space) to replace the `CRLF` (\r\n) with spaces in debugging logs for better readability.

| Name                     | Value | Description                              | Defined At                         |
| ------------------------ | ----- | ---------------------------------------- | ---------------------------------- |
| redis_verbose            | false | [DEBUG] Print EVERY redis request/response | src/brpc/policy/redis_protocol.cpp |
| redis_verbose_crlf2space | false | [DEBUG] Show \r\n as a space             | src/brpc/redis.cpp                 |

# Performance

redis version: 2.6.14

Start a client to send requests to a redis-server on the same machine using 1, 50, 200 bthreads synchronously. The latency is in microseconds.

```
$ ./client -use_bthread -thread_num 1
TRACE: 02-13 19:42:04:   * 0 client.cpp:180] Accessing redis server at qps=18668 latency=50
TRACE: 02-13 19:42:05:   * 0 client.cpp:180] Accessing redis server at qps=17043 latency=52
TRACE: 02-13 19:42:06:   * 0 client.cpp:180] Accessing redis server at qps=16520 latency=54

$ ./client -use_bthread -thread_num 50
TRACE: 02-13 19:42:54:   * 0 client.cpp:180] Accessing redis server at qps=301212 latency=164
TRACE: 02-13 19:42:55:   * 0 client.cpp:180] Accessing redis server at qps=301203 latency=164
TRACE: 02-13 19:42:56:   * 0 client.cpp:180] Accessing redis server at qps=302158 latency=164

$ ./client -use_bthread -thread_num 200
TRACE: 02-13 19:43:48:   * 0 client.cpp:180] Accessing redis server at qps=411669 latency=483
TRACE: 02-13 19:43:49:   * 0 client.cpp:180] Accessing redis server at qps=411679 latency=483
TRACE: 02-13 19:43:50:   * 0 client.cpp:180] Accessing redis server at qps=412583 latency=482
```

The peak QPS at 200 threads is much higher than hiredis since brpc uses a single connection to redis-server by default and requests from multiple threads are [merged in a wait-free way](io.md#sending-messages), making the redis-server receive requests in batch and reach a much higher QPS. The lower QPS in following test that uses pooled connections is another proof.

Start a client to send requests in batch (10 commands per request) to redis-server on the same machine using 1, 50, 200 bthreads synchronously. The latency is in microseconds.

```
$ ./client -use_bthread -thread_num 1 -batch 10  
TRACE: 02-13 19:46:45:   * 0 client.cpp:180] Accessing redis server at qps=15880 latency=59
TRACE: 02-13 19:46:46:   * 0 client.cpp:180] Accessing redis server at qps=16945 latency=57
TRACE: 02-13 19:46:47:   * 0 client.cpp:180] Accessing redis server at qps=16728 latency=57

$ ./client -use_bthread -thread_num 50 -batch 10
TRACE: 02-13 19:47:14:   * 0 client.cpp:180] Accessing redis server at qps=38082 latency=1307
TRACE: 02-13 19:47:15:   * 0 client.cpp:180] Accessing redis server at qps=38267 latency=1304
TRACE: 02-13 19:47:16:   * 0 client.cpp:180] Accessing redis server at qps=38070 latency=1305
 
  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND 
16878 gejun     20   0 48136 2436 1004 R 93.8  0.0  12:48.56 redis-server   // thread_num=50

$ ./client -use_bthread -thread_num 200 -batch 10
TRACE: 02-13 19:49:09:   * 0 client.cpp:180] Accessing redis server at qps=29053 latency=6875
TRACE: 02-13 19:49:10:   * 0 client.cpp:180] Accessing redis server at qps=29163 latency=6855
TRACE: 02-13 19:49:11:   * 0 client.cpp:180] Accessing redis server at qps=29271 latency=6838
 
  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND 
16878 gejun     20   0 48136 2508 1004 R 99.9  0.0  13:36.59 redis-server   // thread_num=200
```

Note that the commands processed per second by the redis-server is the QPS times 10, which is about 400K.  When thread_num equals 50 or higher, the CPU usage of the redis-server reaches limit. Note that redis-server is a [single-threaded reactor](threading_overview.md#single-threaded-reactor), utilizing one core is the maximum that it can do.

Now start a client to send requests to redis-server on the same machine using 50 bthreads synchronously through pooled connections.

```
$ ./client -use_bthread -connection_type pooled
TRACE: 02-13 18:07:40:   * 0 client.cpp:180] Accessing redis server at qps=75986 latency=654
TRACE: 02-13 18:07:41:   * 0 client.cpp:180] Accessing redis server at qps=75562 latency=655
TRACE: 02-13 18:07:42:   * 0 client.cpp:180] Accessing redis server at qps=75238 latency=657
 
  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND
16878 gejun     20   0 48136 2520 1004 R 99.9  0.0   9:52.33 redis-server
```

We can see a tremendous drop of QPS compared to the one using single connection above, and the redis-server has reached the CPU cap. The cause is that each time only one request can be read from a connection by the redis-server, which significantly increases cost of IO operations. This is also the peak performance of a hiredis client.

# Command Line Interface

[example/redis_c++/redis_cli](https://github.com/brpc/brpc/blob/master/example/redis_c%2B%2B/redis_cli.cpp) is a command line tool similar to the official CLI, demostrating brpc's capability to talk with redis servers. When unexpected results are got from a redis-server using a brpc client, you can debug with this tool interactively as well.

Like the official CLI, `redis_cli <command>` runs the command directly, and `-server` which is address of the redis-server can be specified.

```
$ ./redis_cli 
     __          _     __
    / /_  ____ _(_)___/ /_  __      _________  _____
   / __ \/ __ `/ / __  / / / /_____/ ___/ __ \/ ___/
  / /_/ / /_/ / / /_/ / /_/ /_____/ /  / /_/ / /__  
 /_.___/\__,_/_/\__,_/\__,_/     /_/  / .___/\___/  
                                     /_/            
This command-line tool mimics the look-n-feel of official redis-cli, as a 
demostration of brpc's capability of talking to redis server. The 
output and behavior is not exactly same with the official one.
 
redis 127.0.0.1:6379> mset key1 foo key2 bar key3 17
OK
redis 127.0.0.1:6379> mget key1 key2 key3
["foo", "bar", "17"]
redis 127.0.0.1:6379> incrby key3 10
(integer) 27
redis 127.0.0.1:6379> client setname brpc-cli
OK
redis 127.0.0.1:6379> client getname
"brpc-cli"
```
