ub是百度内广泛使用的老RPC框架，在迁移ub服务时不可避免地需要[访问ub-server](ub_client.md)或被ub-client访问。ub使用的协议种类很多，但都以nshead作为二进制包的头部，这类服务在brpc中统称为**“nshead service”**。

nshead后大都使用mcpack/compack作为序列化格式，注意这不是“协议”。"协议"除了序列化格式，还涉及到各种特殊字段的定义，一种序列化格式可能会衍生出很多协议。ub没有定义标准协议，所以即使都使用mcpack或compack，产品线的通信协议也是五花八门，无法互通。鉴于此，我们提供了一套接口，让用户能够灵活的处理自己产品线的协议，同时享受brpc提供的builtin services等一系列框架福利。

# 使用ubrpc的服务

ubrpc协议的基本形式是nshead+compack或mcpack2，但compack或mcpack2中包含一些RPC过程需要的特殊字段。

在brpc r31687之后，用protobuf写的服务可以通过mcpack2pb被ubrpc client访问，步骤如下：

## 把idl文件转化为proto文件

使用脚本[idl2proto](https://github.com/apache/brpc/blob/master/tools/idl2proto)把idl文件自动转化为proto文件，下面是转化后的proto文件。

```protobuf
// Converted from echo.idl by brpc/tools/idl2proto
import "idl_options.proto";
option (idl_support) = true;
option cc_generic_services = true;
message EchoRequest {
  required string message = 1; 
}
message EchoResponse {
  required string message = 1; 
}
 
// 对于有多个参数的idl方法，需要定义一个包含所有request或response的消息，作为对应方法的参数。
message MultiRequests {
  required EchoRequest req1 = 1;
  required EchoRequest req2 = 2;
}
message MultiResponses {
  required EchoRequest res1 = 1;
  required EchoRequest res2 = 2;
}
 
service EchoService {
  // 对应idl中的void Echo(EchoRequest req, out EchoResponse res);
  rpc Echo(EchoRequest) returns (EchoResponse);
 
  // 对应idl中的EchoWithMultiArgs(EchoRequest req1, EchoRequest req2, out EchoResponse res1, out EchoResponse res2);
  rpc EchoWithMultiArgs(MultiRequests) returns (MultiResponses);
}
```

原先的echo.idl文件如下：

```protobuf
struct EchoRequest {
    string message;
};
 
struct EchoResponse {
    string message;
};
 
service EchoService {
    void Echo(EchoRequest req, out EchoResponse res);
    uint32_t EchoWithMultiArgs(EchoRequest req1, EchoRequest req2, out EchoResponse res1, out EchoResponse res2);
};
```

## 以插件方式运行protoc

BRPC_PATH代表brpc产出的路径（包含bin include等目录），PROTOBUF_INCLUDE_PATH代表protobuf的包含路径。注意--mcpack_out要和--cpp_out一致。

```shell
protoc --plugin=protoc-gen-mcpack=$BRPC_PATH/bin/protoc-gen-mcpack --cpp_out=. --mcpack_out=. --proto_path=$BRPC_PATH/include --proto_path=PROTOBUF_INCLUDE_PATH
```

## 实现生成的Service基类

```c++
class EchoServiceImpl : public EchoService {
public:
 
    ...
    // 对应idl中的void Echo(EchoRequest req, out EchoResponse res);
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // 填充response。
        response->set_message(request->message());
 
        // 对应的idl方法没有返回值，不需要像下面方法中那样set_idl_result()。
        // 可以看到这个方法和其他protobuf服务没有差别，所以这个服务也可以被ubrpc之外的协议访问。
    }
 
    virtual void EchoWithMultiArgs(google::protobuf::RpcController* cntl_base,
                                   const MultiRequests* request,
                                   MultiResponses* response,
                                   google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // 填充response。response是我们定义的包含所有idl response的消息。
        response->mutable_res1()->set_message(request->req1().message());
        response->mutable_res2()->set_message(request->req2().message());
 
        // 告诉RPC有多个request和response。
        cntl->set_idl_names(brpc::idl_multi_req_multi_res);
 
        // 对应idl方法的返回值。
        cntl->set_idl_result(17);
    }
};
```

## 设置ServerOptions.nshead_service

```c++
#include <brpc/ubrpc2pb_protocol.h>
...
brpc::ServerOptions option;
option.nshead_service = new brpc::policy::UbrpcCompackAdaptor; // mcpack2用UbrpcMcpack2Adaptor
```

例子见[example/echo_c++_ubrpc_compack](https://github.com/apache/brpc/blob/master/example/echo_c++_ubrpc_compack/)。

# 使用nshead+blob的服务

[NsheadService](https://github.com/apache/brpc/blob/master/src/brpc/nshead_service.h)是brpc中所有处理nshead打头协议的基类，实现好的NsheadService实例得赋值给ServerOptions.nshead_service才能发挥作用。不赋值的话，默认是NULL，代表不支持任何nshead开头的协议，这个server被nshead开头的数据包访问时会报错。明显地，**一个Server只能处理一种以nshead开头的协议。**

NsheadService的接口如下，基本上用户只需要实现`ProcessNsheadRequest`这个函数。

```c++
// 代表一个nshead请求或回复。
struct NsheadMessage {
    nshead_t head;
    butil::IOBuf body;
};
 
// 实现这个类并赋值给ServerOptions.nshead_service来让brpc处理nshead请求。
class NsheadService : public Describable {
public:
    NsheadService();
    NsheadService(const NsheadServiceOptions&);
    virtual ~NsheadService();
 
    // 实现这个方法来处理nshead请求。注意这个方法可能在调用时controller->Failed()已经为true了。
    // 原因可能是Server.Stop()被调用正在退出(错误码是brpc::ELOGOFF)
    // 或触发了ServerOptions.max_concurrency(错误码是brpc::ELIMIT)
    // 在这种情况下，这个方法应该通过返回一个代表错误的response让客户端知道这些错误。
    // Parameters:
    //   server      The server receiving the request.
    //   controller  Contexts of the request.
    //   request     The nshead request received.
    //   response    The nshead response that you should fill in.
    //   done        You must call done->Run() to end the processing, brpc::ClosureGuard is preferred.
    virtual void ProcessNsheadRequest(const Server& server,
                                      Controller* controller,
                                      const NsheadMessage& request,
                                      NsheadMessage* response,
                                      NsheadClosure* done) = 0;
};
```

完整的example在[example/nshead_extension_c++](https://github.com/apache/brpc/tree/master/example/nshead_extension_c++/)。

# 使用nshead+mcpack/compack/idl的服务

idl是mcpack/compack的前端，用户只要在idl文件中描述schema，就可以生成一些C++结构体，这些结构体可以打包为mcpack/compack。如果你的服务仍在大量地使用idl生成的结构体，且短期内难以修改，同时想要使用brpc提升性能和开发效率的话，可以实现[NsheadService](https://github.com/apache/brpc/blob/master/src/brpc/nshead_service.h)，其接口接受nshead + 二进制包为request，用户填写自己的处理逻辑，最后的response也是nshead+二进制包。流程与protobuf方法保持一致，但过程中不涉及任何protobuf的序列化和反序列化，用户可以自由地理解nshead后的二进制包，包括用idl加载mcpack/compack数据包。

不过，你应当充分意识到这么改造的坏处：

> **这个服务在继续使用mcpack/compack作为序列化格式，相比protobuf占用成倍的带宽和打包时间。**

为了解决这个问题，我们提供了[mcpack2pb](mcpack2pb.md)，允许把protobuf作为mcpack/compack的前端。你只要写一份proto文件，就可以同时解析mcpack/compack和protobuf格式的请求。使用这个方法，使用idl描述的服务的可以平滑地改造为使用proto文件描述，而不用修改上游client（仍然使用mcpack/compack）。你产品线的服务可以逐个地从mcpack/compack/idl切换为protobuf，从而享受到性能提升，带宽节省，全新开发体验等好处。你可以自行在NsheadService使用src/mcpack2pb，也可以联系我们，提供更高质量的协议支持。

# 使用nshead+protobuf的服务

如果你的协议已经使用了nshead + protobuf，或者你想把你的协议适配为protobuf格式，那可以使用另一种模式：实现[NsheadPbServiceAdaptor](https://github.com/apache/brpc/blob/master/src/brpc/nshead_pb_service_adaptor.h)（NsheadService的子类）。

工作步骤：

- Call ParseNsheadMeta() to understand the nshead header, user must tell RPC which pb method to call in the callback.
- Call ParseRequestFromIOBuf() to convert the body after nshead header to pb request, then call the pb method.
- When user calls server's done to end the RPC, SerializeResponseToIOBuf() is called to convert pb response to binary data that will be appended after nshead header and sent back to client.

这样做的好处是，这个服务还可以被其他使用protobuf的协议访问，比如baidu_std，hulu_pbrpc，sofa_pbrpc协议等等。NsheadPbServiceAdaptor的主要接口如下。完整的example在[这里](https://github.com/apache/brpc/tree/master/example/nshead_pb_extension_c++/)。

```c++
class NsheadPbServiceAdaptor : public NsheadService {
public:
    NsheadPbServiceAdaptor() : NsheadService(
        NsheadServiceOptions(false, SendNsheadPbResponseSize)) {}
    virtual ~NsheadPbServiceAdaptor() {}
 
    // Fetch meta from `nshead_req' into `meta'.
    // Params:
    //   server: where the RPC runs.
    //   nshead_req: the nshead request that server received.
    //   controller: If something goes wrong, call controller->SetFailed()
    //   meta: Set meta information into this structure. `full_method_name'
    //         must be set if controller is not SetFailed()-ed
    // FIXME: server is not needed anymore, controller->server() is same
    virtual void ParseNsheadMeta(const Server& server,
                                 const NsheadMessage& nshead_req,
                                 Controller* controller,
                                 NsheadMeta* meta) const = 0;
    // Transform `nshead_req' to `pb_req'.
    // Params:
    //   meta: was set by ParseNsheadMeta()
    //   nshead_req: the nshead request that server received.
    //   controller: you can set attachment into the controller. If something
    //               goes wrong, call controller->SetFailed()
    //   pb_req: the pb request should be set by your implementation.
    virtual void ParseRequestFromIOBuf(const NsheadMeta& meta,
                                       const NsheadMessage& nshead_req,
                                       Controller* controller,
                                       google::protobuf::Message* pb_req) const = 0;
    // Transform `pb_res' (and controller) to `nshead_res'.
    // Params:
    //   meta: was set by ParseNsheadMeta()
    //   controller: If something goes wrong, call controller->SetFailed()
    //   pb_res: the pb response that returned by pb method. [NOTE] `pb_res'
    //           can be NULL or uninitialized when RPC failed (indicated by
    //           Controller::Failed()), in which case you may put error
    //           information into `nshead_res'.
    //   nshead_res: the nshead response that will be sent back to client.
    virtual void SerializeResponseToIOBuf(const NsheadMeta& meta,
                                          Controller* controller,
                                          const google::protobuf::Message* pb_res,
                                          NsheadMessage* nshead_res) const = 0;
};
```
