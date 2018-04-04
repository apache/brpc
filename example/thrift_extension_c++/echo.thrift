
namespace cpp example

struct EchoRequest {
    1: string data;
}

struct EchoResponse {
    1: string data;
}

service EchoService {
    EchoResponse Echo(1:EchoRequest request);
}

