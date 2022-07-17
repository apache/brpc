[English version](../en/error_code.md)

brpc使用[brpc::Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h)设置和获取一次RPC的参数，`Controller::ErrorCode()`和`Controller::ErrorText()`则分别是该次RPC的错误码和错误描述，RPC结束后才能访问，否则结果未定义。ErrorText()由Controller的基类google::protobuf::RpcController定义，ErrorCode()则是brpc::Controller定义的。Controller还有个Failed()方法告知该次RPC是否失败，这三者的关系是：

- 当Failed()为true时，ErrorCode()一定为非0，ErrorText()则为非空。
- 当Failed()为false时，ErrorCode()一定为0，ErrorText()未定义（目前在brpc中会为空，但你最好不要依赖这个事实）

# 标记RPC为错误

brpc的client端和server端都有Controller，都可以通过SetFailed()修改其中的ErrorCode和ErrorText。当多次调用一个Controller的SetFailed时，ErrorCode会被覆盖，ErrorText则是**添加**而不是覆盖。在client端，框架会额外加上第几次重试，在server端，框架会额外加上server的地址信息。

client端Controller的SetFailed()常由框架调用，比如发送request失败，接收到的response不符合要求等等。只有在进行较复杂的访问操作时用户才可能需要设置client端的错误，比如在访问后端前做额外的请求检查，发现有错误时把RPC设置为失败。

server端Controller的SetFailed()常由用户在服务回调中调用。当处理过程发生错误时，一般调用SetFailed()并释放资源后就return了。框架会把错误码和错误信息按交互协议填入response，client端的框架收到后会填入它那边的Controller中，从而让用户在RPC结束后取到。需要注意的是，**server端在SetFailed()时默认不打印送往client的错误**。打日志是比较慢的，在繁忙的线上磁盘上，很容易出现巨大的lag。一个错误频发的client容易减慢整个server的速度而影响到其他的client，理论上来说这甚至能成为一种攻击手段。对于希望在server端看到错误信息的场景，可以打开gflag **-log_error_text**(可动态开关)，server会在每次失败的RPC后把对应Controller的ErrorText()打印出来。

# brpc的错误码

