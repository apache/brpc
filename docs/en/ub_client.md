brpc can access services built with ub in a variety of ways.

# ubrpc (by protobuf)

After r31687, brpc supports access to ubrpc through protobuf, without baidu-rpc-ub, and does not rely on idl-compiler. (The protobuf service can also be accessed by the ubrpc client, see [Using the service of ubrpc](nshead_service.md#Using the service of ubrpc)).

**step:**

1. Use [idl2proto](https://github.com/brpc/brpc/blob/master/tools/idl2proto) to convert idl files to proto files. The old version of idl2proto will not convert the service in idl and needs to be converted manually.

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
    
   // For multiple request or response methods in idl, a message containing all requests or responses must be created.
   // In this example, they are MultiRequests and MultiResponses.
   message MultiRequests {
     required EchoRequest req1 = 1;
     required EchoRequest req2 = 2;
   }
   message MultiResponses {
     required EchoRequest res1 = 1;
     required EchoRequest res2 = 2;
   }
    
   service EchoService {
     // Corresponding to void Echo(EchoRequest req, out EchoResponse res) in idl;
     rpc Echo(EchoRequest) returns (EchoResponse);
    
     // Corresponding to uint32_t EchoWithMultiArgs in idl(EchoRequest req1, EchoRequest req2, out EchoResponse res1, out EchoResponse res2);
     rpc EchoWithMultiArgs(MultiRequests) returns (MultiResponses);
   }
   ``

   The original echo.idl file:

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
   ``

2. Insert the following snippet to use the code generation plug-in.

   BRPC_PATH represents the path produced by brpc (including directories such as bin include), and PROTOBUF_INCLUDE_PATH represents the include path of protobuf. Note that --mcpack_out should be consistent with --cpp_out.

   ```shell
   protoc --plugin=protoc-gen-mcpack=$BRPC_PATH/bin/protoc-gen-mcpack --cpp_out=. --mcpack_out=. --proto_path=$BRPC_PATH/include --proto_path=PROTOBUF_INCLUDE_PATH
   ``

3. Use channel to initiate access.

   IDL is different from pb, which allows multiple requests. Let's first look at the situation where there is only one request, which is basically the same as ordinary pb access.

   `` c ++
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
   brpc :: controller cntl;

   request.set_message("hello world");

   stub.Echo(&cntl, &request, &response, NULL);

   if (cntl.Failed()) {
   LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
   return;
   }
   // Take the field in the response
   // [idl] void Echo(EchoRequest req, out EchoResponse res);
   // ^
   //                  response.message();
   ``

   You need to set set_idl_names for multiple requests.

   `` c ++
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
   brpc :: controller cntl;

   multi_requests.mutable_req1()->set_message("hello");
   multi_requests.mutable_req2()->set_message("world");
   cntl.set_idl_names(brpc::idl_multi_req_multi_res);
   stub.EchoWithMultiArgs(&cntl, &multi_requests, &multi_responses, NULL);

   if (cntl.Failed()) {
   LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
   return;
   }
   // Take the field in the response
   // [idl] uint32_t EchoWithMultiArgs (EchoRequest req1, EchoRequest req2,
   //        ^                         out EchoResponse res1, out EchoResponse res2);
   // | ^^
   //        |                          multi_responses.res1().message();       |
   //        |                                                 multi_responses.res2().message();
   // cntl.idl_result();
   ``

   For examples, see [example/echo_c++_ubrpc_compack](https://github.com/brpc/brpc/blob/master/example/echo_c++_ubrpc_compack/).

# ubrpc (by baidu-rpc-ub)

The server side is built by public/ubrpc, request/response uses idl file description fields, and the serialization format is compack or mcpack_v2.

**step:**

1. Rely on the public/baidu-rpc-ub module, which is an extension of brpc, and users who don’t need it will not rely on idl/mcpack/compack and other modules. baidu-rpc-ub only contains the extension code, and the new features in brpc will be automatically reflected in this module.

2. Write a proto file, which defines the service, the name is the same as that in idl, but the request type must be baidu.rpc.UBRequest, and the reply type must be baidu.rpc.UBResponse. These two types are defined in brpc/ub.proto and must be imported when used.

   ```protobuf
   import "brpc/ub.proto";              // UBRequest, UBResponse
   option cc_generic_services = true;
   // Define UB service. request/response must be UBRequest/UBResponse
   service EchoService {
       rpc Echo(baidu.rpc.UBRequest) returns (baidu.rpc.UBResponse);
   };
   ``

3. Include the baidu-rpc-ub/src path in COMAKE.

   ```python
   # Include path of brpc/ub.proto
   PROTOC(ENV.WorkRoot()+"third-64/protobuf/bin/protoc")
   PROTOFLAGS("--proto_path=" + ENV.WorkRoot() + "public/baidu-rpc-ub/src/")
   ``

4. The usage is similar to that of accessing other protocols: create a Channel, and ChannelOptions.protocol is **brpc::PROTOCOL_NSHEAD_CLIENT** or **"nshead_client"**. The request and response objects must be of the type provided by baidu-rpc-ub

   `` c ++
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

   // Construct the request/response of UBRPC, and pass in the idl structure as a module parameter. In order to construct the idl structure, a bsl::mempool needs to be passed in
   brpc::UBRPCCompackRequest<example::EchoService_Echo_params> request(&pool);
   brpc::UBRPCCompackResponse<example::EchoService_Echo_response> response(&pool);

   // set the field
   request.mutable_req()->set_message("hello world");

   // initiate RPC
   brpc :: controller cntl;
   stub.Echo(&cntl, &request, &response, NULL);

   if (cntl.Failed()) {
   LOG(ERROR) << "Fail to Echo, " << cntl.ErrorText();
   return;
   }
   // Get the field in the reply
   response.result_params().res().message();
   ...
   ``

   For specific example code, please refer to [echo_c++_compack_ubrpc](https://github.com/brpc/brpc/tree/master/example/echo_c++_compack_ubrpc/), similarly there is [echo_c++_mcpack_ubrpc](https: //github.com/brpc/brpc/tree/master/example/echo_c++_mcpack_ubrpc/).

# nshead+idl

The server side is built by public/ub, and the communication package is composed of nshead+idl::compack/idl::mcpack(v2)

Since there is no need to specify service and method, no need to write proto file, just use Channel.CallMethod to initiate RPC. The nshead in the request packet can be filled or omitted, and the frame will fill in the correct magic_num and body_len fields:

`` c ++
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

// Construct the request/response of UB, completely similar to constructing the original idl structure, passing in a bsl::mempool (variable pool)
// Pass in the type as a template, and then you can directly use the interface corresponding to the idl structure in use
brpc::UBCompackRequest<example::EchoRequest> request(&pool);
brpc::UBCompackResponse<example::EchoResponse> response(&pool);

// Set `message' field of `EchoRequest'
request.set_message("hello world");
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;

brpc :: controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL); // Assuming that the channel has passed the previous method Init successfully

// Get `message' field of `EchoResponse'
response.message();
``

For specific example code, please refer to [echo_c++_mcpack_ub](https://github.com/brpc/brpc/blob/master/example/echo_c++_mcpack_ub/), the situation of compack is similar, so I won’t repeat it

# nshead+mcpack (not generated by idl)

The server side is built by public/ub, and the communication package is composed of nshead+mcpack package, but it is not generated by idl compiler. Before RPC, you need to construct RawBuffer and pass it in, and then obtain mc_pack_t and operate by manually filling in mcpack before:

`` c ++
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

// Construct RawBuffer, RawBuffer can be reused after one RPC, similar to bsl::mempool
const int BUFSIZE = 10 * 1024 * 1024;
brpc::RawBuffer req_buf(BUFSIZE);
brpc::RawBuffer res_buf(BUFSIZE);

// Pass in RawBuffer to construct request and response
brpc::UBRawMcpackRequest request(&req_buf);
brpc::UBRawMcpackResponse response(&res_buf);

// Fetch mc_pack_t and fill in variables
mc_pack_t* req_pack = request.McpackHandle();
int ret = mc_pack_put_str(req_pack, "mystr", "hello world");
if (ret! = 0) {
LOG(FATAL) << "Failed to put string into mcpack: "
<< mc_pack_perror((long)req_pack) << (void*)req_pack;
break;
}  
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;

brpc :: controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL); // Assuming that the channel has passed the previous method Init successfully

// Get response from response buffer
const mc_pack_t* res_pack = response.McpackHandle();
mc_pack_get_str(res_pack, "mystr");
``

For specific example code, please refer to [echo_c++_raw_mcpack](https://github.com/brpc/brpc/blob/master/example/echo_c++_raw_mcpack/).

# nshead+blob

After r32897, brpc directly supports nshead+blob to access the old server (without relying on baidu-rpc-ub). For example code, please refer to [nshead_extension_c++](https://github.com/brpc/brpc/blob/master/example/nshead_extension_c++/client.cpp).

`` c ++
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


brpc :: controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);

if (cntl.Failed()) {
LOG(ERROR) << "Fail to access the server: " << cntl.ErrorText();
return -1;
}
// response.head and response.body contains nshead_t and blob respectively.
``

Or users can also use UBRawBufferRequest and UBRawBufferResponse in baidu-rpc-ub to access. For example code, please refer to [echo_c++_raw_buffer](https://github.com/brpc/brpc/blob/master/example/echo_c++_raw_buffer/).

`` c ++
brpc::Channel channel;
brpc::ChannelOptions opt;
opt.protocol = brpc::PROTOCOL_NSHEAD_CLIENT; // or "nshead_client"
if (channel.Init(..., &opt) != 0) {
LOG(ERROR) << "Fail to init channel";
return -1;
}
...

// Construct RawBuffer, RawBuffer can be reused after one RPC, similar to bsl::mempool
const int BUFSIZE = 10 * 1024 * 1024;
brpc::RawBuffer req_buf(BUFSIZE);
brpc::RawBuffer res_buf(BUFSIZE);

// Pass in RawBuffer to construct request and response
brpc::UBRawBufferRequest request(&req_buf);
brpc::UBRawBufferResponse response(&res_buf);

// Append message to `request'
request.append("hello world");
// Set fields of the request nshead struct if needed
request.mutable_nshead()->version = 99;

brpc :: controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL); // Assuming that the channel has passed the previous method Init successfully

// Process response. response.data() is the buffer, response.size() is the length.
``