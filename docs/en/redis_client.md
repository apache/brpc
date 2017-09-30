[redis](http://redis.io/) has been one of the most popular cache service in recent years. Compared to memcached it provides users with more data structures and interfaces, which frees the user from lots of development and thus covers a wide range of applications in baidu. In order to speed up the access to redis and make full use of bthread concurrency, brpc directly support the redis protocol. For examples please refer to: [example/redis_c++](https://github.com/brpc/brpc/tree/master/example/redis_c++/)

Compared to [hiredis](https://github.com/redis/hiredis) (the official redis client), we have advantages in:

- Thread safety. No need to set up a separate client for each thread.
- Support access patterns of synchronous, asynchronous, batch synchronous, batch asynchronous. Can be used with ParallelChannel to enable access combinations.
- Support various [connection types](client.md#Connection Type). Support timeout, backup request, cancellation, tracing, built-in services, and other basic benefits of the RPC framework.
- Only a single connection between one process and one redis-server, which is more efficient when multiple threads access one redis-server at the same time (see [performance](#Performance)). The internal memory will be allocated block by block in succession regardless of the complexity of the reply, and short string optimization (SSO) is also implemented.

Like http, brpc guarantees the time complexity of parsing redis reply is O(N) instead of O($N^2$) in the worst case, where N is the number of bytes of reply. This is important when reply consists of a large array.

For debugging, please turn on [-redis_verbose](#Debug) to print all the redis request and response to stderr.

# Request to single redis

Create a `Channel` to access redis:

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

Execute `SET` followed by `INCR`:

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
if (response.reply(0).is_error()) {
    LOG(ERROR) << "Fail to set";
    return -1;
}
// You can fetch and print response through the reply object
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
// The increased value
LOG(INFO) << response.reply(0).integer()  // 2
          << response.reply(0)            // (integer) 2
          << response;                    // (integer) 2
```

Execute `incr` and `decr` in batch

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

# RedisRequest

A [RedisRequest](https://github.com/brpc/brpc/blob/master/src/brpc/redis.h) object can hold multiple Command by calling `AddCommand*`, which returns true on success and false on error along with the calling stack.

```c++
bool AddCommand(const char* fmt, ...);
bool AddCommandV(const char* fmt, va_list args);
bool AddCommandByComponents(const butil::StringPiece* components, size_t n);
```

The format parameter is compatible with hiredis: `%b` represents for binary data (pointer + length), and others are similar to those of `printf`. Some improvements have been made such as characters enclosed by single or double quotes will be recognized as a complete field regardless of the blanks inside the quote. For example, `AddCommand("Set 'a key with space' 'a value with space as well'")` sets value `a value with space as well` to key `a key with space`, while in hiredis it must be written as `redisvCommand(..., "SET% s% s", "a key with space", "a value with space as well");`

`AddCommandByComponents` is similar to `redisCommandArgv` in hiredis. Users specify each part of the command through an array. It's not the fastest way, but the most efficient, and it's immune to the escape problem, which usually occurs in `AddCommand` and `AddCommandV` . If you encounters an error of "quotation marks do not match" or "invalid format" when using `AddCommand` and `AddCommandV`, you should this method.

If `AddCommand` fails, **subsequent `AddCommand` and `CallMethod` will also fail**. In general, there is no need to check whether `AddCommand*` failed or not, since it will be reflected through the RPC failure.

Use `command_size()` to fetch the number of commands that have been added successfully.

Call `Clear()` to reuse the `RedisRequest` object.

# RedisResponse

[RedisResponse](https://github.com/brpc/brpc/blob/master/src/brpc/redis.h) can contain one or multiple [RedisReply](https://github.com/brpc/brpc/blob/master/src/brpc/redis_reply.h) objects. Use `reply_size()` for the total number of the replies and `reply(i)` for reference to the i-th reply (based from 0). Note that in hiredis, you have to call `redisGetReply` for N times to fetch response to N commands, while it's unnecessary in brpc as `RedisResponse` has already included the N replies. As long as RPC is successful, `response.reply_size()` should be equal to `request.command_size()`, unless redis-server has a bug (It's a basic premise of the redis-server that response and request have one by one correspondence)

Each `RedisReply` object could be:

- REDIS_REPLY_NIL: NULL in redis, which means value does not exist. Can be determined by `is_nil()`.
- REDIS_REPLY_STATUS: Referred to `Simple String` in the redis document. It's usually used as the return value, such as the `OK` string returned by `SET`. Can be determined by `is_string()` (It's the same function for REDIS_REPLY_STRING, so users can't distinguish status from string for now). Use `c_str()` or `data()` to get the value.
- REDIS_REPLY_STRING: Referred to `Bulk String` in the redis document. Most return values are of this type, including those can be `incr`ed. You can use `is_string()` to validate and `c_str()` or `data()` for the value.
- REDIS_REPLY_ERROR: The error message when operation failed. Can be determined by `is_error()` and fetched by `error_message()`.
- REDIS_REPLY_INTEGER: A 64-bit signed integer. Can be determined by `is_integer()` and fetched by `integer()`.
- REDIS_REPLY_ARRAY: Array of replies. Can be determined by `is_array()`. Use `size()` for the total size of the array and `[i]` for the reference to the corresponding sub-reply.

For example, a response contains three replies: an integer, a string and an array (size = 2). Then we can use `response.reply(0).integer()`, `response.reply(1).c_str()`, and `repsonse.reply(2)[0], repsonse.reply(2)[1]` to fetch their values respectively. If the type is not correct, the call stack will be printed and an undefined is returned.

The ownership of all the reply objects belongs to `RedisResponse`. All relies will be destroyed when response has been freed. Also note that copy is forbidden for `RedisReply`.

Call `Clear()` to reuse the `RedisRespones` object.

# Request to redis cluster

For now please use [twemproxy](https://github.com/twitter/twemproxy) as a common way to wrap redis cluster so that it can be used just like a single node proxy, in which case you can just replace your hiredis with brpc. Accessing the cluster directly from client (using consistent hash) may reduce the delay, but at the cost of other management services. Make sure to double check that in redis document.

If you maintain a redis cluster like the memcache all by yourself, it should be accessible using consistent hash. In general, you have to make sure each `RedisRequest` contains only one command or keys from multiple commands fall on the same server, since under the current implementation, if a request contains multiple commands, it will always be sent to the same server. For example, if a request contains a number of Get while the corresponding keys distribute in different servers, the result must be wrong, in which case you have to separate the request according to key distribution.

# Debug

 Turn on [-redis_verbose](http://brpc.baidu.com:8765/flags/redis_verbose) to print all redis request and response to stderr. Note that this should only be used for debug instead of online production.

Turn on [-redis_verbose_crlf2space](http://brpc.baidu.com:8765/flags/redis_verbose_crlf2space) to replace the `CRLF` (\r\n) with spaces for better readability.

| Name                     | Value | Description                              | Defined At                         |
| ------------------------ | ----- | ---------------------------------------- | ---------------------------------- |
| redis_verbose            | false | [DEBUG] Print EVERY redis request/response to stderr | src/brpc/policy/redis_protocol.cpp |
| redis_verbose_crlf2space | false | [DEBUG] Show \r\n as a space             | src/brpc/redis.cpp                 |

# Performance

redis version: 2.6.14 (latest version is 3.0+)

Start a client to send requests to redis-server from the same machine using 1, 50, 200 bthreads synchronously. The time unit for latency is microseconds.

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

The QPS reaches the limit after 200 threads, which is much higher than hiredis since brpc uses a single connection to redis-server by default and requests from multiple threads will be [merged in a wait-free way](io.md#发消息). As a result, from the redis-server's view, it received a bunch of requests and read/handle them in batch, thus getting much higher QPS than non-batched ones. The QPS drop in the following test using connection pool to visit redis-server is another proof.

Start a client to send requests (10 commands per request) to redis-server from the same machine using 1, 50, 200 bthreads synchronously. The time unit for latency is microseconds.

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

Note that the actual commands processed per second of redis-server is 10 times the QPS value, which is about 400K.  When thread_num equals 50 or higher, the CPU usage of the redis-server reaches its limit. Since redis-server runs in [single-threaded reactor mode](threading_overview.md#单线程reactor), 99.9% on one core is the maximum CPU it can use.

Now start a client to send requests to redis-server from the same machine using 50 bthreads synchronously through connection pool.

```
$ ./client -use_bthread -connection_type pooled
TRACE: 02-13 18:07:40:   * 0 client.cpp:180] Accessing redis server at qps=75986 latency=654
TRACE: 02-13 18:07:41:   * 0 client.cpp:180] Accessing redis server at qps=75562 latency=655
TRACE: 02-13 18:07:42:   * 0 client.cpp:180] Accessing redis server at qps=75238 latency=657
 
  PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND
16878 gejun     20   0 48136 2520 1004 R 99.9  0.0   9:52.33 redis-server
```

We can see a tremendous drop of QPS compared to single connection and the redis-server has reached full CPU usage. The reason is that each time only one request from a connection can be read by the redis-server, which greatly increases the cost of IO operation.

# Command Line Interface

example/redis_c++/redis_cli is a command line tool just like the official CLI, which shows the ability of brpc to handle the redis protocol. When you encounter an unexpected result from redis-server using brpc, you should try this CLI to debug interactively.

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

Like the official CLI, `redis_cli <command>` can be used to issue commands directly, and use `-server` to specify the address of redis-server.
