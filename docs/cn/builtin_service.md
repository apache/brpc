# 什么是内置服务？

内置服务以多种形式展现服务器内部状态，提高你开发和调试服务的效率。baidu-rpc通过HTTP协议提供内置服务，可通过浏览器或curl访问，服务器会根据User-Agent返回纯文本或html，你也可以添加?console=1要求返回纯文本。我们在自己的开发机上启动了[一个长期运行的例子](http://brpc.baidu.com:8765/)，你可以点击后随便看看。服务端口只有在8000-8999内才能被笔记本访问到，对于范围之外的端口可以使用[rpc_view](rpc_view.md)或在命令行中使用curl <SERVER-URL>。

从浏览器访问： 

![img](../images/builtin_service_more.png)

 从命令行访问：

 ![img](../images/builtin_service_from_console.png) 

# 安全模式

出于安全考虑，直接对外服务需要关闭内置服务（包括经过nginx或其他http server转发流量的），具体方法请阅读[这里](server.md#安全模式)。

# 主要服务

- ​

# 其他服务

[version服务](http://brpc.baidu.com:8765/version)可以查看服务器的版本。用户可通过Server::set_version()设置Server的版本，如果用户没有设置，框架会自动为用户生成，规则：`baidu_rpc_server_<service-name1>_<service-name2> ...`

[health服务](http://brpc.baidu.com:8765/health)可以探测服务的存活情况。

[protobufs服务](http://brpc.baidu.com:8765/protobufs)可以查看程序中所有的protobuf结构体。

[vlog服务](http://brpc.baidu.com:8765/vlog)可以查看程序中当前可开启的[VLOG](streaming_log.md#VLOG)。

dir服务可以浏览服务器上的所有文件，这个服务比较敏感，默认关闭。

threads服务可以查看进程内所有线程的运行状况，调用时对程序性能较大，默认关闭。

其他还有一些调试服务，如有需求请联系我们。
