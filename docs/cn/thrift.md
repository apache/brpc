[English Version](../en/thrift.md)

[thrift](https://thrift.apache.org/)是近几年应用较广的Facebook发布的RPC服务, 为了使用户更方便,快捷的利用bthread的并发能力，brpc实现并支持thrift工作在NonBlocking模式下的协议(FramedProtocol), 注意本文中所说的thrift协议一律指的是此种情况下的thrift协议.
示例程序：[example/thrift_extension_c++](https://github.com/brpc/brpc/tree/master/example/thrift_extension_c++/)

相比使用官方原生的优势有：

- 线程安全。用户不需要为每个线程建立独立的client。
- 支持同步、异步、批量同步、批量异步等访问方式，能使用ParallelChannel等组合访问方式。
- 支持多种连接方式(连接池, 短连接), 支持超时、backup request、取消、tracing、内置服务等一系列RPC基本福利。

# 编译依赖及运行
默认brpc编译是不启用thrift协议支持的, 目的是在用户不需要thrift协议支持的情况下可以不安装thrift依赖. 如果用户启用thrift协议的话, 在配置brpc环境的时候加上--with-thrift参数(实际上是在Makefile里面启用ENABLE_THRIFT_FRAMED_PROTOCOL宏)

安装thrift依赖, ubuntu环境下
```bash
wget http://www.us.apache.org/dist/thrift/0.11.0/thrift-0.11.0.tar.gz
tar -xf thrift-0.11.0.tar.gz
cd thrift-0.11.0/
./configure --prefix=/usr --with-ruby=no --with-python=no --with-java=no --with-go=no --with-perl=no --with-php=no --with-csharp=no --with-erlang=no --with-lua=no --with-nodejs=no
make CPPFLAGS=-DFORCE_BOOST_SMART_PTR -j 3 -s
sudo make install
```
配置brpc支持thrift协议
```bash
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --nodebugsymbols --with-thrift
```
编译完成后会生成libbrpc.a 和libbrpc_thrift.a, thrift扩展协议以静态库的方式提供给用户, 用户在需要启用thrift协议的时候链接即可

# Thrift 原生消息定义, echo.thrift:
```c++
namespace cpp example

struct EchoRequest {
    1: required string data;
    2: required i32 s;
}

struct EchoResponse {
    1: required string data;
}

service EchoService {
    EchoResponse Echo(1:EchoRequest request);
}
```


# Client端访问下游thrift server

创建一个访问thrift server的Channel：

```c++
#include <brpc/channel.h>
#include <brpc/details/thrift_utils.h>
#include <brpc/thrift_message.h>
...

DEFINE_string(server, "0.0.0.0:8019", "IP Address of thrift server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
...
  
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_THRIFT;
brpc::Channel thrift_channel;
if (thrift_channel.Init(Flags_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
   LOG(ERROR) << "Fail to initialize thrift channel";
   return -1;
}
...
```
构造thrift请求, 并发送, ThriftMessage是模板类, 里面托管了thrift原生消息, 通过raw()方法可以可以直接操作原生thrift消息
```c++
 // wrapper thrift raw request into ThriftMessage
 // example::[EchoRequest/EchoResponse]是thrfit原生定义的消息(通过thrift代码生成工具生成)
 brpc::ThriftMessage<example::EchoRequest> req;
 brpc::ThriftMessage<example::EchoResponse> res;

 req.raw().data = "hello";

 cntl.set_thrift_method_name("Echo");

 channel.CallMethod(NULL, &cntl, &req, &res, NULL);

 if (cntl.Failed()) {
     LOG(ERROR) << "Fail to send thrift request, " << cntl.ErrorText();
     return -1;
 } 
```

# Server端处理上游thrfit请求

类似原生brpc协议, 用户需要实现自己的thrift handler, 继承自brpc::ThriftService
由于thrift协议本身的限制, 在服务端只能获取到method name(通过controller.thrift_method_name()方法), 无法获取到service name, 这和原生的thrift实现是一致的, 也就意味着在一个brpc server中只能有一个thrift service
```c++
// Implement User Thrift Service handler
class MyThriftProtocolPbManner : public brpc::ThriftService {
public:
    void ProcessThriftFramedRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              brpc::ThriftMessage* request,
                              brpc::ThriftMessage* response,
                              brpc::ThriftClosure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        if (cntl->Failed()) {
            // NOTE: You can send back a response containing error information
            // back to client instead of closing the connection.
            cntl->CloseConnection("Close connection due to previous error");
            return;
        }

        example::EchoRequest* req = request->Cast<example::EchoRequest>();
        example::EchoResponse* res = response->Cast<example::EchoResponse>();

        // process with req and res
        res->data = req->data + "user data";

        LOG(INFO) << "success to process thrift request in brpc with pb manner";

    }

};
```

注册thrift service并启动服务
```c++
brpc::Server server;
    brpc::ServerOptions options;
    options.thrift_service = new MyThriftProtocolPbManner;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;

    // Start the server.
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }
```