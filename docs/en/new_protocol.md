# Multi-protocol support in the server side

brpc server supports all protocols in the same port, and it makes deployment and maintenance more convenient in most of the time. Since the format of different protocols is very different, it is hard to support all protocols in the same port unambiguously. In consider of decoupling and extensibility, it is also hard to build a multiplexer for all protocols. Thus our way is to classify all protocols into three categories and try one by one:

- First-class protocol: Special characters are marked in front of the protocol data, for example, the data of protocol [baidu_std](baidu_std.md) and hulu_pbrpc begins with 'PRPC' and 'HULU' respectively. Parser just check first four characters to know whether the protocol is matched. This class of protocol is checked first and all these protocols can share one TCP connection.
- Second-class protocol: Some complex protocols without special marked characters can only be detected after several input data are parsed. Currently only HTTP is classified into this category.
- Third-class protocol: Special characters are in the middle of the protocol data, such as the magic number of nshead protocol is the 25th-28th characters. It is complex to handle this case because without reading first 28 bytes, we cannot determine whether the protocol is nshead. If it is tried before http, http messages less than 28 bytes may not be parsed, since the parser consider it as an incomplete nshead message.

Considering that there will be only one protocol in most connections, we record the result of last selection so that it will be tried first when further data comes. It reduces the overhead of matching protocols to nearly zero for long connections. Although the process of matching protocols will be run every time for short connections, the bottleneck of short connections is not in here and this method is still fast enough. If there are lots of new protocols added into brpc in the future, we may consider some heuristic methods to match protocols.

# Multi-protocol support in the client side

Unlike the server side that protocols must be dynamically determined based on the data on the connection, the client side as the originator, naturally know their own protocol format. As long as the protocol data is sent through connection pool or short connection, which means it has exclusive usage of that connection, then the protocol can have any complex (or bad) format. Since the client will record the protocol when sending the data, it will use that recorded protocol to parse the data without any matching overhead when responses come back. There is no magic number in some protocols like memcache, redis, it is hard to distinguish them in the server side, but it has no problem in the client side.

# Support new protocols

brpc is designed to add new protocols at any time, just proceed as follows:

> The protocol that begins with nshead has unified support, just read [this](nshead_service.md).

## add ProtocolType

Add new protocol type in ProtocolType in [options.proto](https://github.com/apache/brpc/blob/master/src/brpc/options.proto). If you need to add new protocol, please contact us to add it for you to make sure there is no conflict with protocols of others.

Currently we support in ProtocolType(at the middle of 2018):
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
    // Reserve special protocol for cds-agent, which depends on FIFO right now
    PROTOCOL_CDS_AGENT = 23;           // Client side only
    PROTOCOL_ESP = 24;                 // Client side only
    PROTOCOL_THRIFT = 25;              // Server side only
}
```
## Implement Callbacks

All callbacks are defined in struct Protocol, which is defined in [protocol.h](https://github.com/apache/brpc/blob/master/src/brpc/protocol.h). Among all these callbacks, `parse` is a callback that must be implemented. Besides, `process_request` must be implemented in the server side and `serialize_request`, `pack_request`, `process_response` must be implemented in the client side.

It is difficult to implement callbacks of the protocol. These codes are not like the codes that ordinary users use which has good prompts and protections. You have to figure it out how to handle similar code in other protocols and implement your own protocol, then send it to us to do code review.

### parse

```c++
typedef ParseResult (*Parse)(butil::IOBuf* source, Socket *socket, bool read_eof, const void *arg);
```
This function is used to cut messages from source. Client side and server side must share the same parse function. The returned message will be passed to `process_request`(server side) or `process_response`(client side).

Argument: source is the binary content from remote side, socket is the corresponding connection, read_eof is true iff the connection is closed by remote, arg is a pointer to the corresponding server in server client and NULL in client side.

ParseResult could be an error or a cut message, its possible value contains:

- PARSE_ERROR_TRY_OTHERS: current protocol is not matched, the framework would try next protocol. The data in source cannot be comsumed.
- PARSE_ERROR_NOT_ENOUGH_DATA: the input data hasn't violated the current protocol yet, but the whole message cannot be detected as well. When there is new data from connection, new data will be appended to source and parse function is called again. If we can determine that data fits current protocol, the content of source can also be transferred to the internal state of protocol. For example, if source doesn't contain a whole http message, it will be consumed by http parser to avoid repeated parsing.
- PARSE_ERROR_TOO_BIG_DATA: message size is too big, the connection will be closed to protect server.
- PARSE_ERROR_NO_RESOURCE: internal error, such as resource allocation failure. Connections will be closed.
- PARSE_ERROR_ABSOLUTELY_WRONG: it is supposed to be some protocols(magic number is matched), but the format is not as expected. Connection will be closed.

### serialize_request
```c++
typedef bool (*SerializeRequest)(butil::IOBuf* request_buf,
                                 Controller* cntl,
                                 const google::protobuf::Message* request);
```
This function is used to serialize request into request_buf that client must implement. It happens before a RPC call and will only be called once. Necessary information needed by some protocols(such as http) is contained in cntl. Return true if succeed, otherwise false.

### pack_request
```c++
typedef int (*PackRequest)(butil::IOBuf* msg, 
                           uint64_t correlation_id,
                           const google::protobuf::MethodDescriptor* method,
                           Controller* controller,
                           const butil::IOBuf& request_buf,
                           const Authenticator* auth);
```
This function is used to pack request_buf into msg, which is called every time before sending messages to server(including retrying). When auth is not NULL, authentication information is also needed to be packed. Return 0 if succeed, otherwise -1.

### process_request
```c++
typedef void (*ProcessRequest)(InputMessageBase* msg_base);
```
This function is used to parse request messages in the server side that server must implement. It and parse() may run in different threads. Multiple process_request may run simultaneously. After the processing is done, msg_base->Destroy() must be called. In order to prevent forgetting calling Destroy, consider using DestroyingPtr<>.

### process_response
```c++
typedef void (*ProcessResponse)(InputMessageBase* msg);
```
This function is used to parse message response in client side that client must implement. It and parse() may run in different threads. Multiple process_request may run simultaneously. After the processing is done, msg_base->Destroy() must be called. In order to prevent forgetting calling Destroy, consider using DestroyingPtr<>.

### verify
```c++
typedef bool (*Verify)(const InputMessageBase* msg);
```
This function is used to authenticate connections, it is called when the first message is received. It is must be implemented by servers that need authentication, otherwise the function pointer can be NULL. Return true if succeed, otherwise false.

### parse_server_address
```c++
typedef bool (*ParseServerAddress)(butil::EndPoint* out, const char* server_addr_and_port);
```
This function converts server_addr_and_port(an argument of Channel.Init) to butil::EndPoint, which is optional. Some protocols may differ in the expression and understanding of server addresses.

### get_method_name
```c++
typedef const std::string& (*GetMethodName)(const google::protobuf::MethodDescriptor* method,
                                            const Controller*);
```
This function is used to customize method name, which is optional.

### supported_connection_type

Used to mark the supported connection method. If all connection methods are supported, this value should set to CONNECTION_TYPE_ALL. If connection pools and short connections are supported, this value should set to CONNECTION_TYPE_POOLED_AND_SHORT.

### name

The name of the protocol, which appears in the various configurations and displays, should be as short as possible and must be a string constant.

## Register to global

RegisterProtocol should be called to [register implemented protocol](https://github.com/apache/brpc/blob/master/src/brpc/global.cpp) to brpc, just like:

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
