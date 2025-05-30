[中文版](../cn/thrift.md)

[thrift](https://thrift.apache.org/) is a RPC framework used widely in various environments, which was developed by Facebook and adopted by Apache later. In order to interact with thrift servers and solves issues on thread-safety, usabilities and concurrencies, brpc directly supports the thrift protocol that is used by thrift in NonBlocking mode.

Example: [example/thrift_extension_c++](https://github.com/apache/brpc/tree/master/example/thrift_extension_c++/).

Advantages compared to the official solution:
- Thread safety. No need to set up separate clients for each thread.
- Supports synchronous, asynchronous, batch synchronous, batch asynchronous, and other access methods. Combination channels such as ParallelChannel are also supported.
- Support various connection types(short, connection pool). Support timeout, backup request, cancellation, tracing, built-in services, and other benefits offered by brpc.
- Better performance.

# Compile
brpc depends on the thrift library and reuses some code generated by thrift tools. Please read official documents to find out how to write thrift files, generate code, compilations etc.

brpc does not enable thrift support or depend on the thrift lib by default. If the support is needed, compile brpc with extra --with-thrift or -DWITH_THRIFT=ON

Install thrift under Linux
Read [Official wiki](https://thrift.apache.org/docs/install/debian) to install depended libs and tools, then download thrift source code from [official site](https://thrift.apache.org/download), uncompress and compile。
```bash
wget https://downloads.apache.org/thrift/0.22.0/thrift-0.22.0.tar.gz
tar -xf thrift-0.22.0.tar.gz
cd thrift-0.22.0/
./bootstrap.sh
./configure --prefix=/usr --with-ruby=no --with-python=no --with-java=no --with-go=no --with-perl=no --with-php=no --with-csharp=no --with-erlang=no --with-lua=no --with-nodejs=no --with-rs=no --with-py3=no CXXFLAGS='-Wno-error'
make CPPFLAGS=-DFORCE_BOOST_SMART_PTR -j 4 -s
sudo make install
```

Config brpc with thrift support, then make. The compiled libbrpc.a includes extended code for thrift support and can be linked normally as in other brpc projects.
```bash
# Ubuntu
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --with-thrift
# Fedora/CentOS
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib64 --with-thrift
# Or use cmake
mkdir build && cd build && cmake ../ -DWITH_THRIFT=ON
```
Read [Getting Started](getting_started.md) for more compilation options.

# Client accesses thrift server
Steps：
- Create a Channel setting protocol to brpc::PROTOCOL_THRIFT
- Create brpc::ThriftStub
- Use native request and response to start RPC directly.

Example code:
```c++
#include <brpc/channel.h>
#include <brpc/thrift_message.h>         // Defines ThriftStub
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

brpc::ThriftStub stub(&thrift_channel);
...

// example::[EchoRequest/EchoResponse] are types generated by thrift
example::EchoRequest req;
example::EchoResponse res;
req.data = "hello";

stub.CallMethod("Echo", &cntl, &req, &res, NULL);
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to send thrift request, " << cntl.ErrorText();
    return -1;
} 
```

# Server processes thrift requests
Inherit brpc::ThriftService to implement the processing code, which may call the native handler generated by thrift to re-use existing entry directly, or read the request and set the response directly just as in other protobuf services.
```c++
class EchoServiceImpl : public brpc::ThriftService {
public:
    void ProcessThriftFramedRequest(brpc::Controller* cntl,
                                    brpc::ThriftFramedMessage* req,
                                    brpc::ThriftFramedMessage* res,
                                    google::protobuf::Closure* done) override {
        // Dispatch calls to different methods
        if (cntl->thrift_method_name() == "Echo") {
            return Echo(cntl, req->Cast<example::EchoRequest>(),
                        res->Cast<example::EchoResponse>(), done);
        } else {
            cntl->SetFailed(brpc::ENOMETHOD, "Fail to find method=%s",
                            cntl->thrift_method_name().c_str());
            done->Run();
        }
    }

    void Echo(brpc::Controller* cntl,
              const example::EchoRequest* req,
              example::EchoResponse* res,
              google::protobuf::Closure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        res->data = req->data + " (processed)";
    }
};
```

Set the implemented service to ServerOptions.thrift_service and start the service.
```c++
    brpc::Server server;
    brpc::ServerOptions options;
    options.thrift_service = new EchoServiceImpl;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;

    // Start the server.
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }
```

# Performance test for native thrift compare with brpc thrift implementaion
Test Env: 48 core  2.30GHz
## server side return string "hello" sent from client
Framework | Threads Num | QPS | Avg lantecy | CPU
---- | --- | --- | --- | ---
native thrift | 60 | 6.9w | 0.9ms | 2.8%
brpc thrift | 60 | 30w | 0.2ms | 18%

## server side return string "hello" * 1000
Framework | Threads Num | QPS | Avg lantecy | CPU
---- | --- | --- | --- | ---
native thrift | 60 | 5.2w | 1.1ms | 4.5%
brpc thrift | 60 | 19.5w | 0.3ms | 22%

## server side do some complicated math and return string "hello" * 1000
Framework | Threads Num | QPS | Avg lantecy | CPU
---- | --- | --- | --- | ---
native thrift | 60 | 1.7w | 3.5ms | 76%
brpc thrift | 60 | 2.1w | 2.9ms | 93%
