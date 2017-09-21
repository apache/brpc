http client的例子见[example/http_c++](https://github.com/brpc/brpc/blob/master/example/http_c++/http_client.cpp)

# 创建Channel

brpc::Channel可访问HTTP服务，ChannelOptions.protocol须指定为PROTOCOL_HTTP。

设定为HTTP协议后，`Channel::Init`的第一个参数可为任意合法的URL。注意：允许任意URL是为了省去用户取出host和port的麻烦，`Channel::Init`只用其中的host及port，其他部分都会丢弃。

```c++
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_HTTP;
if (channel.Init("www.baidu.com" /*any url*/, &options) != 0) {
     LOG(ERROR) << "Fail to initialize channel";
     return -1;
}
```

http channel也支持bns地址。

# GET

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "www.baidu.com/index.html";  // 设置为待访问的URL
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

HTTP和protobuf无关，所以除了Controller和done，CallMethod的其他参数均为NULL。如果要异步操作，最后一个参数传入done。

`cntl.response_attachment()`是回复的body，类型也是butil::IOBuf。注意IOBuf转化为std::string（通过to_string()接口）是需要分配内存并拷贝所有内容的，如果关注性能，你的处理过程应该尽量直接支持IOBuf，而不是要求连续内存。

# POST

默认的HTTP Method为GET，如果需要做POST，则需要设置。待POST的数据应置入request_attachment()，它([butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h))可以直接append std::string或char*

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // 设置为待访问的URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
cntl.request_attachment().append("{\"message\":\"hello world!\"}");
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

需要大量打印过程的body建议使用butil::IOBufBuilder，它的用法和std::ostringstream是一样的。对于有大量对象要打印的场景，IOBufBuilder会简化代码，并且效率也更高。

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // 设置为待访问的URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
butil::IOBufBuilder os;
os << "A lot of printing" << printable_objects << ...;
os.move_to(cntl.request_attachment());
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

# URL

URL的一般形式如下图：

```
// URI scheme : http://en.wikipedia.org/wiki/URI_scheme
//
//  foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=narwhal#nose
//  \_/   \_______________/ \_________/ \__/            \___/ \_/ \______________________/ \__/
//   |           |               |       |                |    |            |                |
//   |       userinfo           host    port              |    |          query          fragment
//   |    \________________________________/\_____________|____|/ \__/        \__/
// schema                 |                          |    |    |    |          |
//                    authority                      |    |    |    |          |
//                                                 path   |    |    interpretable as keys
//                                                        |    |
//        \_______________________________________________|____|/       \____/     \_____/
//                             |                          |    |          |           |
//                     hierarchical part                  |    |    interpretable as values
//                                                        |    |
//                                   interpretable as filename |
//                                                             |
//                                                             |
//                                               interpretable as extension
```

问题：Channel为什么不直接利用Init时传入的URL，而需要给uri()再设置一次？

确实，在简单使用场景下，这两者有所重复，但在复杂场景中，两者差别很大，比如：

- 访问挂在bns下的多个http server。此时Channel.Init传入的是bns节点名称，对uri()的赋值则是包含Host的完整URL（比如"www.foo.com/index.html?name=value"），BNS下所有的http server都会看到"Host: www.foo.com"；uri()也可以是只包含路径的URL，比如"/index.html?name=value"，框架会以目标server的ip和port为Host，地址为10.46.188.39:8989的http server将会看到"Host: 10.46.188.39:8989"。
- 通过http proxy访问目标server。此时Channel.Init传入的是proxy server的地址，但uri()填入的是目标server的URL。

# 常见设置

以http request为例 (对response的操作自行替换), 常见操作方式如下所示：

访问名为Foo的header
```c++
const std::string* value = cntl->http_request().GetHeader("Foo"); //不存在为NULL
```
设置名为Foo的header
```c++
cntl->http_request().SetHeader("Foo", "value");
```
访问名为Foo的query
```c++
const std::string* value = cntl->http_request().uri().GetQuery("Foo"); // 不存在为NULL
```
设置名为Foo的query
```c++
cntl->http_request().uri().SetQuery("Foo", "value");
```
设置HTTP方法 
```c++
cntl->http_request().set_method(brpc::HTTP_METHOD_POST);
```
设置url
```c++
cntl->http_request().uri() = "http://www.baidu.com";
```
设置content-type
```c++
cntl->http_request().set_content_type("text/plain");
```
访问body
```c++
butil::IOBuf& buf = cntl->request_attachment();
std::string str = cntl->request_attachment().to_string(); // 有拷贝
```
设置body
```c++
cntl->request_attachment().append("....");
butil::IOBufBuilder os; os << "....";
os.move_to(cntl->request_attachment());
```

Notes on http header:

- 根据 HTTP 协议[规定](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2)， header 的 field_name部分不区分大小写。brpc对于field_name大小写保持不变，且仍然支持大小写不敏感。
- 如果 HTTP 头中出现了相同的 field_name, 根据协议[规定](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2)，value将被合并到一起， 中间用逗号(,) 分隔， 具体value如何理解，需要用户自己确定.
- query之间用"&"分隔, key和value之间用"="分隔, value可以省略，比如key1=value1&key2&key3=value3中key2是合理的query，值为空字符串。

# 查看client发出的请求和收到的回复

打开[-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose)即可在stderr看到所有的http request和response，注意这应该只用于线下调试，而不是线上程序。 

# HTTP的错误处理

当Server返回的http status code不是2xx时，该次http访问即视为失败，client端会设置对应的ErrorCode：

- 所有错误被统一为EHTTP。如果用户发现`cntl->ErrorCode()`为EHTTP，那么可以检查`cntl->http_response().status_code()`以获得具体的http错误。同时http body会置入`cntl->response_attachment()`，用户可以把代表错误的html或json传递回来。

# 压缩request body

调用Controller::set_request_compress_type(brpc::COMPRESS_TYPE_GZIP)可将http body用gzip压缩，并设置"Content-Encoding"为"gzip"。

# 解压response body

出于通用性考虑且解压代码不复杂，brpc不会自动解压response body，用户可以自己做，方法如下：

```c++
#include <brpc/policy/gzip_compress.h>
...
const std::string* encoding = cntl->http_response().GetHeader("Content-Encoding");
if (encoding != NULL && *encoding == "gzip") {
    butil::IOBuf uncompressed;
    if (!brpc::policy::GzipDecompress(cntl->response_attachment(), &uncompressed)) {
        LOG(ERROR) << "Fail to un-gzip response body";
        return;
    }
    cntl->response_attachment().swap(uncompressed);
}
// cntl->response_attachment()中已经是解压后的数据了
```

# 持续下载

通常下载一个超长的body时，需要一直等待直到body完整才会视作RPC结束，这个过程中超长body都会存在内存中，如果body是无限长的（比如直播用的flv文件），那么内存会持续增长，直到超时。这样的http client不适合下载大文件。

brpc client支持在读取完body前就结束RPC，让用户在RPC结束后再读取持续增长的body。注意这个功能不等同于“支持http chunked mode”，brpc的http实现一直支持解析chunked mode，这里的问题是如何让用户处理超长或无限长的body，和body是否以chunked mode传输无关。

使用方法如下：

1. 首先实现ProgressiveReader，接口如下：

   ```c++
   #include <brpc/progressive_reader.h>
   ...
   class ProgressiveReader {
   public:
       // Called when one part was read.
       // Error returned is treated as *permenant* and the socket where the
       // data was read will be closed.
       // A temporary error may be handled by blocking this function, which
       // may block the HTTP parsing on the socket.
       virtual butil::Status OnReadOnePart(const void* data, size_t length) = 0;
    
       // Called when there's nothing to read anymore. The `status' is a hint for
       // why this method is called.
       // - status.ok(): the message is complete and successfully consumed.
       // - otherwise: socket was broken or OnReadOnePart() failed.
       // This method will be called once and only once. No other methods will
       // be called after. User can release the memory of this object inside.
       virtual void OnEndOfMessage(const butil::Status& status) = 0;
   };
   ```
   OnReadOnePart在每读到一段数据时被调用，OnEndOfMessage在数据结束或连接断开时调用，实现前仔细阅读注释。

2. 发起RPC前设置`cntl.response_will_be_read_progressively();`
   这告诉brpc在读取http response时只要读完header部分RPC就可以结束了。

3. RPC结束后调用`cntl.ReadProgressiveAttachmentBy(new MyProgressiveReader);`
   MyProgressiveReader就是用户实现ProgressiveReader的实例。用户可以在这个实例的OnEndOfMessage接口中删除这个实例。

# 持续上传

目前Post的数据必须是完整生成好的，不适合POST超长的body。

# 访问带认证的Server

根据Server的认证方式生成对应的auth_data，并设置为http header "Authorization"的值。比如用的是curl，那就加上选项`-H "Authorization : <auth_data>"。`
