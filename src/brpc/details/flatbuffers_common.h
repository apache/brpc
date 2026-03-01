#ifndef BRPC_FLATBUFFERS_COMMON_H_
#define BRPC_FLATBUFFERS_COMMON_H_

#include <string>
#undef FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS
#define FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS(TypeName) \
  TypeName(const TypeName&) = delete;               \
  void operator=(const TypeName&) = delete

namespace google {
namespace protobuf {
    class Closure;
    class RpcController;
}  // namespace protobuf
}  // namespace google

namespace brpc {
namespace flatbuffers {

class Message;
class Service;
class ServiceDescriptor;
class MethodDescriptor;

// Abstract interface for an RPC channel.  An RpcChannel represents a
// communication line to a Service which can be used to call that Service's
// methods.  The Service may be running on another machine.  Normally, you
// should not call an RpcChannel directly, but instead construct a stub Service
// wrapping it.  Example:
//   RpcChannel* channel = new MyRpcChannel("remotehost.example.com:1234");
//   MyService* service = new MyService::Stub(channel);
//   service->MyMethod(request, &response, callback);
class RpcChannel {
public:
    inline RpcChannel() {}
    virtual ~RpcChannel() {}

    // Call the given method of the remote service.  The signature of this
    // procedure looks the same as Service::CallMethod(), but the requirements
    // are less strict in one important way:  the request and response objects
    // need not be of any specific class as long as their descriptors are
    // method->input_type() and method->output_type().
    virtual void FBCallMethod(const MethodDescriptor* method,
                            google::protobuf::RpcController* controller_base, const Message* request,
                            Message* response, google::protobuf::Closure* done) = 0;

private:
    FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS(RpcChannel);
};

class Service {
public:
    inline Service() {}
    virtual ~Service() {}

    // When constructing a stub, you may pass STUB_OWNS_CHANNEL as the second
    // parameter to the constructor to tell it to delete its RpcChannel when
    // destroyed.
    enum ChannelOwnership { STUB_OWNS_CHANNEL, STUB_DOESNT_OWN_CHANNEL };

    // Get the ServiceDescriptor describing this service and its methods.
    virtual const ServiceDescriptor* GetDescriptor() = 0;

    // Call a method of the service specified by MethodDescriptor.  This is
    // normally implemented as a simple switch() that calls the standard
    // definitions of the service's methods.
    //
    // Preconditions:
    // * method->service() == GetDescriptor()
    // * request and response are of the exact same classes as the objects
    //   returned by GetRequestPrototype(method) and
    //   GetResponsePrototype(method).
    // * After the call has started, the request must not be modified and the
    //   response must not be accessed at all until "done" is called.
    // * "controller" is of the correct type for the RPC implementation being
    //   used by this Service.  For stubs, the "correct type" depends on the
    //   RpcChannel which the stub is using.  Server-side Service
    //   implementations are expected to accept whatever type of RpcController
    //   the server-side RPC implementation uses.
    //
    // Postconditions:
    // * "done" will be called when the method is complete.  This may be
    //   before CallMethod() returns or it may be at some point in the future.
    // * If the RPC succeeded, "response" contains the response returned by
    //   the server.
    // * If the RPC failed, "response"'s contents are undefined.  The
    //   RpcController can be queried to determine if an error occurred and
    //   possibly to get more information about the error.
    virtual void FBCallMethod(const MethodDescriptor* method,
                            google::protobuf::RpcController* controller, const Message* request,
                            Message* response, google::protobuf::Closure* done) = 0;

 private:
    FB_BRPC_DISALLOW_EVIL_CONSTRUCTORS(Service);
};

}  // namespace flatbuffers
}  // namespace brpc

#endif  // BRPC_FLATBUFFERS_COMMON_H_