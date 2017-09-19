# Multi-protocol support in the server side

brpc server supports all protocols in the same port, and it makes deployment and maintenance more convenient in most of the time. Since the format of different protocols is very different, it is hard to support all protocols in the same port unambiguously. In consider of decoupling and extensibility, it is also hard to build a multiplexer for all protocols. Thus our way is to classify all protocols into three categories and try one by one:

- First-class protocol: Special characters are marked in front of the protocol data, for example, the data of protocol [baidu_std](baidu_std.md) and hulu_pbrpc begins with 'PRPC' and 'HULU' respectively. Parser just check first four characters to know whether the protocol is matched. This class of protocol is checked first and all these protocols can share one TCP connection.
- Second-class protocol: Some complex protocols without special marked characters can only be detected after several input data are parsed. Currently only HTTP is classified into this category.
- Third-class protocol: Special characters are in the middle of the protocol data, such as the magic number of nshead protocol is the 25th-28th characters. It is complex to handle this case because without reading first 28 bytes, we cannot determine whether the protocol is nshead. If it is tried before http, http messages less than 28 bytes may not be parsed, since the parser consider it as an uncomplete nshead message.

Considering that there will be only one protocol in most connections, we record the result of last selection so that it will be tried first when further data comes. It reduces the overhead of matching protocols to nearly zero for long connections. Although the process of matching protocols will be run everytime for short connections, the bottleneck of short connections is not in here and this method is still fast enough. If there are lots of new protocols added into brpc in the future, we may consider some heuristic methods to match protocols.

# Multi-protocol support in the client side

Unlike the server side that protocols must be dynamically determined based on the data on the connection, the client side as the originator, naturally know their own protocol format. As long as the protocol data is sent through connection pool or short connection, which means it has exclusive usage of that connection, then the protocol can have any complex (or bad) format. Since the client will record the protocol when sending the data, it will use that recored protocol to parse the data without any matching overhead when responses come back. There is no magic number in some protocols like memcache, redis, it is hard to distinguish them in the server side, but it is no problem in the client side.

# Support new protocols

brpc is designed to add new protocols at any time, just proceed as follows:

> The protocol that begins with nshead has unified support, just read [this](nshead_service.md).

## add ProtocolType

