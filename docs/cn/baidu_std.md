# 介绍

baidu_std是一种基于TCP协议的二进制RPC通信协议。它以Protobuf作为基本的数据交换格式，并基于Protobuf内置的RPC Service形式，规定了通信双方之间的数据交换协议，以实现完整的RPC调用。

baidu_std不考虑跨TCP连接的情况。

# 基本协议

## 服务

所有RPC服务都在某个IP地址上通过某个端口发布。

一个端口可以同时发布多个服务。服务以名字标识。服务名必须是UpperCamelCase，由大小写字母和数字组成，长度不超过64个字符。

一个服务可以包含一个或多个方法。每个方法以名字标识，由大小写字母、数字和下划线组成，长度不超过64个字符。考虑到不同语言风格差异较大，这里对方法命名格式不做强制规定。

四元组唯一地标识了一个RPC方法。

调用方法所需参数应放在一个Protobuf消息内。如果方法有返回结果，也同样应放在一个Protobuf消息内。具体定义由通信双方自行约定。特别地，可以使用空的Protobuf消息来表示请求/响应为空的情况。

## 包

包是baidu_std的基本数据交换单位。每个包由包头和包体组成，其中包体又分为元数据、数据、附件三部分。具体的参数和返回结果放在数据部分。

包分为请求包和响应包两种。它们的包头格式一致，但元数据部分的定义不同。

## 包头

包头长度固定为12字节。前四字节为协议标识PRPC，中间四字节是一个32位整数，表示包体长度（不包括包头的12字节），最后四字节是一个32位整数，表示包体中的元数据包长度。整数均采用网络字节序表示。

## 元数据

元数据用于描述请求/响应。

```
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

请求包中只有request，响应包中只有response。实现可以根据域的存在性来区分请求包和响应包。

| 参数                  | 说明                                       |
| ------------------- | ---------------------------------------- |
| request             | 请求包元数据                                   |
| response            | 响应包元数据                                   |
| compress_type       | 详见附录[压缩算法](#compress_algorithm)          |
| correlation_id      | 请求包中的该域由请求方设置，用于唯一标识一个RPC请求。请求方有义务保证其唯一性，协议本身对此不做任何检查。响应方需要在对应的响应包里面将correlation_id设为同样的值。 |
| attachment_size     | 附件大小，详见[附件](#attachment)                 |
| chuck_info          | 详见[Chunk模式](#chunk-mode)                 |
| authentication_data | 用于存放身份认证相关信息                             |

### 请求包元数据

请求包的元数据主要描述了需要调用的RPC方法信息，Protobuf如下

```
message RpcRequestMeta {
    required string service_name = 1; 
    required string method_name = 2;
    optional int64 log_id = 3; 
};
```

| 参数           | 说明                           |
| ------------ | ---------------------------- |
| service_name | 服务名，约束见上文                    |
| method_name  | 方法名，约束见上文                    |
| log_id       | 用于打印日志。可用于存放BFE_LOGID。该参数可选。 |

### 响应包元数据

响应包的元数据是对返回结果的描述。如果出现任何异常，错误也会放在元数据中。其Protobuf描述如下

```
message RpcResponseMeta { 
    optional int32 error_code = 1; 
    optional string error_text = 2; 
};
```

| 参数         | 说明                                   |
| ---------- | ------------------------------------ |
| error_code | 发生错误时的错误号，0表示正常，非0表示错误。具体含义由应用方自行定义。 |
| error_text | 错误的文本描述                              |

### 对元数据的扩展

某些实现需要在元数据中增加自己专有的字段。为了避免冲突，并保证不同实现之间相互调用的兼容性，所有实现都需要向接口规范委员会申请一个专用的序号用于存放自己的扩展字段。

以Hulu为例，被分配的序号为100。因此Hulu可以使用这样的proto定义：

```
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

因为只是将100这个序号保留给Hulu使用，因此Hulu可以自由决定是否添加这些字段，以及使用什么样的名字。其余实现使用的proto中不存在这些定义，会直接作为Unknown字段忽略。

当前分配的序号如下

| 序号   | 实现   |
| ---- | ---- |
| 100  | Hulu |
| 101  | Sofa |

## 数据

自定义的Protobuf Message。用于存放参数或返回结果。

## Attachment

某些场景下需要通过RPC来传递二进制数据，例如文件上传下载，多媒体转码等等。将这些二进制数据打包在Protobuf内会增加不必要的内存拷贝。因此协议允许使用附件的方式直接传送二进制数据。

附件总是放在包体的最后，紧跟数据部分。消息包需要携带附件时，应将RpcMeta中的attachment_size设为附件的实际字节数。

## 压缩算法

可以使用指定的压缩算法来压缩消息包中的数据部分。

| 值    | 含义       |
| ---- | -------- |
| 0    | 不压缩      |
| 1    | 使用Snappy |
| 2    | 使用gzip   |

# HTTP接口

服务应以标准的HTTP协议对外发布接口。

数据交换格式默认应使用JSON。Content-Type使用application/json。有特殊需求的接口不受此限制。例如上传文件可以使用multipart/form-data；下载文件可以根据实际内容选用合适的Content-Type。

URL和JSON中的字符编码一律使用UTF-8。

建议使用RESTful形式的Web Service接口。由于RESTful并非一个严格的规范，本规范对此不做强制规定。