
namespace cpp example

struct EchoRequest {
    1: optional string data;
    2: optional i32 need_by_proxy;
    3: optional list<i32>  nums,
}

struct ProxyRequest {
    2: optional i32 need_by_proxy;
}

struct EchoResponse {
    1: required string data;
    2: optional list<i32>  nums,
}

service EchoService {
    EchoResponse Echo(1:EchoRequest request);
}