Add new protocol type in ProtocolType in [options.proto](https://github.com/brpc/brpc/blob/master/src/brpc/options.proto). If you need to add new protocol, please contact us to add it for you to make sure there is no conflict with protocols of others.

Currently we support in ProtocolType(at the end of 2016):
```c++
enum ProtocolType {
    PROTOCOL_UNKNOWN = 0;
    PROTOCOL_BAIDU_STD = 1;
    PROTOCOL_STREAMING_RPC = 2;
    PROTOCOL_HULU_PBRPC = 3;
    PROTOCOL_SOFA_PBRPC = 4;
    PROTOCOL_RTMP = 5;
    PROTOCOL_HTTP = 6;
    PROTOCOL_PUBLIC_PBRPC = 7;
    PROTOCOL_NOVA_PBRPC = 8;
    PROTOCOL_NSHEAD_CLIENT = 9;        // implemented in brpc-ub
    PROTOCOL_NSHEAD = 10;
    PROTOCOL_HADOOP_RPC = 11;
    PROTOCOL_HADOOP_SERVER_RPC = 12;
    PROTOCOL_MONGO = 13;               // server side only
    PROTOCOL_UBRPC_COMPACK = 14;
    PROTOCOL_DIDX_CLIENT = 15;         // Client side only
    PROTOCOL_REDIS = 16;               // Client side only
    PROTOCOL_MEMCACHE = 17;            // Client side only
    PROTOCOL_ITP = 18;
    PROTOCOL_NSHEAD_MCPACK = 19;
    PROTOCOL_DISP_IDL = 20;            // Client side only
    PROTOCOL_ERSDA_CLIENT = 21;        // Client side only
    PROTOCOL_UBRPC_MCPACK2 = 22;       // Client side only
}
```
## Implement Callbacks

All callbacks are defined in struct Protocol, which is defined in [protocol.h](https://github.com/brpc/brpc/blob/master/src/brpc/protocol.h). Among all these callbacks, `parse` is a callback that must be implmented. Besides, `process_request` must be implemented in the server side and `serialize_request`, `pack_request`, `process_response` must be implemented in the client side.

It is difficult to implement callbacks of the protocol. These codes is not like the codes that ordinary users use which has good prompts and protections. You have to figure it out how to handle similar code in other protocols and implement your own protocol, then send it to us to do code review.

### parse

```c++
typedef ParseResult (*Parse)(butil::IOBuf* source, Socket *socket, bool read_eof, const void *arg);
```
用于把消息从source上切割下来，client端和server端使用同一个parse函数。返回的消息会被递给process_request(server端)或process_response(client端)。

参数：source是读取到的二进制内容，socket是对应的连接，read_eof为true表示连接已被对端关闭，arg在server端是对应server的指针，在client端是NULL。

ParseResult可能是错误，也可能包含一个切割下来的message，可能的值有：

- PARSE_ERROR_TRY_OTHERS ：不是这个协议，框架会尝试下一个协议。source不能被消费。
- PARSE_ERROR_NOT_ENOUGH_DATA : 到目前为止数据内容不违反协议，但不构成完整的消息。等到连接上有新数据时，新数据会被append入source并重新调用parse。如果不确定数据是否一定属于这个协议，source不应被消费，如果确定数据属于这个协议，也可以把source的内容转移到内部的状态中去。比如http协议解析中即使source不包含一个完整的http消息，它也会被http parser消费掉，以避免下一次重复解析。
- PARSE_ERROR_TOO_BIG_DATA : 消息太大，拒绝掉以保护server，连接会被关闭。
- PARSE_ERROR_NO_RESOURCE  : 内部错误，比如资源分配失败。连接会被关闭。
- PARSE_ERROR_ABSOLUTELY_WRONG  : 应该是这个协议（比如magic number匹配了），但是格式不符合预期。连接会被关闭。

### serialize_request
```c++
typedef bool (*SerializeRequest)(butil::IOBuf* request_buf,
                                 Controller* cntl,
                                 const google::protobuf::Message* request);
```
把request序列化进request_buf，client端必须实现。发生在pack_request之前，一次RPC中只会调用一次。cntl包含某些协议（比如http）需要的信息。成功返回true，否则false。

### pack_request
```c++
typedef int (*PackRequest)(butil::IOBuf* msg, 
                           uint64_t correlation_id,
                           const google::protobuf::MethodDescriptor* method,
                           Controller* controller,
                           const butil::IOBuf& request_buf,
                           const Authenticator* auth);
```
把request_buf打包入msg，每次向server发送消息前（包括重试）都会调用。当auth不为空时，需要打包认证信息。成功返回0，否则-1。

### process_request
```c++
typedef void (*ProcessRequest)(InputMessageBase* msg_base);
```
处理server端parse返回的消息，server端必须实现。可能会在和parse()不同的线程中运行。多个process_request可能同时运行。

在r34386后必须在处理结束时调用msg_base->Destroy()，为了防止漏调，考虑使用DestroyingPtr<>。

### process_response
```c++
typedef void (*ProcessResponse)(InputMessageBase* msg);
```
处理client端parse返回的消息，client端必须实现。可能会在和parse()不同的线程中运行。多个process_response可能同时运行。

在r34386后必须在处理结束时调用msg_base->Destroy()，为了防止漏调，考虑使用DestroyingPtr<>。

### verify
```c++
typedef bool (*Verify)(const InputMessageBase* msg);
```
处理连接的认证，只会对连接上的第一个消息调用，需要支持认证的server端必须实现，不需要认证或仅支持client端的协议可填NULL。成功返回true，否则false。

### parse_server_address
```c++
typedef bool (*ParseServerAddress)(butil::EndPoint* out, const char* server_addr_and_port);
```
把server_addr_and_port(Channel.Init的一个参数)转化为butil::EndPoint，可选。一些协议对server地址的表达和理解可能是不同的。

### get_method_name
```c++
typedef const std::string& (*GetMethodName)(const google::protobuf::MethodDescriptor* method,
                                            const Controller*);
```
定制method name，可选。

### supported_connection_type

标记支持的连接方式。如果支持所有连接方式，设为CONNECTION_TYPE_ALL。如果只支持连接池和短连接，设为CONNECTION_TYPE_POOLED_AND_SHORT。

### name

协议的名称，会出现在各种配置和显示中，越简短越好，必须是字符串常量。

## 注册到全局

实现好的协议要调用RegisterProtocol[注册到全局](https://github.com/brpc/brpc/blob/master/src/brpc/global.cpp)，以便brpc发现。就像这样：
```c++
Protocol http_protocol = { ParseHttpMessage,
                           SerializeHttpRequest, PackHttpRequest,
                           ProcessHttpRequest, ProcessHttpResponse,
                           VerifyHttpRequest, ParseHttpServerAddress,
                           GetHttpMethodName,
                           CONNECTION_TYPE_POOLED_AND_SHORT,
                           "http" };
if (RegisterProtocol(PROTOCOL_HTTP, http_protocol) != 0) {
    exit(1);
}
```
