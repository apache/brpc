brpc使用[brpc::Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h)设置一次RPC的参数和获取一次RPC的结果，ErrorCode()和ErrorText()是Controller的两个方法，分别是该次RPC的错误码和错误描述，只在RPC结束后才能访问，否则结果未定义。ErrorText()由Controller的基类google::protobuf::RpcController定义，ErrorCode()则是brpc::Controller定义的。Controller还有个Failed()方法告知该次RPC是否失败，这三者的关系是：

- 当Failed()为true时，ErrorCode()一定不为0，ErrorText()是非空的错误描述
- 当Failed()为false时，ErrorCode()一定为0，ErrorText()是未定义的（目前在brpc中会为空，但你最好不要依赖这个事实）

# 标记RPC为错误

brpc的client端和server端都有Controller，都可以通过SetFailed()修改其中的ErrorCode和ErrorText。当多次调用一个Controller的SetFailed时，ErrorCode会被覆盖，ErrorText则是**添加**而不是覆盖，在client端，框架会额外加上第几次重试，在server端，框架会额外加上server的地址信息。

client端Controller的SetFailed()常由框架调用，比如发送request失败，接收到的response不符合要求等等。只有在进行较复杂的访问操作时用户才可能需要设置client端的错误，比如在访问后端前做额外的请求检查，发现有错误时需要把RPC设置为失败。

server端Controller的SetFailed()常由用户在服务回调中调用。当处理过程发生错误时，一般调用SetFailed()并释放资源后就return了。框架会把错误码和错误信息按交互协议填入response，client端的框架收到后会填入它那边的Controller中，从而让用户在RPC结束后取到。需要注意的是，**server端在SetFailed()时一般不需要再打条日志。**打日志是比较慢的，在繁忙的线上磁盘上，很容易出现巨大的lag。一个错误频发的client容易减慢整个server的速度而影响到其他的client，理论上来说这甚至能成为一种攻击手段。对于希望在server端看到错误信息的场景，可以打开**-log_error_text**开关（已上线服务可访问/flags/log_error_text?setvalue=true动态打开），server会在每次失败的RPC后把对应Controller的ErrorText()打印出来。

# brpc的错误码

brpc使用的所有ErrorCode都定义在[errno.proto](https://github.com/brpc/brpc/blob/master/src/brpc/errno.proto)中，*SYS_*开头的来自linux系统，与/usr/include/errno.h中定义的精确一致，定义在proto中是为了跨语言。其余的是brpc自有的。

[berror(error_code)](https://github.com/brpc/brpc/blob/master/src/butil/errno.h)可获得error_code的描述，berror()可获得[system errno](http://www.cplusplus.com/reference/cerrno/errno/)的描述。**ErrorText() != berror(ErrorCode())**，ErrorText()会包含更具体的错误信息。brpc默认包含berror，你可以直接使用。

brpc中常见错误的打印内容列表如下：

 

| 错误码            | 数值   | 重试   | 说明                                       | 日志                                       |
| -------------- | ---- | ---- | ---------------------------------------- | ---------------------------------------- |
| EAGAIN         | 11   | 是    | 同时异步发送的请求过多。软限，很少出现。                     | Resource temporarily unavailable         |
| ETIMEDOUT      | 110  | 是    | 连接超时。                                    | Connection timed out                     |
| ENOSERVICE     | 1001 | 否    | 找不到服务，不太出现，一般会返回ENOMETHOD。               |                                          |
| ENOMETHOD      | 1002 | 否    | 找不到方法。                                   | 形式广泛，常见如"Fail to find method=..."        |
| EREQUEST       | 1003 | 否    | request格式或序列化错误，client端和server端都可能设置     | 形式广泛："Missing required fields in request: ...""Fail to parse request message, ...""Bad request" |
| EAUTH          | 1004 | 否    | 认证失败                                     | "Authentication failed"                  |
| ETOOMANYFAILS  | 1005 | 否    | ParallelChannel中太多子channel失败             | "%d/%d channels failed, fail_limit=%d"   |
| EBACKUPREQUEST | 1007 | 是    | 触发backup request时设置，用户一般在/rpcz里看到        | “reached backup timeout=%dms"            |
| ERPCTIMEDOUT   | 1008 | 否    | RPC超时                                    | "reached timeout=%dms"                   |
| EFAILEDSOCKET  | 1009 | 是    | RPC进行过程中TCP连接出现问题                        | "The socket was SetFailed"               |
| EHTTP          | 1010 | 否    | 失败的HTTP访问（非2xx状态码）均使用这个错误码。默认不重试，可通过RetryPolicy定制 | Bad http call                            |
| EOVERCROWDED   | 1011 | 是    | 连接上有过多的未发送数据，一般是由于同时发起了过多的异步访问。可通过参数-socket_max_unwritten_bytes控制，默认8MB。 | The server is overcrowded                |
| EINTERNAL      | 2001 | 否    | Server端Controller.SetFailed没有指定错误码时使用的默认错误码。 | "Internal Server Error"                  |
| ERESPONSE      | 2002 | 否    | response解析或格式错误，client端和server端都可能设置     | 形式广泛"Missing required fields in response: ...""Fail to parse response message, ""Bad response" |
| ELOGOFF        | 2003 | 是    | Server已经被Stop了                           | "Server is going to quit"                |
| ELIMIT         | 2004 | 是    | 同时处理的请求数超过ServerOptions.max_concurrency了 | "Reached server's limit=%d on concurrent requests", |

# 自定义错误码

在C++/C中你可以通过宏、常量、protobuf enum等方式定义ErrorCode:
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
strerror/strerror_r不认识使用BAIDU_REGISTER_ERRNO定义的错误码，自然地，printf类的函数中的%m也不能转化为对应的描述，你必须使用%s并配以berror()。
```c++
errno = ESTOP;
printf("Describe errno: %m\n");                               // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", strerror_r(errno, NULL, 0));   // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", berror());                     // [Correct] Describe errno: the thread is stopping
printf("Describe errno: %s\n", berror(errno));                // [Correct] Describe errno: the thread is stopping
```
当同一个error code被重复注册时，如果都是在C++中定义的，那么会出现链接错误：

```
redefinition of `class BaiduErrnoHelper<30>'
```
否则在程序启动时会abort：
```
Fail to define EMYERROR(30) which is already defined as `Read-only file system', abort
```

总的来说这和RPC框架没什么关系，直到你希望通过RPC框架传递ErrorCode。这个需求很自然，不过你得确保不同的模块对ErrorCode的理解是相同的，否则当两个模块把一个错误码理解为不同的错误时，它们之间的交互将出现无法预计的行为。为了防止这种情况出现，你最好这么做：
- 优先使用系统错误码，它们的值和含义是固定不变的。
- 多个交互的模块使用同一份错误码定义，防止后续修改时产生不一致。
- 使用BAIDU_REGISTER_ERRNO描述新错误码，以确保同一个进程内错误码是互斥的。 
