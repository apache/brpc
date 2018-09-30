[English version](../en/http_derivatives.md)

http/h2协议的基本用法见[http_client](http_client.md)和[http_service](http_service.md)

下文中的小节名均为可填入ChannelOptions.protocol中的协议名。冒号后的内容是协议参数，用于动态选择衍生行为，但基本协议仍然是http/1.x或http/2，故这些协议在服务端只会被显示为http或h2/h2c。

# http:json, h2:json

如果pb request不为空，则会被转为json并作为http/h2请求的body，此时Controller.request_attachment()必须为空否则报错。

如果pb response不为空，则会以json解析http/h2回复的body，并转填至pb response的对应字段。

http/1.x默认是这个行为，所以"http"和"http:json"等价。

# http:proto, h2:proto

如果pb request不为空，则会把pb序列化结果作为http/h2请求的body，此时Controller.request_attachment()必须为空否则报错。

如果pb response不为空，则会以pb序列化格式解析http/h2回复的body，并填至pb response。

http/2默认是这个行为，所以"h2"和"h2:proto"等价。

# h2:grpc

[gRPC](https://github.com/grpc)的默认协议，具体格式可阅读[gRPC over HTTP2](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)

使用brpc的客户端把ChannelOptions.protocol设置为"h2:grpc"一般就能连通gRPC。

使用brpc的服务端一般无需修改代码即可自动被gRPC客户端访问。

gRPC默认序列化是pb二进制格式，所以"h2:grpc"和"h2:grpc+proto"等价。

TODO: gRPC其他配置

# h2:grpc+json

这个协议相比h2:grpc就是用json序列化结果代替pb序列化结果。gRPC未必直接支持这个格式，如grpc-go用户可参考[这里](https://github.com/johanbrandhorst/grpc-json-example/blob/master/codec/json.go)注册相应的codec后才支持。
