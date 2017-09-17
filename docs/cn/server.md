# 示例程序

Echo的[server端代码](https://github.com/brpc/brpc/blob/master/example/echo_c++/server.cpp)。

# 填写proto文件

RPC接口规范规定百度内使用protobuf作为通用的描述语言，我们谈论的Service默认指google::protobuf::Service。

```C++
# 告诉protoc要生成C++ Service基类，如果是java或python，则应分别修改为java_generic_services和py_generic_services
option cc_generic_services = true;
 
message EchoRequest {
      required string message = 1;
};
message EchoResponse {
      required string message = 1;
};
 
service EchoService {
      rpc Echo(EchoRequest) returns (EchoResponse);
};
```

protobuf的更多用法请阅读[protobuf官方文档](https://developers.google.com/protocol-buffers/docs/proto#options)。

# 实现生成的Service基类

protoc运行后会生成echo.pb.cc和echo.pb.h文件，你得include echo.pb.h，实现其中的EchoService基类：

**my_echo_service.cpp** 

```c++
#include "echo.pb.h"
...
class MyEchoService : public EchoService  {
public:
    void Echo(::google::protobuf::RpcController* cntl_base,
              const ::example::EchoRequest* request,
              ::example::EchoResponse* response,
              ::google::protobuf::Closure* done) {
        // 这个对象确保在return时自动调用done->Run()
        brpc::ClosureGuard done_guard(done);
         
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // 填写response
        response->set_message(request->message());
    }
};
```

当客户端发来请求时，Echo会被调用。4个参数的含义分别是：

**controller**

在brpc中可以静态转为brpc::Controller（前提是这运行brpc的Server中），包含了所有request和response之外的参数集合，具体接口查阅[controller.h](https://github.com/brpc/brpc/blob/master/src/brpc/controller.h)

**request**

请求，只读的，解析自client端发来的二进制数据包。

**response**

回复。需要你来填充，如果你没有填充所有的**required**字段，框架会发现并令该次调用失败。

**done**

done包含了调用服务回调后的下一步动作，包括检查response正确性，序列化，打包，发送等逻辑。

> 不管成功失败，done->Run()必须在请求处理完成后被调用。

为什么框架不自己调用done？这是为了允许用户把done保存下来，在服务回调之后调用，即实现异步Service。

我们强烈建议使用**ClosureGuard**确保done->Run()被调用，即在服务回调开头的那句：

```c++
brpc::ClosureGuard done_guard(done);
```

不管在中间还是末尾脱离服务回调，都会使done_guard析构，其中会调用done->Run()。这个机制称为[RAII](https://en.wikipedia.org/wiki/Resource_Acquisition_Is_Initialization)。没有这个的话你得在每次return前都加上done->Run()，**极易忘记**。

在异步Service中，退出服务回调时请求未处理完成，done->Run()不应被调用，done应被保存下来供以后调用，乍看起来，这里并不需要用ClosureGuard。但在实践中，异步Service照样会因各种原因跳出回调，如果不使用ClosureGuard，一些分支很可能会忘记done->Run()。所以我们同样建议使用done_guard，与同步Service不同的是，为了避免脱离函数时done->Run()被调用，你可以调用done_guard.release()来释放其中的done。

一般来说，同步Service和异步Service分别按如下代码处理done：

```c++
class MyFooService: public FooService  {
public:
    // 同步服务
    void SyncFoo(::google::protobuf::RpcController* cntl_base,
                 const ::example::EchoRequest* request,
                 ::example::EchoResponse* response,
                 ::google::protobuf::Closure* done) {
         brpc::ClosureGuard done_guard(done);
         ...
    }
 
    // 异步服务
    void AsyncFoo(::google::protobuf::RpcController* cntl_base,
                  const ::example::EchoRequest* request,
                  ::example::EchoResponse* response,
                  ::google::protobuf::Closure* done) {
         brpc::ClosureGuard done_guard(done);
         ...
         done_guard.release();
    }
};
```

ClosureGuard的接口如下：

```c++
// RAII: Call Run() of the closure on destruction.
class ClosureGuard {
public:
    ClosureGuard();
    // Constructed with a closure which will be Run() inside dtor.
    explicit ClosureGuard(google::protobuf::Closure* done);
    
    // Call Run() of internal closure if it's not NULL.
    ~ClosureGuard();
 
    // Call Run() of internal closure if it's not NULL and set it to `done'.
    void reset(google::protobuf::Closure* done);
 
    // Set internal closure to NULL and return the one before set.
    google::protobuf::Closure* release();
};
```

Service在插入[brpc::Server](https://github.com/brpc/brpc/blob/master/src/brpc/server.h)后才可能提供服务。

## 标记当前调用为失败

调用Controller.SetFailed()可以把当前调用设置为失败，当发送过程出现错误时，框架也会调用这个函数。用户一般是在服务的CallMethod里调用这个函数，比如某个处理环节出错，SetFailed()后便可调用done->Run()并跳出函数了（如果使用了ClosureGuard的话在跳出函数时会自动调用done，不用手动）。Server端的done的逻辑主要是发送response回client，当其发现用户调用了SetFailed()后，会把错误信息送回client。client收到后，它的Controller::Failed()会为true（成功时为false），Controller::ErrorCode()和Controller::ErrorText()则分别是错误码和错误信息。

对于http访问，用户还可以设置[status-code](http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html)，在server端一般是调用controller.http_response().set_status_code()，标准的status-code定义在[http_status_code.h](https://github.com/brpc/brpc/blob/master/src/brpc/http_status_code.h)中。如果SetFailed了但没有设置status-code，默认设为brpc::HTTP_STATUS_INTERNAL_SERVER_ERROR（500错误）

## 获取Client的地址和端口

controller->remote_side()可获得发送该请求的client地址和端口，类型是butil::EndPoint。如果client是nginx，remote_side()是nginx的ip。要获取真实client的ip，可以在nginx里设置proxy_header ClientIp $remote_addr; 在rpc中通过controller->http_request().GetHeader("ClientIp")获得对应的值。

打印方式：

```c++
LOG(INFO) << "remote_side=" << cntl->remote_side(); 
printf("remote_side=%s\n", butil::endpoint2str(cntl->remote_side()).c_str());
```

## 获取Server的地址和端口

r31384前server在接受连接时会把server端的端口打印到日志中，但没有接口获得。r31384后调用controller->local_side()获得，类型是butil::EndPoint。

打印方式：

```c++
LOG(INFO) << "local_side=" << cntl->local_side(); 
printf("local_side=%s\n", butil::endpoint2str(cntl->local_side()).c_str());
```

## 异步Service

即done->Run()在Service回调之外被调用。

大多数Server在进入回调函数后使用brpc::ClosureGuard以确保在退出回调时done->Run()会被自动调用（向client发回response），这种是**同步service**。

有些server以等待后端服务返回结果为主，且处理时间特别长，为了及时地释放出线程资源，更好的办法是把done注册到被等待事件的回调中，等到事件发生后再调用done->Run()，这种是**异步service**。

异步service的最后一行一般是done_guard.release()以确保done_guard在析构时不会调用done->Run()，而是在事件回调中调用。例子请看[example/session_data_and_thread_local](https://github.com/brpc/brpc/tree/master/example/session_data_and_thread_local/)。

Service和Channel都可以使用done来表达后续的操作，但它们是完全不同的，请勿混淆：

* Service的done由框架创建，用户处理请求后调用done把response发回给client。
* Channel的done由用户创建，递给框架，待RPC结束后被框架调用以执行用户的后续代码。

# 插入Service

默认构造后的Server不包含任何服务，也不会对外提供服务，仅仅是一个对象。

通过AddService插入你的Service实例。

**server.h**

```c++
int AddService(google::protobuf::Service* service, ServiceOwnership ownership);
```

若ownership参数为SERVER_OWNS_SERVICE，Server在析构时会一并删除Service，否则应设为SERVER_DOESNT_OWN_SERVICE。插入上节中的MyEchoService代码如下：

```c++
brpc::Server server;
MyEchoService my_echo_service;
if (server.AddService(&my_echo_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG(FATAL) << "Fail to add my_echo_service";
    return -1;
}
```

Server启动后你无法再修改其中的Service。

# 启动

**server.h**

```c++
int Start(const char* ip_and_port_str, const ServerOptions* opt);
int Start(EndPoint ip_and_port, const ServerOptions* opt);
int Start(int port, const ServerOptions* opt);
int Start(const char *ip_str, PortRange port_range, const ServerOptions *opt);  // r32009后增加
```

"localhost:9000", "cq01-cos-dev00.cq01:8000", “127.0.0.1:7000"都是合法的"ip_and_port_str"。其他重载形式请阅读[server.h](https://github.com/brpc/brpc/blob/master/src/brpc/server.h)。

options为NULL时所有参数取默认值，如果你要使用非默认值，这么做就行了：

```c++
brpc::ServerOptions options;  // 包含了默认值
options.xxx = yyy;
...
server.Start(..., &options);
```

## 监听多个端口

一个server只能监听一个端口（不考虑ServerOptions.internal_port），需要监听N个端口就起N个Server。

# 停止

```c++
server.Stop(closewait_ms); // r28921后closewait_ms无效
server.Join();
```

Stop()不会阻塞，Join()会。分成两个函数的原因在于当多个Server需要退出时，可以先全部Stop再一起Join，如果一个个Stop/Join，可能得花费Server个数倍的等待时间。

r28921之前：如果closewait_ms不会0，且还有请求在被处理，那么Server不会立刻关闭与client的连接，而是继续处理请求，同时对client发来的新请求立刻回复ELOGOFF错误以防止新请求加入，这个过程最多持续closewait_ms毫秒，之后Server会关闭所有连接并退出。

r28921之后：closewait_ms失效，不管它是什么值，server在退出时总会等待所有正在被处理的请求完成，同时对新请求立刻回复ELOGOFF错误。这么做的原因在于只要server退出时仍有处理线程运行，就有访问到已释放内存的风险。如果你的server“退不掉”，很有可能是由于某个检索线程hang住了，

当client看到ELOGOFF时，会跳过对应的server，并在其他server上重试对应的请求。所以在一般情况下brpc总是“优雅退出”的，重启或上线时几乎不会或只会丢失很少量的流量。

r31371后增加了RunUntilAskedToQuit()函数，简化了大部分情况下server的运转和停止代码。在server.Start后，只需如下代码即会让server运行直到按到Ctrl-C。

```c++
// Wait until Ctrl-C is pressed, then Stop() and Join() the server.
server.RunUntilAskedToQuit();
 
// server已经停止了，这里可以写释放资源的代码。
```

Join()完成后可以修改其中的Service，并重新Start。

# 被HTTP client访问

每个Protobuf Service默认可以通过HTTP访问，body被视为json串可通过名字与pb消息相互转化。以[echo server](http://brpc.baidu.com:8765/)为例，你可以通过http协议访问这个服务。

```shell
# before r31987
$ curl -H 'Content-Type: application/json' -d '{"message":"hello"}' http://brpc.baidu.com:8765/EchoService/Echo
{"message":"hello"}
 
# after r31987
$ curl -d '{"message":"hello"}' http://brpc.baidu.com:8765/EchoService/Echo
{"message":"hello"}
```

注意：

- 在r31987前必须把Content-Type指定为applicaiton/json（-H选项），否则服务器端不会做转化。r31987后不需要。
- r34740之后可以指定Content-Type为application/proto (-H选项) 来传递protobuf二进制格式.

## json<=>pb

json通过名字与pb字段一一对应，结构层次也应匹配。json中一定要包含pb的required字段，否则转化会失败，对应请求会被拒绝。json中可以包含pb中没有定义的字段，但不会作为pb的unknown字段被继续传递。转化规则详见[json <=> protobuf](json2pb.md)。

r34532后增加选项-pb_enum_as_number，开启后pb中的enum会转化为它的数值而不是名字，比如在`enum MyEnum { Foo = 1; Bar = 2; };`中不开启此选项时MyEnum类型的字段会转化为"Foo"或"Bar"，开启后为1或2。此选项同时影响client发出的请求和server返回的回复。由于转化为名字相比数值有更好的前后兼容性，此选项只应用于兼容无法处理enum为名字的场景。

## 兼容(很)老版本client

15年时，brpc允许一个pb service被http协议访问时，不设置pb请求，即使里面有required字段。一般来说这种service会自行解析http请求和设置http回复，并不会访问pb请求。但这也是非常危险的行为，毕竟这是pb service，但pb请求却是未定义的。这种服务在升级到新版本rpc时会遇到障碍，因为brpc早不允许这种行为。为了帮助这种服务升级，r34953后brpc允许用户经过一些设置后不把http body自动转化为pb（从而可自行处理），方法如下：

```c++
brpc::ServiceOptions svc_opt;
svc_opt.ownership = ...;
svc_opt.restful_mappings = ...;
svc_opt.allow_http_body_to_pb = false; //关闭http body至pb的自动转化
server.AddService(service, svc_opt);
```

如此设置后service收到http请求后不会尝试把body转化为pb请求，所以pb请求总是未定义状态，用户得根据`cntl->request_protocol() == brpc::PROTOCOL_HTTP`来判断请求是否是http，并自行对http body进行解析。

相应地，r34953中当cntl->response_attachment()不为空时（且pb回复不为空），框架不再报错，而是直接把cntl->response_attachment()作为回复的body。这个功能和设置allow_http_body_to_pb与否无关，如果放开自由度导致过多的用户犯错，可能会有进一步的调整。

# 协议支持

server端会自动尝试其支持的协议，无需用户指定。`cntl->protocol()`可获得当前协议。server能从一个listen端口建立不同协议的连接，不需要为不同的协议使用不同的listen端口，一个连接上也可以传输多种协议的数据包（但一般不会这么做），支持的协议有：

- 百度标准协议，显示为"baidu_std"，默认启用。

- hulu-pbrpc的协议，显示为"hulu_pbrpc"，默认启动。

- http协议，显示为”http“，默认启用。

- sofa-pbrpc的协议，显示为”sofa_pbrpc“, 默认启用。

- nova协议，显示为”nova_pbrpc“, 默认不启用，开启方式：

  ```c++
  #include <brpc/policy/nova_pbrpc_protocol.h>
  ...
  ServerOptions options;
  ...
  options.nshead_service = new brpc::policy::NovaServiceAdaptor;
  ```

- public_pbrpc协议，显示为"public_pbrpc" (r32206前显示为"nshead_server")，默认不启用，开启方式：

  ```c++
  #include <brpc/policy/public_pbrpc_protocol.h>
  ...
  ServerOptions options;
  ...
  options.nshead_service = new brpc::policy::PublicPbrpcServiceAdaptor;
  ```

- nshead_mcpack协议，显示为"nshead_mcpack"，默认不启用，开启方式：

  ```c++
  #include <brpc/policy/nshead_mcpack_protocol.h>
  ...
  ServerOptions options;
  ...
  options.nshead_service = new brpc::policy::NsheadMcpackAdaptor;
  ```

  顾名思义，这个协议的数据包由nshead+mcpack构成，mcpack中不包含特殊字段。不同于用户基于NsheadService的实现，这个协议使用了mcpack2pb：任何protobuf service都可以接受这个协议的请求。由于没有传递ErrorText的字段，当发生错误时server只能关闭连接。

- ITP协议，显示为"itp"，默认不启用，使用方式见[ITP](itp.md)。

- 和UB相关的协议请阅读[实现NsheadService](nshead_service.md)。

如果你有更多的协议需求，可以联系我们。

# 设置

## 版本

Server.set_version(...)可以为server设置一个名称+版本，可通过/version内置服务访问到。名字中请包含服务名，而不是仅仅是一个版本号。

## 关闭闲置连接

如果一个连接在ServerOptions.idle_timeout_sec对应的时间内没有读取或写出数据，则被视为”闲置”而被server主动关闭，打开[-log_idle_connection_close](http://brpc.baidu.com:8765/flags/log_idle_connection_close)后关闭前会打印一条日志。默认值为-1，代表不开启。

| Name                      | Value | Description                              | Defined At          |
| ------------------------- | ----- | ---------------------------------------- | ------------------- |
| log_idle_connection_close | false | Print log when an idle connection is closed | src/brpc/socket.cpp |

## pid_file

```
默认为空。如果设置了此字段，Server启动时会创建一个同名文件，内容为进程号。
```

## 在每条日志后打印hostname

此功能只对[butil/logging.h](https://github.com/brpc/brpc/blob/master/src/butil/logging.h)中的日志宏有效。打开[-log_hostname](http://brpc.baidu.com:8765/flags/log_hostname)后每条日志后都会带本机名称，如果所有的日志需要汇总到一起进行分析，这个功能可以帮助你了解某条日志来自哪台机器。

## 打印FATAL日志后退出程序

打开[-crash_on_fatal_log](http://brpc.baidu.com:8765/flags/crash_on_fatal_log)后如果程序使用LOG(FATAL)打印了异常日志或违反了CHECK宏中的断言，那么程序会在打印日志后abort，这一般也会产生coredump文件。这个开关可在对程序的压力测试中打开，以确认程序没有进入过严重错误的分支。

> 虽然LOG(ERROR)在打印至comlog时也显示为FATAL，但那只是因为comlog没有ERROR这个级别，ERROR并不受这个选项影响，LOG(ERROR)总不会导致程序退出。一般的惯例是，ERROR表示可容忍的错误，FATAL代表不可逆转的错误。

## 最低日志级别

此功能只对[butil/logging.h](https://github.com/brpc/brpc/blob/master/src/butil/logging.h)中的日志宏有效。设置[-min_log_level](http://brpc.baidu.com:8765/flags/min_log_level)后只有**不低于**被设置日志级别的日志才会被打印，这个选项可以动态修改。设置值和日志级别的对应关系：0=INFO 1=NOTICE 2=WARNING 3=ERROR 4=FATAL

被拦住的日志产生的开销只是一次if判断，也不会评估参数(比如某个参数调用了函数，日志不打，这个函数就不会被调用），这和comlog是完全不同的。如果日志最终打印到comlog，那么还要经过comlog中的日志级别的过滤。

## 打印发送给client的错误

server的框架部分在出现错误时一般是不打日志的，因为当大量client出现错误时，可能会导致server高频打印日志，雪上加霜。但有时为了调试问题，或就是需要让server打印错误，打开参数[-log_error_text](http://brpc.baidu.com:8765/flags/log_error_text)即可。

## 限制最大消息

为了保护server和client，当server收到的request或client收到的response过大时，server或client会拒收并关闭连接。此最大尺寸由[-max_body_size](http://brpc.baidu.com:8765/flags/max_body_size)控制，单位为字节。

超过最大消息时会打印如下错误日志：

```
FATAL: 05-10 14:40:05: * 0 src/brpc/input_messenger.cpp:89] A message from 127.0.0.1:35217(protocol=baidu_std) is bigger than 67108864 bytes, the connection will be closed. Set max_body_size to allow bigger messages
```

protobuf中有[类似的限制](https://github.com/google/protobuf/blob/master/src/google/protobuf/io/coded_stream.h#L364)，在r34677之前，即使用户设置了足够大的-max_body_size，仍然有可能因为protobuf中的限制而被拒收，出错时会打印如下日志：

```
FATAL: 05-10 13:35:02: * 0 google/protobuf/io/coded_stream.cc:156] A protocol message was rejected because it was too big (more than 67108864 bytes). To increase the limit (or to disable these warnings), see CodedInputStream::SetTotalBytesLimit() in google/protobuf/io/coded_stream.h.
```

在r34677后，brpc移除了protobuf中的限制，只要-max_body_size足够大，protobuf不会再打印限制错误。此功能对protobuf的版本没有要求。

## 压缩

set_response_compress_type()设置response的压缩方式，默认不压缩。注意附件不会被压缩。HTTP body的压缩方法见[server压缩response body](http_client.md#压缩responsebody)。

支持的压缩方法有：

- brpc::CompressTypeSnappy : [snanpy压缩](http://google.github.io/snappy/)，压缩和解压显著快于其他压缩方法，但压缩率最低。
- brpc::CompressTypeGzip : [gzip压缩](http://en.wikipedia.org/wiki/Gzip)，显著慢于snappy，但压缩率高
- brpc::CompressTypeZlib : [zlib压缩](http://en.wikipedia.org/wiki/Zlib)，比gzip快10%~20%，压缩率略好于gzip，但速度仍明显慢于snappy。

更具体的性能对比见[Client-压缩](client.md#压缩).

## 附件

baidu_std和hulu_pbrpc协议支持附件，这段数据由用户自定义，不经过protobuf的序列化。站在server的角度，设置在Controller::response_attachment()的附件会被client端收到，request_attachment()则包含了client端送来的附件。附件不受压缩选项影响。

在http协议中，附件对应[message body](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html)，比如要返回的数据就设置在response_attachment()中。

## 验证client身份

如果server端要开启验证功能，需要继承实现`Authenticator`中的`VerifyCredential`接口

```c++
class Authenticator {                                                                                                                                                              
public:                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    // Implement this method to verify credential information                                                                                                                      
    // `auth_str' from `client_addr'. You can fill credential                                                                                                                      
    // context (result) into `*out_ctx' and later fetch this                                                                                                                       
    // pointer from `Controller'.                                                                                                                                                  
    // Returns 0 on success, error code otherwise                                                                                                                                  
    virtual int VerifyCredential(const std::string& auth_str,                                                                                                                      
                                 const butil::EndPoint& client_addr,                                                                                                                
                                 AuthContext* out_ctx) const = 0;                                                                                                                                                                                                                                                                                                     
}; 
 
class AuthContext {
public:
    const std::string& user() const;
    const std::string& group() const;
    const std::string& roles() const;
    const std::string& starter() const;
    bool is_service() const;
};
```

当server收到连接上的第一个包时，会尝试解析出其中的身份信息部分（如baidu_std里的auth字段、HTTP协议里的Authorization头），然后附带client地址信息一起调用`VerifyCredential`。

若返回0，表示验证成功，用户可以把验证后的信息填入`AuthContext`，后续可通过`controller->auth_context()获取，用户不需要关心controller->auth_context()的分配和释放`

否则，表示验证失败，连接会被直接关闭，client访问失败。

由于server的验证是基于连接的，`VerifyCredential`只会在每个连接建立之初调用，后续请求默认通过验证。

最后，把实现的`Authenticator`实例赋值到`ServerOptions.auth`，即开启验证功能，需要保证该实例在整个server运行周期内都有效，不能被析构。

我们为公司统一的Giano验证方案提供了默认的Authenticator实现，配合Giano的具体用法参看[Giano快速上手手册.pdf](http://wiki.baidu.com/download/attachments/37774685/Giano%E5%BF%AB%E9%80%9F%E4%B8%8A%E6%89%8B%E6%89%8B%E5%86%8C.pdf?version=1&modificationDate=1421990746000&api=v2)中的鉴权部分。

server端开启giano认证的方式：

```c++
// Create a baas::CredentialVerifier using Giano's API
baas::CredentialVerifier verifier = CREATE_MOCK_VERIFIER(baas::sdk::BAAS_OK);
 
// Create a brpc::policy::GianoAuthenticator using the verifier we just created 
// and then pass it into brpc::ServerOptions
brpc::policy::GianoAuthenticator auth(NULL, &verifier);
brpc::ServerOptions option;
option.auth = &auth;
```

## worker线程数

设置ServerOptions.num_threads即可，默认是cpu core的个数（包含超线程的）。

> ServerOptions.num_threads仅仅是提示值。

你不能认为Server就用了这么多线程，因为进程内的所有Server和Channel会共享线程资源，线程总数是所有ServerOptions.num_threads和bthread_concurrency中的最大值。Channel没有相应的选项，但可以通过--bthread_concurrency调整。比如一个程序内有两个Server，num_threads分别为24和36，bthread_concurrency为16。那么worker线程数为max(24, 36, 16) = 36。这不同于其他RPC实现中往往是加起来。

另外，brpc**不区分**io线程和worker线程。brpc知道如何编排IO和处理代码，以获得更高的并发度和线程利用率。

## 限制最大并发

“并发”在中文背景下有两种含义，一种是连接数，一种是同时在处理的请求数。有了[epoll](https://linux.die.net/man/4/epoll)之后我们就不太关心连接数了，brpc在所有上下文中提到的并发(concurrency)均指同时在处理的请求数，不是连接数。

在传统的同步rpc server中，最大并发不会超过worker线程数（上面的num_threads选项），设定worker数量一般也限制了并发。但brpc的请求运行于bthread中，M个bthread会映射至N个worker中（一般M大于N），所以同步server的并发度可能超过worker数量。另一方面，异步server虽然占用的线程较少，但有时也需要控制并发量。

brpc支持设置server级和method级的最大并发，当server或method同时处理的请求数超过并发度限制时，它会立刻给client回复ELIMIT错误，而不会调用服务回调。看到ELIMIT错误的client应尝试另一个server。在一些情况下，这个选项可以防止server出现过度排队，或用于限制server占用的资源，但在大部分情况下并无必要。

### 为什么超过最大并发要立刻给client返回错误而不是排队？

当前server达到最大并发并不意味着集群中的其他server也达到最大并发了，立刻让client获知错误，并去尝试另一台server在全局角度是更好的策略。

### 选择最大并发数

最大并发度=极限qps*平均延时([little's law](https://en.wikipedia.org/wiki/Little%27s_law))，平均延时指的是server在正常服务状态（无积压）时的延时，设置为计算结果或略大的值即可。当server的延时较为稳定时，限制最大并发的效果和限制qps是等价的。但限制最大并发实现起来比限制qps容易多了，只需要一个计数器加加减减即可，这也是大部分流控都限制并发而不是qps的原因，比如tcp中的“窗口"即是一种并发度。

### 限制server级别并发度

设置ServerOptions.max_concurrency，默认值0代表不限制。访问内置服务不受此选项限制。

r34101后调用Server.ResetMaxConcurrency()可在server启动后动态修改server级别的max_concurrency。

### 限制method级别并发度

r34591后调用server.MaxConcurrencyOf("...") = ...可设置method级别的max_concurrency。可能的设置方法有：

```c++
server.MaxConcurrencyOf("example.EchoService.Echo") = 10;
server.MaxConcurrencyOf("example.EchoService", "Echo") = 10;
server.MaxConcurrencyOf(&service, "Echo") = 10;
```

此设置一般**发生在AddService后，server启动前**。当设置失败时（比如对应的method不存在），server会启动失败同时提示用户修正MaxConcurrencyOf设置错误。

当method级别和server级别的max_concurrency都被设置时，先检查server级别的，再检查method级别的。

注意：没有service级别的max_concurrency。

## pthread模式

用户代码（客户端的done，服务器端的CallMethod）默认在栈为1M的bthread中运行。但有些用户代码无法在bthread中运行，比如：

- JNI会检查stack layout而无法在bthread中运行。
- 代码中广泛地使用pthread local传递session数据（跨越了某次RPC），短时间内无法修改。请注意，如果代码中完全不使用brpc的客户端，在bthread中运行是没有问题的，只要代码没有明确地支持bthread就会阻塞pthread，并不会产生问题。

对于这些情况，brpc提供了pthread模式，开启**-usercode_in_pthread**后，用户代码均会在pthread中运行，原先阻塞bthread的函数转而阻塞pthread。

**r33447前请勿在开启-usercode_in_pthread的代码中发起同步RPC，只要同时进行的同步RPC个数超过工作线程数就会死锁。**

打开pthread模式在性能上的注意点：

- 开启这个开关后，RPC操作都会阻塞pthread，server端一般需要设置更多的工作线程(ServerOptions.num_threads)，调度效率会略微降低。
- pthread模式下运行用户代码的仍然是bthread，只是很特殊，会直接使用pthread worker的栈。这些特殊bthread的调度方式和其他bthread是一致的，这方面性能差异很小。
- bthread端支持一个独特的功能：把当前使用的pthread worker 让给另一个bthread运行，以消除一次上下文切换。client端的实现利用了这点，从而使一次RPC过程中3次上下文切换变为了2次。在高QPS系统中，消除上下文切换可以明显改善性能和延时分布。但pthread模式不具备这个能力，在高QPS系统中性能会有一定下降。
- pthread模式中线程资源是硬限，一旦线程被打满，请求就会迅速拥塞而造成大量超时。比如下游服务大量超时后，上游服务可能由于线程大都在等待下游也被打满从而影响性能。开启pthread模式后请考虑设置ServerOptions.max_concurrency以控制server的最大并发。而在bthread模式中bthread个数是软限，对此类问题的反应会更加平滑。

打开pthread模式可以让一些产品快速尝试brpc，但我们仍然建议产品线逐渐地把代码改造为使用bthread local从而最终能关闭这个开关。

## 安全模式

如果你的服务流量来自外部（包括经过nginx等转发），你需要注意一些安全因素：

### 对外禁用内置服务

内置服务很有用，但包含了大量内部信息，不应对外暴露。有多种方式可以对外禁用内置服务：

- 设置内部端口。把ServerOptions.internal_port设为一个**仅允许内网访问**的端口。你可通过internal_port访问到内置服务，但通过对外端口(Server.Start时传入的那个)访问内置服务时将看到如下错误：

  ```
  [a27eda84bcdeef529a76f22872b78305] Not allowed to access builtin services, try ServerOptions.internal_port=... instead if you're inside Baidu's network
  ```

- 前端server指定转发路径。nginx等http server可配置URL的映射关系，比如下面的配置把访问/MyAPI的外部流量映射到`target-server的/ServiceName/MethodName`。当外部流量尝试访问内置服务，比如说/status时，将直接被nginx拒绝。
```nginx
  location /MyAPI {
      ...
      proxy_pass http://<target-server>/ServiceName/MethodName$query_string   # $query_string是nginx变量，更多变量请查询http://nginx.org/en/docs/http/ngx_http_core_module.html
      ...
  }
```
**请勿开启**-enable_dir_service和-enable_threads_service两个选项，它们虽然很方便，但会暴露服务器上的其他信息，有安全隐患。早于r30869 (1.0.106.30846)的rpc版本没有这两个选项而是默认打开了这两个服务，请升级rpc确保它们关闭。检查现有rpc服务是否打开了这两个开关：
```shell
curl -s -m 1 <HOSTNAME>:<PORT>/flags/enable_dir_service,enable_threads_service | awk '{if($3=="false"){++falsecnt}else if($3=="Value"){isrpc=1}}END{if(isrpc!=1||falsecnt==2){print "SAFE"}else{print "NOT SAFE"}}'
```
### 对返回的URL进行转义

可调用brpc::WebEscape()对url进行转义，防止恶意URI注入攻击。

### 不返回内部server地址

可以考虑对server地址做签名。比如在设置internal_port后，server返回的错误信息中的IP信息是其MD5签名，而不是明文。

## 定制/health页面

/health页面默认返回"OK"，r32162后可以定制/health页面的内容：先继承[HealthReporter](https://github.com/brpc/brpc/blob/master/src/brpc/health_reporter.h)，在其中实现生成页面的逻辑（就像实现其他http service那样），然后把实例赋给ServerOptions.health_reporter，这个实例不被server拥有，必须保证在server运行期间有效。用户在定制逻辑中可以根据业务的运行状态返回更多样的状态信息。

## 私有变量

百度内的检索程序大量地使用了[thread-local storage](https://en.wikipedia.org/wiki/Thread-local_storage) (缩写tls)，有些是为了缓存频繁访问的对象以避免反复创建，有些则是为了在全局函数间隐式地传递状态。你应当尽量避免后者，这样的函数难以测试，不设置thread-local变量甚至无法运行。brpc中有三套机制解决和thread-local相关的问题。

### session-local

session-local data与一次检索绑定，从进service回调开始，到done被调用结束。 所有的session-local data在server停止时删除。

session-local data得从server端的Controller获得， server-thread-local可以在任意函数中获得，只要这个函数直接或间接地运行在server线程中。当service是同步时，session-local和server-thread-local基本没有差别，除了前者需要Controller创建。当service是异步时，且你需要在done中访问到数据，这时只能用session-local，出了service回调后server-thread-local已经失效。

**示例用法：**

```c++
struct MySessionLocalData {
    MySessionLocalData() : x(123) {}
    int x;
};
 
class EchoServiceImpl : public example::EchoService {
public:
    ...
    void Echo(google::protobuf::RpcController* cntl_base,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) {
        ...
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // Get the session-local data which is created by ServerOptions.session_local_data_factory
        // and reused between different RPC.
        MySessionLocalData* sd = static_cast<MySessionLocalData*>(cntl->session_local_data());
        if (sd == NULL) {
            cntl->SetFailed("Require ServerOptions.session_local_data_factory to be set with a correctly implemented instance");
            return;
        }
        ...
```

**使用方法：**

设置ServerOptions.session_local_data_factory后访问Controller.session_local_data()即可获得session-local数据。若没有设置，Controller.session_local_data()总是返回NULL。若ServerOptions.reserved_session_local_data大于0，Server会在启动前就创建这么多个数据。

```c++
struct ServerOptions {
    ...
    // The factory to create/destroy data attached to each RPC session.
    // If this field is NULL, Controller::session_local_data() is always NULL.
    // NOT owned by Server and must be valid when Server is running.
    // Default: NULL
    const DataFactory* session_local_data_factory;
 
    // Prepare so many session-local data before server starts, so that calls
    // to Controller::session_local_data() get data directly rather than
    // calling session_local_data_factory->Create() at first time. Useful when
    // Create() is slow, otherwise the RPC session may be blocked by the
    // creation of data and not served within timeout.
    // Default: 0
    size_t reserved_session_local_data;
};
```

**实现session_local_data_factory**

session_local_data_factory的类型为[DataFactory](https://github.com/brpc/brpc/blob/master/src/brpc/data_factory.h)，你需要实现其中的CreateData和DestroyData。

注意：CreateData和DestroyData会被多个线程同时调用，必须线程安全。

```c++
class MySessionLocalDataFactory : public brpc::DataFactory {
public:
    void* CreateData() const {
        return new MySessionLocalData;
    }  
    void DestroyData(void* d) const {
        delete static_cast<MySessionLocalData*>(d);
    }  
};
 
int main(int argc, char* argv[]) {
    ...
    MySessionLocalDataFactory session_local_data_factory;
 
    brpc::Server server;
    brpc::ServerOptions options;
    ...
    options.session_local_data_factory = &session_local_data_factory;
    ...
```

### server-thread-local

server-thread-local与一个检索线程绑定，从进service回调开始，到出service回调结束。所有的server-thread-local data在server停止时删除。在实现上server-thread-local是一个特殊的bthread-local。

**示例用法：**

```c++
struct MyThreadLocalData {
    MyThreadLocalData() : y(0) {}
    int y;
};
 
class EchoServiceImpl : public example::EchoService {
public:
    ...
    void Echo(google::protobuf::RpcController* cntl_base,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) {
        ...
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
         
        // Get the thread-local data which is created by ServerOptions.thread_local_data_factory
        // and reused between different threads.
        // "tls" is short for "thread local storage".
        MyThreadLocalData* tls = static_cast<MyThreadLocalData*>(brpc::thread_local_data());
        if (tls == NULL) {
            cntl->SetFailed("Require ServerOptions.thread_local_data_factory "
                            "to be set with a correctly implemented instance");
            return;
        }
        ...
```

**使用方法：**

设置ServerOptions.thread_local_data_factory后访问Controller.thread_local_data()即可获得thread-local数据。若没有设置，Controller.thread_local_data()总是返回NULL。若ServerOptions.reserved_thread_local_data大于0，Server会在启动前就创建这么多个数据。

```c++
struct ServerOptions {
    ...    
    // The factory to create/destroy data attached to each searching thread
    // in server.
    // If this field is NULL, brpc::thread_local_data() is always NULL.
    // NOT owned by Server and must be valid when Server is running.
    // Default: NULL
    const DataFactory* thread_local_data_factory;
 
    // Prepare so many thread-local data before server starts, so that calls
    // to brpc::thread_local_data() get data directly rather than calling
    // thread_local_data_factory->Create() at first time. Useful when Create()
    // is slow, otherwise the RPC session may be blocked by the creation
    // of data and not served within timeout.
    // Default: 0
    size_t reserved_thread_local_data;
};
```

**实现thread_local_data_factory：**

thread_local_data_factory的类型为[DataFactory](https://github.com/brpc/brpc/blob/master/src/brpc/data_factory.h)，你需要实现其中的CreateData和DestroyData。

注意：CreateData和DestroyData会被多个线程同时调用，必须线程安全。

```c++
class MyThreadLocalDataFactory : public brpc::DataFactory {
public:
    void* CreateData() const {
        return new MyThreadLocalData;
    }  
    void DestroyData(void* d) const {
        delete static_cast<MyThreadLocalData*>(d);
    }  
};
 
int main(int argc, char* argv[]) {
    ...
    MyThreadLocalDataFactory thread_local_data_factory;
 
    brpc::Server server;
    brpc::ServerOptions options;
    ...
    options.thread_local_data_factory  = &thread_local_data_factory;
    ...
```

### bthread-local

Session-local和server-thread-local对大部分server已经够用。不过在一些情况下，我们可能需要更通用的thread-local方案。在这种情况下，你可以使用bthread_key_create, bthread_key_destroy, bthread_getspecific, bthread_setspecific等函数，它们的用法完全等同于[pthread中的函数](http://linux.die.net/man/3/pthread_key_create)。

这些函数同时支持bthread和pthread，当它们在bthread中被调用时，获得的是bthread私有变量，而当它们在pthread中被调用时，获得的是pthread私有变量。但注意，这里的“pthread私有变量”不是pthread_key_create创建的pthread-local，使用pthread_key_create创建的pthread-local是无法被bthread_getspecific访问到的，这是两个独立的体系。由于pthread与LWP是1:1的关系，由gcc的__thread，c++11的thread_local等声明的变量也可视作pthread-local，同样无法被bthread_getspecific访问到。

由于brpc会为每个请求建立一个bthread，server中的bthread-local行为特殊：当一个检索bthread退出时，它并不删除bthread-local，而是还回server的一个pool中，以被其他bthread复用。这可以避免bthread-local随着bthread的创建和退出而不停地构造和析构。这对于用户是透明的。

**在使用bthread-local前确保brpc的版本 >= 1.0.130.31109**

在那个版本之前的bthread-local没有在不同bthread间重用线程私有的存储(keytable)。由于brpc server会为每个请求创建一个bthread, bthread-local函数会频繁地创建和删除thread-local数据，性能表现不佳。之前的实现也无法在pthread中使用。

**主要接口：**

```c++
// Create a key value identifying a slot in a thread-specific data area.
// Each thread maintains a distinct thread-specific data area.
// `destructor', if non-NULL, is called with the value associated to that key
// when the key is destroyed. `destructor' is not called if the value
// associated is NULL when the key is destroyed.
// Returns 0 on success, error code otherwise.
extern int bthread_key_create(bthread_key_t* key, void (*destructor)(void* data)) __THROW;
 
// Delete a key previously returned by bthread_key_create().
// It is the responsibility of the application to free the data related to
// the deleted key in any running thread. No destructor is invoked by
// this function. Any destructor that may have been associated with key
// will no longer be called upon thread exit.
// Returns 0 on success, error code otherwise.
extern int bthread_key_delete(bthread_key_t key) __THROW;
 
// Store `data' in the thread-specific slot identified by `key'.
// bthread_setspecific() is callable from within destructor. If the application
// does so, destructors will be repeatedly called for at most
// PTHREAD_DESTRUCTOR_ITERATIONS times to clear the slots.
// NOTE: If the thread is not created by brpc server and lifetime is
// very short(doing a little thing and exit), avoid using bthread-local. The
// reason is that bthread-local always allocate keytable on first call to
// bthread_setspecific, the overhead is negligible in long-lived threads,
// but noticeable in shortly-lived threads. Threads in brpc server
// are special since they reuse keytables from a bthread_keytable_pool_t
// in the server.
// Returns 0 on success, error code otherwise.
// If the key is invalid or deleted, return EINVAL.
extern int bthread_setspecific(bthread_key_t key, void* data) __THROW;
 
// Return current value of the thread-specific slot identified by `key'.
// If bthread_setspecific() had not been called in the thread, return NULL.
// If the key is invalid or deleted, return NULL.
extern void* bthread_getspecific(bthread_key_t key) __THROW;
```

**使用步骤:**

- 创建一个bthread_key_t，它代表一个bthread私有变量。

  ```c++
  static void my_data_destructor(void* data) {
      ...
  }
   
  bthread_key_t tls_key;
   
  if (bthread_key_create(&tls_key, my_data_destructor) != 0) {
      LOG(ERROR) << "Fail to create tls_key";
      return -1;
  }
  ```

- get/set bthread私有变量。一个线程中第一次访问某个私有变量返回NULL。

  ```c++
  // in some thread ...
  MyThreadLocalData* tls = static_cast<MyThreadLocalData*>(bthread_getspecific(tls_key));
  if (tls == NULL) {  // First call to bthread_getspecific (and before any bthread_setspecific) returns NULL
      tls = new MyThreadLocalData;   // Create thread-local data on demand.
      CHECK_EQ(0, bthread_setspecific(tls_key, tls));  // set the data so that next time bthread_getspecific in the thread returns the data.
  }
  ```

- 在所有线程都不使用某个bthread_key_t后删除它。如果删除了一个仍在被使用的bthread_key_t，相关的私有变量就泄露了。

**示例代码:**

```c++
static void my_thread_local_data_deleter(void* d) {
    delete static_cast<MyThreadLocalData*>(d);
}  
 
class EchoServiceImpl : public example::EchoService {
public:
    EchoServiceImpl() {
        CHECK_EQ(0, bthread_key_create(&_tls2_key, my_thread_local_data_deleter));
    }
    ~EchoServiceImpl() {
        CHECK_EQ(0, bthread_key_delete(_tls2_key));
    };
    ...
private:
    bthread_key_t _tls2_key;
}
 
class EchoServiceImpl : public example::EchoService {
public:
    ...
    void Echo(google::protobuf::RpcController* cntl_base,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) {
        ...
        // You can create bthread-local data for your own.
        // The interfaces are similar with pthread equivalence:
        //   pthread_key_create  -> bthread_key_create
        //   pthread_key_delete  -> bthread_key_delete
        //   pthread_getspecific -> bthread_getspecific
        //   pthread_setspecific -> bthread_setspecific
        MyThreadLocalData* tls2 = static_cast<MyThreadLocalData*>(bthread_getspecific(_tls2_key));
        if (tls2 == NULL) {
            tls2 = new MyThreadLocalData;
            CHECK_EQ(0, bthread_setspecific(_tls2_key, tls2));
        }
        ...
```

# FAQ

### Q: Fail to write into fd=1865 SocketId=8905@10.208.245.43:54742@8230: Got EOF是什么意思

A: 一般是client端使用了连接池或短连接模式，在RPC超时后会关闭连接，server写回response时发现连接已经关了就报这个错。Got EOF就是指之前已经收到了EOF（对端正常关闭了连接）。client端使用单连接模式server端一般不会报这个。

### Q: Remote side of fd=9 SocketId=2@10.94.66.55:8000 was closed是什么意思

这不是错误，是常见的warning日志，表示对端关掉连接了（EOF)。这个日志有时对排查问题有帮助。r31210之后，这个日志默认被关闭了。如果需要打开，可以把参数-log_connection_close设置为true（支持[动态修改](flags.md#change-gflag-on-the-fly)）

### Q: 为什么server端线程数设了没用

brpc同一个进程中所有的server[共用线程](#worker线程数)，如果创建了多个server，最终的工作线程数是最大的那个。

### Q: 为什么client端的延时远大于server端的延时

可能是server端的工作线程不够用了，出现了排队现象。排查方法请查看[高效率排查服务卡顿](server_debugging.md)。

### Q: 程序切换到rpc之后，会出现莫名其妙的core，像堆栈被写坏

brpc的Server是运行在bthread之上，默认栈大小为1M，而pthread默认栈大小为10M，所以在pthread上正常运行的程序，在bthread上可能遇到栈不足。

解决方案：添加以下gflag，调整栈大小。第一个表示调整栈大小为10M左右，如有必要，可以更大。第二个表示每个工作线程cache的栈个数

**--stack_size_normal=10000000 --tc_stack_normal=1**

注意：不是说程序core了就意味着”栈不够大“了...只是因为这个试起来最容易，所以优先排除掉可能性。

### Q: Fail to open /proc/self/io

有些内核没这个文件，不影响服务正确性，但如下几个bvar会无法更新：
```
process_io_read_bytes_second
process_io_write_bytes_second
process_io_read_second
process_io_write_second
```
### Q: json串="[1,2,3]"没法直接转为protobuf message

不行，最外层必须是json object（大括号包围的）

# 附:Server端基本流程

![img](../images/server_side.png)
