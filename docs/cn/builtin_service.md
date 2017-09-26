# 什么是内置服务？

内置服务以多种形式展现服务器内部状态，提高你开发和调试服务的效率。brpc通过HTTP协议提供内置服务，可通过浏览器或curl访问，服务器会根据User-Agent返回纯文本或html，你也可以添加?console=1要求返回纯文本。我们在自己的开发机上启动了[一个长期运行的例子](http://brpc.baidu.com:8765/)，你可以点击后随便看看。对于服务端口被限的情况（比如百度内不是所有的端口都能被笔记本访问到），可以使用[rpc_view](rpc_view.md)转发或在命令行中使用curl \<SERVER-URL\>。

从浏览器访问： 

![img](../images/builtin_service_more.png)

 从命令行访问：

 ![img](../images/builtin_service_from_console.png) 

# 安全模式

出于安全考虑，直接对外服务需要关闭内置服务（包括经过nginx或其他http server转发流量的），具体方法请阅读[这里](server.md#安全模式)。

# 主要服务

[status](status.md)

[vars](vars.md)

[connections](connections.md)

[flags](flags.md)

[rpcz](rpcz.md)

[cpu profiler](cpu_profiler.md)

[heap profiler](heap_profiler.md)

[contention profiler](contention_profiler.md)

# 其他服务

[version服务](http://brpc.baidu.com:8765/version)可以查看服务器的版本。用户可通过Server::set_version()设置Server的版本，如果用户没有设置，框架会自动为用户生成，规则：`brpc_server_<service-name1>_<service-name2> ...`

![img](../images/version_service.png)

[health服务](http://brpc.baidu.com:8765/health)可以探测服务的存活情况。

![img](../images/health_service.png)

[protobufs服务](http://brpc.baidu.com:8765/protobufs)可以查看程序中所有的protobuf结构体。

![img](../images/protobufs_service.png)

[vlog服务](http://brpc.baidu.com:8765/vlog)可以查看程序中当前可开启的[VLOG](streaming_log.md#VLOG)。

![img](../images/vlog_service.png)

dir服务可以浏览服务器上的所有文件，这个服务很敏感，默认关闭也不建议打开。

threads服务可以查看进程内所有线程的运行状况，调用时对程序性能影响较大，默认关闭。
