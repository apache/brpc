#ubprpc service

ub is an old RPC framework widely used in Baidu. It is inevitable that [Access ub-server](ub_client.md) or be accessed by ub-client is required when migrating ub services. There are many types of protocols used by ub, but they all use nshead as the header of the binary package. Such services are collectively referred to as **"nshead service"** in brpc.

After nshead, most of them use mcpack/compack as the serialization format. Note that this is not a "protocol". In addition to the serialization format, the "protocol" also involves the definition of various special fields. A serialization format may derive many protocols. ub does not define a standard protocol, so even if both mcpack or compack are used, the communication protocols of the product line are varied and cannot be interoperable. In view of this, we provide a set of interfaces that allow users to flexibly handle their product line agreements while enjoying a series of framework benefits such as builtin services provided by brpc.

# Use ubrpc's service

The basic form of the ubrpc protocol is nshead+compack or mcpack2, but compack or mcpack2 contains some special fields required by the RPC process.

After brpc r31687, services written in protobuf can be accessed by ubrpc client through mcpack2pb, the steps are as follows:

## Convert idl files to proto files

Use the script [idl2proto](https://github.com/brpc/brpc/blob/master/tools/idl2proto) to automatically convert idl files into proto files. Below is the converted proto file.

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
 
// For idl methods with multiple parameters, a message containing all request or response needs to be defined as the parameter of the corresponding method.
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
 
  // Corresponding to EchoWithMultiArgs(EchoRequest req1, EchoRequest req2, out EchoResponse res1, out EchoResponse res2) in idl;
  rpc EchoWithMultiArgs(MultiRequests) returns (MultiResponses);
}
```

The original echo.idl file is as follows:

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

## Run protoc as a plug-in

BRPC_PATH represents the path produced by brpc (including bin include and other directories), and PROTOBUF_INCLUDE_PATH represents the include path of protobuf. Note that --mcpack_out should be consistent with --cpp_out.

```shell
protoc --plugin=protoc-gen-mcpack=$BRPC_PATH/bin/protoc-gen-mcpack --cpp_out=. --mcpack_out=. --proto_path=$BRPC_PATH/include --proto_path=PROTOBUF_INCLUDE_PATH
```

## Implement the generated Service base class

```c ++
class EchoServiceImpl: public EchoService {
public:
 
    ...
    // Corresponding to void Echo(EchoRequest req, out EchoResponse res) in idl;
    virtual void Echo (google :: protobuf :: RpcController * cntl_base,
                      const EchoRequest* request,
                      EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // Fill in the response.
        response->set_message(request->message());
 
        // The corresponding idl method has no return value, so set_idl_result() is not required as in the following method.
        // You can see that this method is no different from other protobuf services, so this service can also be accessed by protocols other than ubrpc.
    }
 
    virtual void EchoWithMultiArgs (google :: protobuf :: RpcController * cntl_base,
                                   const MultiRequests* request,
                                   MultiResponses* response,
                                   google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // Fill in the response. Response is a message that we define that contains all idl responses.
        response->mutable_res1()->set_message(request->req1().message());
        response->mutable_res2()->set_message(request->req2().message());
 
        // Tell RPC that there are multiple requests and responses.
        cntl->set_idl_names(brpc::idl_multi_req_multi_res);
 
        // Corresponds to the return value of the idl method.
        cntl->set_idl_result(17);
    }
};
```

## Set ServerOptions.nshead_service

``` c ++
#include <brpc/ubrpc2pb_protocol.h>
...
brpc :: ServerOptions option;
option.nshead_service = new brpc::policy::UbrpcCompackAdaptor; // mcpack2用UbrpcMcpack2Adaptor
```

For example, see [example/echo_c++_ubrpc_compack](https://github.com/brpc/brpc/blob/master/example/echo_c++_ubrpc_compack/).

# Use nshead+blob service

[NsheadService](https://github.com/brpc/brpc/blob/master/src/brpc/nshead_service.h) is the base class for all processing nshead header protocols in brpc, and the implemented NsheadService instance must be assigned to ServerOptions. nshead_service can work. If it is not assigned, the default is NULL, which means that any protocol starting with nshead is not supported. This server will report an error when accessed by a packet starting with nshead. Obviously, a Server can only handle a protocol beginning with nshead. **

The interface of NsheadService is as follows, basically users only need to implement the function `ProcessNsheadRequest`.

``` c ++
// Represents an nshead request or reply.
struct NsheadMessage {
    nshead_t head;
    butyl :: IOBuf body;
};
 
