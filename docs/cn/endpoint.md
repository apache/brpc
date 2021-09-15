# UDS及IPV6支持

butil::EndPoint已经支持UDS(Unix Domain Socket)及IPV6。

## 基本用法
代码用法：

```cpp
EndPoint ep;
str2endpoint("unix:path.sock", &ep); // 初始化一个UDS的EndPoint
str2endpoint("[::1]:8086", &ep);  // 初始化一个IPV6的EndPoint
str2endpoint("[::1]", 8086, &ep);  // 初始化一个IPV6的EndPoint
 
// 获取EndPoint的类型
sa_family_t type = get_endpoint_type(ep); // 可能为AF_INET、AF_INET6或AF_UNIX
 
// 使用EndPoint，和原来的方式一样
LOG(DEBUG) << ep; // 打印EndPoint
std::string ep_str = endpoint2str(ep).c_str(); // EndPoint转str
tcp_listen(ep); // 用监听EndPoint表示的tcp端口
tcp_connect(ep, NULL); // 用连接EndPoint表示的tcp端口
 
sockaddr_storage ss;
socklen_t socklen = 0;
endpoint2sockaddr(ep, &ss, &socklen); // 将EndPoint转为sockaddr结构，以便调用系统函数
```

## 在brpc中使用UDS或IPV6

只需要在原来输入IPV4字符串的时候，填写UDS路径或IPV6地址即可，如：

```cpp
server.Start("unix:path.sock", options); // 启动server监听UDS地址
server.Start("[::0]:8086", options);  // 启动server监听IPV6地址
 
channel.Init("unix:path.sock", options); // 初始化single server的Channel，访问UDS地址
channel.Init("list://[::1]:8086,[::1]:8087", "rr", options); // 初始化带LB的Channel，访问IPV6地址
```

通过 example/echo_c++ ，展示了如何使用UDS或IPV6：

```bash
./echo_server -listen_addr='unix:path.sock' & # 启动Server监听UDS地址
./echo_server -listen_addr='[::0]:8080' & # 启动Server监听IPV6端口

./echo_client -server='unix:path.sock' # 启动Client访问UDS地址
./echo_client -server='[::1]:8080' # 启动Client访问IPV6端口
```

## 限制

由于EndPoint结构被广泛地使用，为了保证对存量代码的兼容性（包括ABI兼容性），目前采用的实现方式是不修改EndPoint的ABI定义，使用原来的ip字段作为id，port字段来做为扩展标记，把真正的信息存在一个外部的数据结构中。

这种实现方式对于现存的仅使用IPV4的代码是完全兼容的，但对于使用UDS或IPV6的用户，有些代码是不兼容的，比如直接访问EndPoint的ip和port成员的代码。

关于UDS和IPV6，目前已知的一些限制：

- 不兼容rpcz
- 不支持使用PortRange方式启动server
- 不支持在ServerOption中指定internal_port
- IPV6不支持link local地址（fe80::开头的地址)