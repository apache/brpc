The user can see the detailed information of the recent request through /rpcz, and can insert annotations (annotation), which is different from the tracing system (such as [dapper](http://static.googleusercontent.com/media/research.google.com/en //pubs/archive/36356.pdf)) To see the delay distribution of the overall system from a global perspective, rpcz is more of a debugging tool. Although the roles are different, the data sources of rpcz and tracing in brpc are the same . When the number of requests per second is less than 10,000, rpcz will record all requests. When it exceeds 10,000, rpcz will randomly ignore some requests and control the number of samples to about 10,000. rpcz can eliminate the data before the time window, set by the -span_keeping_seconds option, the default is 1 hour. [A long-running example] (http://brpc.baidu.com:8765/rpcz).

Regarding overhead: Our implementation completely avoids thread competition and has very low overhead. In the test scenario of qps 300,000, no obvious performance changes are observed. For most applications, it should be "free". Even if tens of millions of requests are collected, rpcz will not increase a lot of memory, generally within 50 megabytes. rpcz will take up some disk space (just like a log). If it is set to store data for one hour, it is generally about several hundred megabytes.

## Switch method

It is not enabled by default, adding [-enable_rpcz](http://brpc.baidu.com:8765/flags/*rpcz*) option will be enabled after startup.

| Name | Value | Description | Defined At |
| -------------------------- | -------------------- |- --------------------------------------- | ---------- ---------------------------- |
| enable_rpcz (R) | true (default:false) | Turn on rpcz | src/baidu/rpc/builtin/rpcz_service.cpp |
| rpcz_hex_log_id (R) | false | Show log_id in hexadecimal | src/baidu/rpc/builtin/rpcz_service.cpp |
| rpcz_database_dir | ./rpc_data/rpcz | For storing requests/contexts collected by rpcz. | src/baidu/rpc/span.cpp |
| rpcz_keep_span_db | false | Don't remove DB of rpcz at program's exit | src/baidu/rpc/span.cpp |
| rpcz_keep_span_seconds (R) | 3600 | Keep spans for at most so many seconds | src/baidu/rpc/span.cpp |

If -enable_rpcz is not added at startup, you can access SERVER_URL/rpcz/enable after startup to dynamically turn on rpcz, and access SERVER_URL/rpcz/disable to turn off. These two links are equivalent to accessing SERVER_URL/flags/enable_rpcz?setvalue=true and SERVER_URL/flags/enable_rpcz?setvalue=false. After r31010, rpc added a button to visually turn on and off in the html version.

![img](../images/rpcz_4.png)

![img](../images/rpcz_5.png)

If only brpc client or brpc is not used, see [here](dummy_server.md).

## Data presentation

The data displayed by /rpcz is divided into two layers.

### level one

See the overview of the latest request and click on the link to enter the second layer.

![img](../images/rpcz_6.png)

### Second floor

See the detailed information of a certain series (trace) or a certain request (span). Generally enter by clicking the link, you can also use trace= and span= as query-string to spell out the link

![img](../images/rpcz_7.png)

Content description:

-Time is divided into absolute time (such as 2015/01/21-20:20:30.817392, accurate to microseconds after the decimal point) and the difference between the previous time (such as 19, representing 19 microseconds).
-trace=ID is a bit like "session id", corresponding to all services involved in completing one external service in a system, that is, upstream and downstream servers share a trace-id. span=ID corresponds to the processing process of a request in a server or client. Trace-id and span-id are unique in probability.
-After request= and response= on the first layer page is the number of bytes of the data packet, including attachments but not protocol meta. The bytes of request and response in the second layer are generally in parentheses, such as 13 in "Responded(13)".
-Clicking on the link may visit rpcz on other servers, and clicking the browser back will usually return to the previous page location.
-I'm the last call, I'm about to ... are all user annotations.

## Annotation

As long as you use brpc, you can use [TRACEPRINTF](https://github.com/brpc/brpc/blob/master/src/brpc/traceprintf.h) to print content to the event stream, such as:

```c++
TRACEPRINTF("Hello rpcz %d", 123);
```

This annotation will be inserted into the rpcz corresponding to the request according to its occurrence time. From this perspective, rpcz is a request-level log. If you print the context along the road with TRACEPRINTF, you can see how long the request stays at each stage, and the data sets and parameters involved. This is a very useful feature.