// Implement this class and assign it to ServerOptions.nshead_service to let brpc handle nshead requests.
class NsheadService : public Describable {
public:
    NsheadService();
    NsheadService(const NsheadServiceOptions&);
    virtual ~NsheadService();
 
    // Implement this method to handle nshead requests. Note that this method may already be true when controller->Failed() is called.
    // The reason may be that Server.Stop() is called and is exiting (the error code is brpc::ELOGOFF)
    // Or ServerOptions.max_concurrency is triggered (the error code is brpc::ELIMIT)
    // In this case, this method should let the client know about these errors by returning a response representing the error.
    // Parameters:
    //   server      The server receiving the request.
    //   controller  Contexts of the request.
    //   request     The nshead request received.
    //   response    The nshead response that you should fill in.
    //   done        You must call done->Run() to end the processing, brpc::ClosureGuard is preferred.
    virtual void ProcessNsheadRequest(const Server& server,
                                      Controller * controller,
                                      const NsheadMessage& request,
                                      NsheadMessage* response,
                                      NsheadClosure* done) = 0;
};
```

The complete example is in [example/nshead_extension_c++](https://github.com/brpc/brpc/tree/master/example/nshead_extension_c++/).

# Use the service of nshead+mcpack/compack/idl

idl is the front end of mcpack/compack. Users only need to describe the schema in the idl file to generate some C++ structures. These structures can be packaged as mcpack/compack. If your service is still using a large number of structures generated by idl, and it is difficult to modify in the short term, and you want to use brpc to improve performance and development efficiency, you can achieve [NsheadService](https://github.com/brpc/brpc /blob/master/src/brpc/nshead_service.h), its interface accepts nshead + binary package as request, users fill in their own processing logic, and the final response is also nshead + binary package. The process is consistent with the protobuf method, but the process does not involve any protobuf serialization and deserialization. Users can freely understand the binary package after nshead, including loading mcpack/compack data packets with idl.

However, you should be fully aware of the disadvantages of such a transformation:

> **This service continues to use mcpack/compack as the serialization format, which occupies twice the bandwidth and packaging time compared to protobuf. **

To solve this problem, we provide [mcpack2pb](mcpack2pb.md), which allows protobuf to be used as the front end of mcpack/compack. As long as you write a proto file, you can parse requests in mcpack/compack and protobuf formats at the same time. Using this method, the service described by idl can be smoothly transformed to use the proto file description without modifying the upstream client (still using mcpack/compack). The services of your product line can be switched from mcpack/compack/idl to protobuf one by one, so as to enjoy the benefits of performance improvement, bandwidth saving, and new development experience. You can use src/mcpack2pb in NsheadService by yourself, or you can contact us to provide higher quality protocol support.

# Use the service of nshead+protobuf

If your protocol already uses nshead + protobuf, or you want to adapt your protocol to the protobuf format, you can use another mode: implement [NsheadPbServiceAdaptor](https://github.com/brpc/brpc/blob /master/src/brpc/nshead_pb_service_adaptor.h) (subclass of NsheadService).

Work steps:

- Call ParseNsheadMeta() to understand the nshead header, user must tell RPC which pb method to call in the callback.
- Call ParseRequestFromIOBuf() to convert the body after nshead header to pb request, then call the pb method.
- When user calls server's done to end the RPC, SerializeResponseToIOBuf() is called to convert pb response to binary data that will be appended after nshead header and sent back to client.

The advantage of this is that this service can also be accessed by other protocols that use protobuf, such as baidu_std, hulu_pbrpc, sofa_pbrpc, and so on. The main interface of NsheadPbServiceAdaptor is as follows. The complete example is [here](https://github.com/brpc/brpc/tree/master/example/nshead_pb_extension_c++/).

``` c ++
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
                                 Controller * controller,
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
                                       Controller * controller,
                                       google :: protobuf :: Message * pb_req) const = 0;
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
                                          Controller * controller,
                                          const google :: protobuf :: Message * pb_res,
                                          NsheadMessage* nshead_res) const = 0;
};
```