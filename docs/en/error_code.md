[中文版](../cn/error_code.md)

brpc use [brpc::Controller](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h) to set and get parameters for one RPC. `Controller::ErrorCode()` and `Controller::ErrorText()` return error code and description of the RPC respectively, only accessible after completion of the RPC, otherwise the result is undefined. `ErrorText()` is defined by the base class of the Controller: `google::protobuf::RpcController`, while `ErrorCode()` is defined by `brpc::Controller`. Controller also has a method `Failed()` to tell whether RPC fails or not. Relations between the three methods:

-  When `Failed()` is true, `ErrorCode()` must be non-zero and `ErrorText()` be non-empty.
-  When `Failed()` is false, `ErrorCode()` is 0 and `ErrorText()` is undefined (it's empty in brpc currently, but you'd better not rely on this)

# Mark RPC as failed

Both client and server in brpc have `Controller`, which can be set with `setFailed()` to modify ErrorCode and ErrorText. Multiple calls to `Controller::SetFailed` leave the last ErrorCode and **concatenate** ErrorTexts rather than leaving the last one. The framework elaborates ErrorTexts by adding extra prefixes: number of retries at client-side and address of the server at server-side.

`Controller::SetFailed()` at client-side is usually called by the framework, such as sending failure,  incomplete response, and so on. Error may be set at client-side under some situations. For example, you may set error to the RPC if an additional check before sending the request is failed.

`Controller::SetFailed()` at server-side is often called by the user in the service callback. Generally speaking when error occurs, users call `SetFailed()`, release all the resources, and return from the callback. The framework fills the error code and message into the response according to communication protocol. When the response is received, the error inside are set into the client-side Controller so that users can fetch them after end of RPC. Note that **server does not print errors to clients by default**, as frequent loggings may impact performance of the server significantly due to heavy disk IO. A client crazily producing errors could slow the entire server down and affect all other clients, which can even become an attacking method against the server. If you really want to see error messages on the server, turn on the gflag **-log_error_text** (modifiable at run-time), the server will log the ErrorText of corresponding Controller of each failed RPC.

# Error Code in brpc

All error codes in brpc are defined in [errno.proto](https://github.com/brpc/brpc/blob/master/src/brpc/errno.proto), in which those begin with *SYS_* are defined by linux system and exactly same with the ones defined in `/usr/include/errno.h`. The reason that we put it in .proto is to cross language. The rest of the error codes are defined by brpc.

[berror(error_code)](https://github.com/brpc/brpc/blob/master/src/butil/errno.h) gets description for the error code, and `berror()` gets description for current [system errno](http://www.cplusplus.com/reference/cerrno/errno/). Note that **ErrorText() != berror(ErorCode())** since `ErrorText()` contains more specific information. brpc includes berror by default so that you can use it in your project directly.

Following table shows common error codes and their descriptions: 

| Error Code     | Value | Retry | Description                              | Logging message                          |
| -------------- | ----- | ----- | ---------------------------------------- | ---------------------------------------- |
| EAGAIN         | 11    | Yes   | Too many requests at the same time, hardly happening as it's a soft limit. | Resource temporarily unavailable         |
| ETIMEDOUT      | 110   | Yes   | Connection timeout.                      | Connection timed out                     |
| EHOSTDOWN      | 112   | Yes   | No available server to send request. The servers may be stopped or stopping(returning ELOGOFF). | "Fail to select server from …"  "Not connected to … yet" |
| ENOSERVICE     | 1001  | No    | Can't locate the service, hardly happening and usually being ENOMETHOD instead |                                          |
| ENOMETHOD      | 1002  | No    | Can't locate the method.                 | Misc forms, common ones are "Fail to find method=…" |
| EREQUEST       | 1003  | No    | fail to serialize the request, may be set on either client-side or server-side | Misc forms: "Missing required fields in request: …" "Fail to parse request message, …"  "Bad request" |
| EAUTH          | 1004  | No    | Authentication failed                    | "Authentication failed"                  |
| ETOOMANYFAILS  | 1005  | No    | Too many sub-channel failures inside a ParallelChannel | "%d/%d channels failed, fail_limit=%d"   |
| EBACKUPREQUEST | 1007  | Yes   | Set when backup requests are triggered. Not returned by ErrorCode() directly, viewable from spans in /rpcz | "reached backup timeout=%dms"            |
| ERPCTIMEDOUT   | 1008  | No    | RPC timeout.                             | "reached timeout=%dms"                   |
| EFAILEDSOCKET  | 1009  | Yes   | The connection is broken during RPC      | "The socket was SetFailed"               |
| EHTTP          | 1010  | No    | HTTP responses with non 2xx status code are treated as failure and set with this code. No retry by default, changeable by customizing RetryPolicy. | Bad http call                            |
| EOVERCROWDED   | 1011  | Yes   | Too many messages to buffer at the sender side. Usually caused by lots of concurrent asynchronous requests. Modifiable by `-socket_max_unwritten_bytes`, 8MB by default. | The server is overcrowded                |
| EINTERNAL      | 2001  | No    | The default error for `Controller::SetFailed` without specifying a one. | Internal Server Error                    |
| ERESPONSE      | 2002  | No    | fail to serialize the response, may be set on either client-side or server-side | Misc forms: "Missing required fields in response: …" "Fail to parse response message, " "Bad response" |
| ELOGOFF        | 2003  | Yes   | Server has been stopped                  | "Server is going to quit"                |
| ELIMIT         | 2004  | Yes   | Number of requests being  processed concurrently exceeds `ServerOptions.max_concurrency` | "Reached server's limit=%d on concurrent requests" |

# User-defined Error Code

In C/C++, error code can be defined in macros, constants or enums:

```c++
#define ESTOP -114                // C/C++
static const int EMYERROR = 30;   // C/C++
const int EMYERROR2 = -31;        // C++ only
```

If you need to get the error description through `berror`, register it in the global scope of your c/cpp file by `BAIDU_REGISTER_ERRNO(error_code, description)`, for example:

```c++
BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")
BAIDU_REGISTER_ERRNO(EMYERROR, "my error")
```

Note that `strerror` and `strerror_r` do not recognize error codes defined by `BAIDU_REGISTER_ERRNO`. Neither does the `%m` used in `printf`. You must use `%s` paired with `berror`:

```c++
errno = ESTOP;
printf("Describe errno: %m\n");                              // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", strerror_r(errno, NULL, 0));  // [Wrong] Describe errno: Unknown error -114
printf("Describe errno: %s\n", berror());                    // [Correct] Describe errno: the thread is stopping
printf("Describe errno: %s\n", berror(errno));               // [Correct] Describe errno: the thread is stopping
```

When the registration of an error code is duplicated, a linking error is generated provided it's defined in C++:

```
redefinition of `class BaiduErrnoHelper<30>'
```

Or the program aborts before start:

```
Fail to define EMYERROR(30) which is already defined as `Read-only file system', abort
```

You have to make sure that different modules have same understandings on same ErrorCode. Otherwise, interactions between two modules that interpret an error code differently may be undefined. To prevent this from happening, you'd better follow these:

- Prefer system error codes which have fixed values and meanings, generally.
- Share code on error definitions between multiple modules to prevent inconsistencies after modifications.
- Use `BAIDU_REGISTER_ERRNO` to describe new error code to ensure that same error code is defined only once inside a process.