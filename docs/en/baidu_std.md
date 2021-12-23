# introduce

baidu_std is a binary RPC communication protocol based on TCP protocol. It uses Protobuf as the basic data exchange format, and based on the built-in RPC Service form of Protobuf, it specifies the data exchange protocol between the communicating parties to achieve complete RPC calls.

baidu_std does not consider the cross-TCP connection.

# Basic agreement

## Serve

All RPC services are published on a certain IP address through a certain port.

A port can publish multiple services at the same time. Services are identified by name. The service name must be UpperCamelCase, consisting of uppercase and lowercase letters and numbers, and no more than 64 characters in length.

A service can contain one or more methods. Each method is identified by a name, composed of uppercase and lowercase letters, numbers, and underscores, and the length does not exceed 64 characters. Considering that different language styles are quite different, the method naming format is not mandatory here.

The four-tuple uniquely identifies an RPC method.

The parameters needed to call the method should be placed in a Protobuf message. If the method returns a result, it should also be placed in a Protobuf message. The specific definition shall be agreed upon by the communicating parties. In particular, an empty Protobuf message can be used to indicate that the request/response is empty.

## Bag

The package is the basic data exchange unit of baidu_std. Each package consists of a header and a package body. The package body is divided into three parts: metadata, data, and attachments. The specific parameters and return results are placed in the data section.

There are two types of packages: request packages and response packages. Their header format is the same, but the definition of the metadata part is different.

## Baotou

The length of the header is fixed at 12 bytes. The first four bytes are the protocol identification PRPC, the middle four bytes are a 32-bit integer, indicating the length of the packet body (excluding the 12 bytes of the header), and the last four bytes are a 32-bit integer, indicating the metadata in the packet body Packet length. Integers are expressed in network byte order.

## Metadata

Metadata is used to describe the request/response.

```protobuf
message RpcMeta {
optional RpcRequestMeta request = 1;
optional RpcResponseMeta response = 2;
optional int32 compress_type = 3;
optional int64 correlation_id = 4;
optional int32 attachment_size = 5;
optional ChunkInfo chuck_info = 6;
optional bytes authentication_data = 7;
};
```

There is only request in the request packet, and only response in the response packet. Implementations can distinguish between request packets and response packets based on the existence of domains.

| Parameters | Description |
| ------------------- | ---------------------------------------- |
| request | Request package metadata |
| response | Response package metadata |
| compress_type | See the appendix [Compression Algorithm](#compress_algorithm) for details |
| correlation_id | This field in the request packet is set by the requester and is used to uniquely identify an RPC request. The requesting party is obliged to guarantee its uniqueness, and the agreement itself does not make any checks on this. The responder needs to set the correlation_id to the same value in the corresponding response packet. |
| attachment_size | Attachment size, see [Attachment](#attachment) for details |
| chuck_info | See [Chunk mode](#chunk-mode) for details |
| authentication_data | Used to store identity authentication related information |

### Request package metadata

The metadata of the request packet mainly describes the RPC method information that needs to be called. Protobuf is as follows

```protobuf
message RpcRequestMeta {
required string service_name = 1;
required string method_name = 2;
optional int64 log_id = 3;
};
```

| Parameters | Description |
| ------------ | ---------------------------- |
| service_name | Service name, see above for constraints |
| method_name | Method name, see above for constraints |
| log_id | Used to print logs. Can be used to store BFE_LOGID. This parameter is optional. |

### Response package metadata

The metadata of the response packet is a description of the returned result. If there is any exception, the error will also be placed in the metadata. Its Protobuf is described as follows

```protobuf
message RpcResponseMeta {
optional int32 error_code = 1;
optional string error_text = 2;
};
```

| Parameters | Description |
| ---------- | ------------------------------------ |
| error_code | The error code when the error occurred, 0 means normal, non-zero means error. The specific meaning is defined by the application party. |
| error_text | Error text description |

### Extension to Metadata

Some implementations need to add their own proprietary fields in the metadata. In order to avoid conflicts and ensure the compatibility of calls between different implementations, all implementations need to apply to the Interface Specification Committee for a dedicated serial number to store their own extension fields.

Take Hulu as an example, the assigned serial number is 100. So Hulu can use this proto definition:

```protobuf
message RpcMeta {
optional RpcRequestMeta request = 1;
optional RpcResponseMeta response = 2;
optional int32 compress_type = 3;
optional int64 correlation_id = 4;
optional int32 attachment_size = 5;
optional ChunkInfo chuck_info = 6;
optional HuluMeta hulu_meta = 100;
};
message RpcRequestMeta {
required string service_name = 1;
required string method_name = 2;
optional int64 log_id = 3;
optional HuluRequestMeta hulu_request_meta = 100;
};
message RpcResponseMeta {
optional int32 error_code = 1;
optional string error_text = 2;
optional HuluResponseMeta hulu_response_meta = 100;
};
```

Because the serial number 100 is reserved for Hulu use, Hulu is free to decide whether to add these fields and what name to use. These definitions do not exist in the proto used by other implementations and will be directly ignored as the Unknown field.

The serial numbers currently assigned are as follows

| Serial Number | Implementation |
| ---- | ---- |
| 100 | Hulu |
| 101 | Sofa |

## data

Customized Protobuf Message. Used to store parameters or return results.

## Attachment

In some scenarios, RPC is required to transfer binary data, such as file upload and download, multimedia transcoding, and so on. Packing these binary data in Protobuf will increase unnecessary memory copies. Therefore, the protocol allows the use of attachments to directly transmit binary data.

Attachments are always placed at the end of the package body, immediately following the data part. When the message package needs to carry attachments, the attachment_size in RpcMeta should be set to the actual number of bytes of the attachment.

## Compression Algorithm

You can use the specified compression algorithm to compress the data part of the message packet.

| Value | Meaning |
| ---- | -------- |
| 0 | No compression |
| 1 | Using Snappy |
| 2 | Use gzip |

# HTTP interface

The service shall publish the interface to the outside through the standard HTTP protocol.

The data exchange format should use JSON by default. Content-Type uses application/json. Interfaces with special requirements are not subject to this restriction. For example, upload files can use multipart/form-data; download files can choose appropriate Content-Type according to the actual content.

The character encoding in URL and JSON all use UTF-8.

It is recommended to use the RESTful form of Web Service interface. Since RESTful is not a strict specification, this specification does not make mandatory provisions on it.