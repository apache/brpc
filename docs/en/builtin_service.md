[中文版](../cn/builtin_service.md)

# Builtin Services

Builtin services expose internal status of servers in different pespectives, making development and debugging over brpc more efficient. brpc serves builting services via HTTP, which can be easily accessed through curl and web browsers. Servers respond plain text or html according to `User-Agent` in the request header, or you may append `?console=1` to the uri to force the server to respond in plain text. Check the [example](http://brpc.baidu.com:8765/) running on our dev machine(only accessible from Baidu internal) for more details. If the port is forbidden from where you run curl or web browser (e.g. not all ports are accessible from a web browser inside Baidu), you can use [rpc_view](rpc_view.md) for proxying.

Following 2 screenshots show accesses to builtin services from a web browser and a terminal respectively.  Note that the logo is the codename inside Baidu, and being modified to brpc in opensourced version.

**From a web browser**

![img](../images/builtin_service_more.png)

**From a terminal**

![img](../images/builtin_service_from_console.png)

# Security Mode

To avoid potential attacks and information leaks, builtin services **must** be hidden on servers that may be accessed from public, including the ones proxied by nginx or other http servers. Click [here](server.md#security-mode) for more details.

# Main services:

[/status](status.md): displays brief status of all services.

[/vars](vars.md): lists user-customizable counters on miscellaneous metrics.

[/connections](../cn/connections.md): lists all connections and their stats.

[/flags](../cn/flags.md): lists all gflags, some of them are modifiable at run-time.

[/rpcz](../cn/rpcz.md): traces all RPCs.

[cpu profiler](../cn/cpu_profiler.md): analyzes CPU hotspots.

[heap profiler](../cn/heap_profiler.md): shows how memory are allocated.

[contention profiler](../cn/contention_profiler.md): analyzes lock contentions.

# Other services

[/version](http://brpc.baidu.com:8765/version) shows version of the server. Call Server::set_version() to specify version of the server, or brpc would generate a default version like `brpc_server_<service-name1>_<service-name2> ...`

![img](../images/version_service.png)

[/health](http://brpc.baidu.com:8765/health) shows whether this server is alive or not.

![img](../images/health_service.png)

[/protobufs](http://brpc.baidu.com:8765/protobufs) shows scheme of all protobuf messages inside the server.

![img](../images/protobufs_service.png)

[/vlog](http://brpc.baidu.com:8765/vlog) shows all the [VLOG](streaming_log.md#VLOG) that can be enabled(not working with glog).

![img](../images/vlog_service.png)

/dir: browses all files on the server, convenient but too dangerous, disabled by default.

/threads: displays information of all threads of the process, hurting performance significantly when being turned on, disabled by default.
