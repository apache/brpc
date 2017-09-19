brcc use [brpc::Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h) to set the parameters for RPC and fetch RPC result. `ErrorCode()` and `ErrorText()` are two methods of the Controller, which are the error code and error description of the RPC. It's accessible only after RPC finishes, otherwise the result is undefined. `ErrorText()` is defined by the base class of the Controller: `google::protobuf::RpcController`, while `ErrorCode()` is defined by `brpc::Controller`. Controller also has a `Failed()` method to tell whether RPC fails or not. The following shows the relationship among the three:

-  When `Failed()` is true, `ErrorCode()` can't be 0 and `ErrorText()` is a non-empty error description
- When `Failed()` is false, `ErrorCode()` must be 0 and `ErrorText()` is undefined (currently in brpc it will be empty, but you should not rely on this)

# Set Error to RPC

Both client and server side have Controller object, through which you can use `setFailed()` to modify ErrorCode and ErrorText. Multiple calls to `Controller::SetFailed` leaves the last ErrorCode only, but ErrorText will be **concatenated** instead of overwriting. The framework will also add prefix to the ErrorText: the number of retry at the client side and the address information at the server side.

`Controller::SetFailed()` at the client side is usually called by the framework, such as sending failure,  incomplete response, and so on. Only under some complex situation may the user set error at the client side. For example, you may need to set error to RPC if an error was found during additional check before sending.

`Controller::SetFailed()` at the server-side is often called by the user in the service callback. Generally speaking when error occurs, a user calls `SetFailed()` and then releases all the resources before return. The framework will fill the error code and error message into response according to communication protocol, and then these will be received and filled into Controller at the client side so that users can fetch them after RPC completes. Note that **it's not common to print additional error log when calling  `SetFailed()` at the server side**, as logging may lead to huge lag due to heavy disk IO. An error prone client could easily slow the speed of the entire server, and thus affect other clients.  This can even become a security issue in theory. If you really want to see the error message on the server side, you can open the **-log_error_text** gflag (for online service access `/flags/log_error_text?Setvalue=true` to turn it on dynamically). The server will print the ErrorText of the Controller for each failed RPC.

# Error Code in brpc

All error codes in brpc are defined in [errno.proto](https://github.com/brpc/brpc/blob/master/src/brpc/errno.proto), those begin with *SYS_* come from linux system, which are exactly the same as `/usr/include/errno.h`. The reason we put it in proto is to cross language. The rest of the error codes belong to brpc itself.

You can use [berror(error_code)](https://github.com/brpc/brpc/blob/master/src/butil/errno.h) to get the error description for an error code, and `berror()` for [system errno](http://www.cplusplus.com/reference/cerrno/errno/). Note that **ErrorText() != berror(ErorCode())**, since `ErrorText()` contains more specific information. brpc includes berror by default so that you can use it in your project directly.

The following table shows some common error codes and their description: 

| Error Code     | Value | Retry | Situation                                | Log Message                              |
| -------------- | ----- | ----- | ---------------------------------------- | ---------------------------------------- |
| EAGAIN         | 11    | Yes   | Too many requests at the same time. Hardly happens as it's a soft limit. | Resource temporarily unavailable         |
| ETIMEDOUT      | 110   | Yes   | Connection timeout.                      | Connection timed out                     |
| ENOSERVICE     | 1001  | No    | Can't locate the service. Hardly happens, usually ENOMETHOD instead |                                          |
| ENOMETHOD      | 1002  | No    | Can't locate the target method.          | Fail to find method=...                  |
| EREQUEST       | 1003  | No    | request格式或序列化错误，client端和server端都可能设置     | Missing required fields in request: ...  |
|                |       |       |                                          | Fail to parse request message, ...       |
|                |       |       |                                          | Bad request                              |
| EAUTH          | 1004  | No    | Authentication failed                    | Authentication failed                    |
| ETOOMANYFAILS  | 1005  | No    | Too many sub channel failure inside a ParallelChannel. | %d/%d channels failed, fail_limit=%d     |
| EBACKUPREQUEST | 1007  | Yes   | Trigger the backup request. Can be seen from /rpcz | reached backup timeout=%dms              |
| ERPCTIMEDOUT   | 1008  | No    | RPC timeout.                             | reached timeout=%dms                     |
| EFAILEDSOCKET  | 1009  | Yes   | Connection broken during RPC             | The socket was SetFailed                 |
| EHTTP          | 1010  | No    | Non 2xx status code of a HTTP request. No retry by default, but it can be changed through RetryPolicy. | Bad http call                            |
| EOVERCROWDED   | 1011  | Yes   | Too many buffering message at the sender side. Usually caused by lots of concurrent asynchronous requests. Can be tuned by `-socket_max_unwritten_bytes`, default is 8MB. | The server is overcrowded                |
| EINTERNAL      | 2001  | No    | Default error code when calling `Controller::SetFailed` without one. | Internal Server Error                    |
| ERESPONSE      | 2002  | No    | Parsing/Format error in response. Could be set both by the client and the server. | Missing required fields in response: ... |
|                |       |       |                                          | Fail to parse response message,          |
|                |       |       |                                          | Bad response                             |
| ELOGOFF        | 2003  | Yes   | Server has already been stopped          | Server is going to quit                  |
| ELIMIT         | 2004  | Yes   | The number of concurrent processing requests exceeds `ServerOptions.max_concurrency` | Reached server's limit=%d on concurrent requests, |

# User-Defined Error Code

In C/C++, you can use macro, or constant or protobuf enum to define your own ErrorCode:

```c++
#define ESTOP -114                // C/C++
static const int EMYERROR = 30;   // C/C++
const int EMYERROR2 = -31;        // C++ only
```

If you need to get the error description through berror, you can register it in the global scope of your c/cpp file by:

`BAIDU_REGISTER_ERRNO(error_code, description)`

```c++
BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")
BAIDU_REGISTER_ERRNO(EMYERROR, "my error")
```

Note that `strerror/strerror_r` can't recognize error codes defined by `BAIDU_REGISTER_ERRNO`. Neither can `%m` inside `printf`. You must use `%s` along with `berror`:

```c++
errno = ESTOP;
printf("Describe errno: %m\n");                               // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", strerror_r(errno, NULL, 0));   // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", berror());                     // [Correct] Describe errno: the thread is stopping
printf("Describe errno: %s\n", berror(errno));                // [Correct] Describe errno: the thread is stopping
```

When an error code has already been registered, it will cause a link error if it's defined in C++:

```
redefinition of `class BaiduErrnoHelper<30>'
```

Otherwise, the program will abort once starts:

```
Fail to define EMYERROR(30) which is already defined as `Read-only file system', abort
```

In general this has nothing to do with the RPC framework unless you want to pass ErrorCode through it. It's a natural scenario but you have to make sure that different modules have the same understanding of the same ErrorCode. Otherwise, the result is unpredictable if two modules interpret an error code differently. In order to prevent this from happening, you'd better follow these:

- Prefer system error codes since their meanings are fixed.
- Use the same code for error definitions among multiple modules to prevent inconsistencies during later modifications.
- Use `BAIDU_REGISTER_ERRNO` to describe a new error code to ensure that the same error code is mutually exclusive inside a process.