brpc使用的所有ErrorCode都定义在[errno.proto](https://github.com/brpc/brpc/blob/master/src/brpc/errno.proto)中，*SYS_*开头的来自linux系统，与/usr/include/errno.h中定义的精确一致，定义在proto中是为了跨语言。其余的是brpc自有的。

[berror(error_code)](https://github.com/brpc/brpc/blob/master/src/butil/errno.h)可获得error_code的描述，berror()可获得当前[system errno](http://www.cplusplus.com/reference/cerrno/errno/)的描述。**ErrorText() != berror(ErrorCode())**，ErrorText()会包含更具体的错误信息。brpc默认包含berror，你可以直接使用。

brpc中常见错误的打印内容列表如下：

 

| 错误码            | 数值   | 重试   | 说明                                       | 日志                                       |
| -------------- | ---- | ---- | ---------------------------------------- | ---------------------------------------- |
| EAGAIN         | 11   | 是    | 同时发送的请求过多。软限，很少出现。                       | Resource temporarily unavailable         |
| ENODATA        | 61   | 是    | 1. Naming Service返回的server列表为空 2. Naming Service某次变更时，所有实例都发生了修改，Naming Service更新LB的逻辑是先Remove再Add，会存在很短时间内LB实例列表为空的情况 | Fail to select server from xxx |
| ETIMEDOUT      | 110  | 是    | 连接超时。                                    | Connection timed out                     |
| EHOSTDOWN      | 112  | 是    | 可能原因：一、Naming Server返回的列表不为空，但LB选不出可用的server，LB返回了EHOSTDOWN错误。具体可能原因：a.Server正在退出中(返回了ELOGOFF) b. Server因为之前的某种失败而被封禁，封禁的具体逻辑：1. 对于单连接，唯一的连接socket被SetFail即封禁，SetFail在代码里出现非常多，有很多种可能性触发 2. 对于连接池/短连接，只有错误号满足does_error_affect_main_socket时（ECONNREFUSED，ENETUNREACH，EHOSTUNREACH或EINVAL）才会封禁 3. 封禁之后，有CheckHealth线程健康检查，就是尝试去连接一下，检查间隔由SocketOptions的health_check_interval_s控制，检查正常会解封。二、使用SingleServer方式初始化Channel（没有LB），唯一的一个连接为LOGOFF或者封禁状态（同上） | "Fail to select server from …"  "Not connected to … yet" |
| ENOSERVICE     | 1001 | 否    | 找不到服务，不太出现，一般会返回ENOMETHOD。               |                                          |
| ENOMETHOD      | 1002 | 否    | 找不到方法。                                   | 形式广泛，常见如"Fail to find method=..."        |
| EREQUEST       | 1003 | 否    | request序列化错误，client端和server端都可能设置        | 形式广泛："Missing required fields in request: …" "Fail to parse request message, …"  "Bad request" |
| EAUTH          | 1004 | 否    | 认证失败                                     | "Authentication failed"                  |
| ETOOMANYFAILS  | 1005 | 否    | ParallelChannel中太多子channel失败             | "%d/%d channels failed, fail_limit=%d"   |
| EBACKUPREQUEST | 1007 | 是    | 触发backup request时设置，不会出现在ErrorCode中，但可在/rpcz里看到 | “reached backup timeout=%dms"            |
| ERPCTIMEDOUT   | 1008 | 否    | RPC超时                                    | "reached timeout=%dms"                   |
| EFAILEDSOCKET  | 1009 | 是    | RPC进行过程中TCP连接出现问题                        | "The socket was SetFailed"               |
| EHTTP          | 1010 | 否    | 非2xx状态码的HTTP访问结果均认为失败并被设置为这个错误码。默认不重试，可通过RetryPolicy定制 | Bad http call                            |
| EOVERCROWDED   | 1011 | 是    | 连接上有过多的未发送数据，常由同时发起了过多的异步访问导致。可通过参数-socket_max_unwritten_bytes控制，默认64MB。 | The server is overcrowded                |
| EINTERNAL      | 2001 | 否    | Server端Controller.SetFailed没有指定错误码时使用的默认错误码。 | "Internal Server Error"                  |
| ERESPONSE      | 2002 | 否    | response解析错误，client端和server端都可能设置        | 形式广泛"Missing required fields in response: ...""Fail to parse response message, ""Bad response" |
| ELOGOFF        | 2003 | 是    | Server已经被Stop了                           | "Server is going to quit"                |
| ELIMIT         | 2004 | 是    | 同时处理的请求数超过ServerOptions.max_concurrency了 | "Reached server's limit=%d on concurrent requests" |

# 自定义错误码

在C++/C中你可以通过宏、常量、enum等方式定义ErrorCode:
```c++
#define ESTOP -114                // C/C++
static const int EMYERROR = 30;   // C/C++
const int EMYERROR2 = -31;        // C++ only
```
如果你需要用berror返回这些新错误码的描述，你可以在.cpp或.c文件的全局域中调用BAIDU_REGISTER_ERRNO(error_code, description)进行注册，比如：
```c++
BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")
BAIDU_REGISTER_ERRNO(EMYERROR, "my error")
```
strerror和strerror_r不认识使用BAIDU_REGISTER_ERRNO定义的错误码，自然地，printf类的函数中的%m也不能转化为对应的描述，你必须使用%s并配以berror()。
```c++
errno = ESTOP;
printf("Describe errno: %m\n");                              // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", strerror_r(errno, NULL, 0));  // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", berror());                    // [Correct] Describe errno: the thread is stopping
printf("Describe errno: %s\n", berror(errno));               // [Correct] Describe errno: the thread is stopping
```
当同一个error code被重复注册时，那么会出现链接错误：

```
redefinition of `class BaiduErrnoHelper<30>'
```
或者在程序启动时会abort：
```
Fail to define EMYERROR(30) which is already defined as `Read-only file system', abort
```

你得确保不同的模块对ErrorCode的理解是相同的，否则当两个模块把一个错误码理解为不同的错误时，它们之间的交互将出现无法预计的行为。为了防止这种情况出现，你最好这么做：
- 优先使用系统错误码，它们的值和含义一般是固定不变的。
- 多个交互的模块使用同一份错误码定义，防止后续修改时产生不一致。
- 使用BAIDU_REGISTER_ERRNO描述新错误码，以确保同一个进程内错误码是互斥的。 
