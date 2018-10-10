[中文版](../cn/http_derivatives.md)

Basics for accessing and serving http/h2 in brpc are listed in [http_client](http_client.md) and [http_service](http_service.md).

Following section names are protocol names that can be directly set to ChannelOptions.protocol. The content after colon is parameters for the protocol to select derivative behaviors dynamically, but the base protocol is still http/1.x or http/2. As a result, these protocols are displayed at server-side as http or h2/h2c only.

# http:json, h2:json

Non-empty pb request is serialized to json and set to the body of the http/h2 request. The Controller.request_attachment() must be empty otherwise the RPC fails.

Non-empty pb response is converted from a json which is parsed from the body of the http/h2 response.

http/1.x behaves in this way by default, so "http" and "http:json" are just same.

# http:proto, h2:proto

Non-empty pb request is serialized (in pb's wire format) and set to the body of the http/h2 request. The Controller.request_attachment() must be empty otherwise the RPC fails.

Non-empty pb response is parsed from the body of the http/h2 response(in pb's wire format).

http/2 behaves in this way by default, so "h2" and "h2:proto" are just same.

# h2:grpc

Default protocol of [gRPC](https://github.com/grpc). The detailed format is described in [gRPC over HTTP2](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md).

Clients using brpc should be able to talk with gRPC after changing ChannelOptions.protocol to "h2:grpc".

Servers using brpc should be accessible by gRPC clients automatically without changing the code.

gRPC serializes message into pb wire format by default, so "h2:grpc" and "h2:grpc+proto" are just same.

TODO: Other configurations for gRPC 

# h2:grpc+json

Comparing to h2:grpc, this protocol serializes messages into json instead of pb, which may not be supported by gRPC directly. For example, grpc-go users may reference [here](https://github.com/johanbrandhorst/grpc-json-example/blob/master/codec/json.go) to register the corresponding codec and turn on the support.
