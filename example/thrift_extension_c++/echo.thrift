
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

