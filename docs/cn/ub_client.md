brpc可通过多种方式访问用ub搭建的服务。

# ubrpc (by protobuf)

r31687后，brpc支持通过protobuf访问ubrpc，不需要baidu-rpc-ub，也不依赖idl-compiler。（也可以让protobuf服务被ubrpc client访问，方法见[使用ubrpc的服务](nshead_service.md#使用ubrpc的服务)）。

**步骤：**

1. 用[idl2proto](https://github.com/brpc/brpc/blob/master/tools/idl2proto)把idl文件转化为proto文件，老版本idl2proto不会转化idl中的service，需要手动转化。

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
    
   // 对于idl中多个request或response的方法，要建立一个包含所有request或response的消息。
   // 这个例子中就是MultiRequests和MultiResponses。
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
    
     // 对应idl中的uint32_t EchoWithMultiArgs(EchoRequest req1, EchoRequest req2, out EchoResponse res1, out EchoResponse res2);
     rpc EchoWithMultiArgs(MultiRequests) returns (MultiResponses);
   }
   ```

   原先的echo.idl文件：

   ```idl
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

2. 插入如下片段以使用代码生成插件。

   BRPC_PATH代表brpc产出的路径（包含bin include等目录），PROTOBUF_INCLUDE_PATH代表protobuf的包含路径。注意--mcpack_out要和--cpp_out一致。

   ```shell
   protoc --plugin=protoc-gen-mcpack=$BRPC_PATH/bin/protoc-gen-mcpack --cpp_out=. --mcpack_out=. --proto_path=$BRPC_PATH/include --proto_path=PROTOBUF_INCLUDE_PATH
   ```

3. 用channel发起访问。

   idl不同于pb，允许有多个请求，我们先看只有一个请求的情况，和普通的pb访问基本上是一样的。

   ```c++
   #include <brpc/channel.h>
   #include "echo.pb.h"
   ...
    
   brpc::Channel channel;
   brpc::ChannelOptions opt;
   opt.protocol = brpc::PROTOCOL_UBRPC_COMPACK; // or "ubrpc_compack";
   if (channel.Init(..., &opt) != 0) {
       LOG(ERROR) << "Fail to init channel";
       return -1;
   }
   EchoService_Stub stub(&channel);
   ...
    
   EchoRequest request;
   EchoResponse response;
   brpc::Controller cntl;
    
   request.set_message("hello world");
    
   stub.Echo(&cntl, &request, &response, NULL);
        
   if (cntl.Failed()) {
       LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
       return;
   }
   // 取response中的字段
   // [idl] void Echo(EchoRequest req, out EchoResponse res);
   //                              ^ 
   //                  response.message();
   ```

   多个请求要设置一下set_idl_names。

   ```c++
   #include <brpc/channel.h>
   #include "echo.pb.h"
   ...
    
   brpc::Channel channel;
   brpc::ChannelOptions opt;
   opt.protocol = brpc::PROTOCOL_UBRPC_COMPACK; // or "ubrpc_compack";
   if (channel.Init(..., &opt) != 0) {
       LOG(ERROR) << "Fail to init channel";
       return -1;
   }
   EchoService_Stub stub(&channel);
   ...
    
   MultiRequests multi_requests;
   MultiResponses multi_responses;
   brpc::Controller cntl;
    
   multi_requests.mutable_req1()->set_message("hello");
   multi_requests.mutable_req2()->set_message("world");
   cntl.set_idl_names(brpc::idl_multi_req_multi_res);
   stub.EchoWithMultiArgs(&cntl, &multi_requests, &multi_responses, NULL);
        
   if (cntl.Failed()) {
       LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
       return;
   }
   // 取response中的字段
   // [idl] uint32_t EchoWithMultiArgs(EchoRequest req1, EchoRequest req2,
   //        ^                         out EchoResponse res1, out EchoResponse res2);
   //        |                                           ^                      ^
   //        |                          multi_responses.res1().message();       |
   //        |                                                 multi_responses.res2().message();
   // cntl.idl_result();
   ```

   例子详见[example/echo_c++_ubrpc_compack](https://github.com/brpc/brpc/blob/master/example/echo_c++_ubrpc_compack/)。

# ubrpc (by baidu-rpc-ub)

server端由public/ubrpc搭建，request/response使用idl文件描述字段，序列化格式是compack或mcpack_v2。

**步骤：**

1. 依赖public/baidu-rpc-ub模块，这个模块是brpc的扩展，不需要的用户不会依赖idl/mcpack/compack等模块。baidu-rpc-ub只包含扩展代码，brpc中的新特性会自动体现在这个模块中。

2. 编写一个proto文件，其中定义了service，名字和idl中的相同，但请求类型必须是baidu.rpc.UBRequest，回复类型必须是baidu.rpc.UBResponse。这两个类型定义在brpc/ub.proto中，使用时得import。

   ```protobuf
   import "brpc/ub.proto";              // UBRequest, UBResponse
   option cc_generic_services = true;
   // Define UB service. request/response must be UBRequest/UBResponse
   service EchoService {
       rpc Echo(baidu.rpc.UBRequest) returns (baidu.rpc.UBResponse);
   };
   ```

3. 在COMAKE包含baidu-rpc-ub/src路径。

   ```python
   # brpc/ub.proto的包含路径
   PROTOC(ENV.WorkRoot()+"third-64/protobuf/bin/protoc")
   PROTOFLAGS("--proto_path=" + ENV.WorkRoot() + "public/baidu-rpc-ub/src/")
   ```

4. 用法和访问其他协议类似：创建Channel，ChannelOptions.protocol为**brpc::PROTOCOL_NSHEAD_CLIENT**或**"nshead_client"**。request和response对象必须是baidu-rpc-ub提供的类型

   ```c++
   #include <brpc/ub_call.h>
   ...
       
   brpc::Channel channel;
   brpc::ChannelOptions opt;
   opt.protocol = brpc::PROTOCOL_NSHEAD_CLIENT; // or "nshead_client";
   if (channel.Init(..., &opt) != 0) {
       LOG(ERROR) << "Fail to init channel";
       return -1;
   }
   EchoService_Stub stub(&channel);    
   ...
    
   const int BUFSIZE = 1024 * 1024;  // 1M
   char* buf_for_mempool = new char[BUFSIZE];
   bsl::xmempool pool;
   if (pool.create(buf_for_mempool, BUFSIZE) != 0) {
       LOG(FATAL) << "Fail to create bsl::xmempool";
       return -1;
   }
    
   // 构造UBRPC的request/response，idl结构体作为模块参数传入。为了构造idl结构，需要传入一个bsl::mempool
   brpc::UBRPCCompackRequest<example::EchoService_Echo_params> request(&pool);
   brpc::UBRPCCompackResponse<example::EchoService_Echo_response> response(&pool);
    
   // 设置字段
   request.mutable_req()->set_message("hello world");
    
   // 发起RPC
   brpc::Controller cntl;
   stub.Echo(&cntl, &request, &response, NULL);
       
   if (cntl.Failed()) {
       LOG(ERROR) << "Fail to Echo, " << cntl.ErrorText();
       return;
   }
   // 取回复中的字段
   response.result_params().res().message();
   ...
   ```

   具体example代码可以参考[echo_c++_compack_ubrpc](https://github.com/brpc/brpc/tree/master/example/echo_c++_compack_ubrpc/)，类似的还有[echo_c++_mcpack_ubrpc](https://github.com/brpc/brpc/tree/master/example/echo_c++_mcpack_ubrpc/)。

# nshead+idl

server端是由public/ub搭建，通讯包组成为nshead+idl::compack/idl::mcpack(v2)

由于不需要指定service和method，无需编写proto文件，直接使用Channel.CallMethod方法发起RPC即可。请求包中的nshead可以填也可以不填，框架会补上正确的magic_num和body_len字段：

```c++
#include <brpc/ub_call.h>
...
 
brpc::Channel channel;
brpc::ChannelOptions opt;
opt.protocol = brpc::PROTOCOL_NSHEAD_CLIENT; // or "nshead_client";
 
if (channel.Init(..., &opt) != 0) {
    LOG(ERROR) << "Fail to init channel";
    return -1;
}
...
 
// 构造UB的request/response，完全类似构造原先idl结构，传入一个bsl::mempool（变量pool）
// 将类型作为模板传入，之后在使用上可以直接使用对应idl结构的接口
brpc::UBCompackRequest<example::EchoRequest> request(&pool);
brpc::UBCompackResponse<example::EchoResponse> response(&pool);
 
// Set `message' field of `EchoRequest'
request.set_message("hello world");
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;
 
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);    // 假设channel已经通过之前所述方法Init成功
 
// Get `message' field of `EchoResponse'
response.message();
```

具体example代码可以参考[echo_c++_mcpack_ub](https://github.com/brpc/brpc/blob/master/example/echo_c++_mcpack_ub/)，compack情况类似，不再赘述

# nshead+mcpack(非idl产生的)

server端是由public/ub搭建，通讯包组成为nshead+mcpack包，但不是idl编译器生成的，RPC前需要先构造RawBuffer将其传入，然后获取mc_pack_t并按之前手工填写mcpack的方式操作：

```c++
#include <brpc/ub_call.h>
...
 
brpc::Channel channel;
brpc::ChannelOptions opt;
opt.protocol = brpc::PROTOCOL_NSHEAD_CLIENT; // or "nshead_client";
if (channel.Init(..., &opt) != 0) {
    LOG(ERROR) << "Fail to init channel";
    return -1;
}
...
 
// 构造RawBuffer，一次RPC结束后RawBuffer可以复用，类似于bsl::mempool
const int BUFSIZE = 10 * 1024 * 1024;
brpc::RawBuffer req_buf(BUFSIZE);
brpc::RawBuffer res_buf(BUFSIZE);
 
// 传入RawBuffer来构造request和response
brpc::UBRawMcpackRequest request(&req_buf);
brpc::UBRawMcpackResponse response(&res_buf);
         
// Fetch mc_pack_t and fill in variables
mc_pack_t* req_pack = request.McpackHandle();
int ret = mc_pack_put_str(req_pack, "mystr", "hello world");
if (ret != 0) {
    LOG(FATAL) << "Failed to put string into mcpack: "
               << mc_pack_perror((long)req_pack) << (void*)req_pack;
    break;
}  
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;
 
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);    // 假设channel已经通过之前所述方法Init成功
 
// Get response from response buffer
const mc_pack_t* res_pack = response.McpackHandle();
mc_pack_get_str(res_pack, "mystr");
```

具体example代码可以参考[echo_c++_raw_mcpack](https://github.com/brpc/brpc/blob/master/example/echo_c++_raw_mcpack/)。

# nshead+blob

r32897后brpc直接支持用nshead+blob访问老server（而不用依赖baidu-rpc-ub）。example代码可以参考[nshead_extension_c++](https://github.com/brpc/brpc/blob/master/example/nshead_extension_c++/client.cpp)。

```c++
#include <brpc/nshead_message.h>
...
 
brpc::Channel;
brpc::ChannelOptions opt;
opt.protocol = brpc::PROTOCOL_NSHEAD; // or "nshead"
if (channel.Init(..., &opt) != 0) {
    LOG(ERROR) << "Fail to init channel";
    return -1;
} 
...
brpc::NsheadMessage request;
brpc::NsheadMessage response;
       
// Append message to `request'
request.body.append("hello world");
// Set fields of the request nshead struct if needed
request.head.version = 99;
 
 
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);
 
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access the server: " << cntl.ErrorText();
    return -1;
}
// response.head and response.body contains nshead_t and blob respectively.
```

或者用户也可以使用baidu-rpc-ub中的UBRawBufferRequest和UBRawBufferResponse来访问。example代码可以参考[echo_c++_raw_buffer](https://github.com/brpc/brpc/blob/master/example/echo_c++_raw_buffer/)。

```c++
brpc::Channel channel;
brpc::ChannelOptions opt;
opt.protocol = brpc::PROTOCOL_NSHEAD_CLIENT; // or "nshead_client"
if (channel.Init(..., &opt) != 0) {
    LOG(ERROR) << "Fail to init channel";
    return -1;
}
...
 
// 构造RawBuffer，一次RPC结束后RawBuffer可以复用，类似于bsl::mempool
const int BUFSIZE = 10 * 1024 * 1024;
brpc::RawBuffer req_buf(BUFSIZE);
brpc::RawBuffer res_buf(BUFSIZE);
 
// 传入RawBuffer来构造request和response
brpc::UBRawBufferRequest request(&req_buf);
brpc::UBRawBufferResponse response(&res_buf);
         
// Append message to `request'
request.append("hello world");
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;
 
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);    // 假设channel已经通过之前所述方法Init成功
 
// Process response. response.data() is the buffer, response.size() is the length.
```
