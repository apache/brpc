[English Version](../en/thrift.md)

[thrift](https://thrift.apache.org/)是应用较广的RPC框架，最初由Facebook发布，后交由Apache维护。为了和thrift服务互通，同时解决thrift原生方案在多线程安全、易用性、并发能力等方面的一系列问题，brpc实现并支持thrift在NonBlocking模式下的协议(FramedProtocol), 下文均直接称为thrift协议。

示例程序：[example/thrift_extension_c++](https://github.com/brpc/brpc/tree/master/example/thrift_extension_c++/)

相比使用原生方案的优势有：
- 线程安全。用户不需要为每个线程建立独立的client.
- 支持同步、异步、批量同步、批量异步等访问方式，能使用ParallelChannel等组合访问方式.
- 支持多种连接方式(连接池, 短连接), 支持超时、backup request、取消、tracing、内置服务等一系列RPC基本福利.

# 编译依赖及运行
为了复用解析代码，brpc对thrift的支持仍需要依赖thrift库以及thrift生成的代码，thrift格式怎么写，代码怎么生成，怎么编译等问题请参考thrift官方文档。

brpc默认不启用thrift支持也不需要thrift依赖。但如果需用thrift协议, 配置brpc环境的时候需加上--with-thrift参数.

Ubuntu环境下安装thrift依赖
先参考[官方wiki](https://thrift.apache.org/docs/install/debian)安装好必备的依赖和工具，然后从[官网](https://thrift.apache.org/download)下载thrift源代码，解压编译。
```bash
wget http://www.us.apache.org/dist/thrift/0.11.0/thrift-0.11.0.tar.gz
tar -xf thrift-0.11.0.tar.gz
cd thrift-0.11.0/
./configure --prefix=/usr --with-ruby=no --with-python=no --with-java=no --with-go=no --with-perl=no --with-php=no --with-csharp=no --with-erlang=no --with-lua=no --with-nodejs=no
make CPPFLAGS=-DFORCE_BOOST_SMART_PTR -j 4 -s
sudo make install
```

配置brpc支持thrift协议后make。编译完成后会生成libbrpc.a, 其中包含了支持thrift协议的扩展代码, 像正常使用brpc的代码一样链接即可。
```bash
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --with-thrift
```

# Client端访问thrift server
基本步骤：
- 创建一个协议设置为brpc::PROTOCOL_THRIFT的Channel
- 定义brpc::ThriftMessage<原生Request>作为请求，brpc::ThriftMessage<原生Response>作为回复。raw()方法可以操作原生thrift消息。
- 通过Controller::set_thrift_method_name()设置thrift方法名。

示例代码如下：
```c++
#include <brpc/channel.h>
#include <brpc/thrift_message.h>         // 定义了ThriftMessage
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

# Server端处理thrift请求
用户通过继承brpc::ThriftService实现处理逻辑，既可以调用thrift生成的handler以直接复用原有的函数入口，也可以像protobuf服务那样直接读取request和设置response。
```c++
class MyThriftProtocol : public brpc::ThriftService {
public:
    void ProcessThriftFramedRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              brpc::ThriftFramedMessage* request,
                              brpc::ThriftFramedMessage* response,
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

        // 通过cntl->thrift_method_name()获得被访问的方法名
        if (_native_handler) {
            _native_handler->Echo(*res, *req);
        } else {
            res->data = req->data + "user data";
        }
    }
private:
    EchoServiceHandler* _native_handler;
};
```

实现好thrift service后，设置到ServerOptions.thrift_service并启动服务
```c++
    brpc::Server server;
    brpc::ServerOptions options;
    options.thrift_service = new MyThriftProtocol;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;

    // Start the server.
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }
```
