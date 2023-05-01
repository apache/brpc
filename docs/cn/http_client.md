[English version](../en/http_client.md)

# 示例

[example/http_c++](https://github.com/apache/brpc/blob/master/example/http_c++/http_client.cpp)

# 关于h2

brpc把HTTP/2协议统称为"h2"，不论是否加密。然而未开启ssl的HTTP/2连接在/connections中会按官方名称h2c显示，而开启ssl的会显示为h2。

brpc中http和h2的编程接口基本没有区别。除非特殊说明，所有提到的http特性都同时对h2有效。

# 创建Channel

brpc::Channel可访问http/h2服务，ChannelOptions.protocol须指定为PROTOCOL_HTTP或PROTOCOL_H2。

设定好协议后，`Channel::Init`的第一个参数可为任意合法的URL。注意：允许任意URL是为了省去用户取出host和port的麻烦，`Channel::Init`只用其中的host及port，其他部分都会丢弃。

```c++
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_HTTP;  // or brpc::PROTOCOL_H2
if (channel.Init("www.baidu.com" /*any url*/, &options) != 0) {
     LOG(ERROR) << "Fail to initialize channel";
     return -1;
}
```

http/h2 channel也支持bns地址或其他NamingService。

# GET

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "www.baidu.com/index.html";  // 设置为待访问的URL
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

HTTP/h2和protobuf关系不大，所以除了Controller和done，CallMethod的其他参数均为NULL。如果要异步操作，最后一个参数传入done。

`cntl.response_attachment()`是回复的body，类型也是butil::IOBuf。IOBuf可通过to_string()转化为std::string，但是需要分配内存并拷贝所有内容，如果关注性能，处理过程应直接支持IOBuf，而不要求连续内存。

# POST

默认的HTTP Method为GET，可设置为POST或[更多http method](https://github.com/apache/brpc/blob/master/src/brpc/http_method.h)。待POST的数据应置入request_attachment()，它([butil::IOBuf](https://github.com/apache/brpc/blob/master/src/butil/iobuf.h))可以直接append std::string或char*。

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // 设置为待访问的URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
cntl.request_attachment().append("{\"message\":\"hello world!\"}");
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

需要大量打印过程的body建议使用butil::IOBufBuilder，它的用法和std::ostringstream是一样的。对于有大量对象要打印的场景，IOBufBuilder简化了代码，效率也可能比c-style printf更高。

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // 设置为待访问的URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
butil::IOBufBuilder os;
os << "A lot of printing" << printable_objects << ...;
os.move_to(cntl.request_attachment());
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```
# 控制HTTP版本

brpc的http行为默认是http/1.1。

http/1.0相比http/1.1缺少长连接功能，brpc client与一些古老的http server通信时可能需要按如下方法设置为1.0。
```c++
cntl.http_request().set_version(1, 0);
```

设置http版本对h2无效，但是client收到的h2 response和server收到的h2 request中的version会被设置为(2, 0)。

brpc server会自动识别HTTP版本，并相应回复，无需用户设置。

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
// scheme                 |                          |    |    |    |          |
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

在上面例子中可以看到，Channel.Init()和cntl.http_request().uri()被设置了相同的URL。为什么Channel不直接利用Init时传入的URL，而需要给uri()再设置一次？

确实，在简单使用场景下，这两者有所重复，但在复杂场景中，两者差别很大，比如：

- 访问命名服务(如BNS)下的多个http/h2 server。此时Channel.Init传入的是对该命名服务有意义的名称（如BNS中的节点名称），对uri()的赋值则是包含Host的完整URL(比如"www.foo.com/index.html?name=value")。
- 通过http/h2 proxy访问目标server。此时Channel.Init传入的是proxy server的地址，但uri()填入的是目标server的URL。

## Host字段

若用户自己填写了"host"字段(大小写不敏感)，框架不会修改。

若用户没有填且URL中包含host，比如http://www.foo.com/path，则http request中会包含"Host: www.foo.com"。

若用户没有填且URL不包含host，比如"/index.html?name=value"，但如果Channel初始化的地址scheme为http(s)且包含域名，则框架会以域名作为Host，比如"http://www.foo.com"，该http server将会看到"Host: www.foo.com"。如果地址是"http://www.foo.com:8989"，则该http server将会看到"Host: www.foo.com:8989"。

若用户没有填且URL不包含host，比如"/index.html?name=value"，如果Channel初始化的地址也不包含域名，则框架会以目标server的ip和port为Host，地址为10.46.188.39:8989的http server将会看到"Host: 10.46.188.39:8989"。

对应的字段在h2中叫":authority"。

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
设置HTTP Method
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
butil::IOBufBuilder os;
os << "....";
os.move_to(cntl->request_attachment());
```

Notes on http header:

- 根据[rfc2616](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2)，http header的field_name不区分大小写。brpc支持大小写不敏感，同时会在打印时保持用户传入的大小写。
- 若http header中出现了相同的field_name, 根据[rfc2616](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2)，value应合并到一起，用逗号(,)分隔，用户自行处理.
- query之间用"&"分隔, key和value之间用"="分隔, value可以省略，比如key1=value1&key2&key3=value3中key2是合理的query，值为空字符串。

# 查看HTTP消息

打开[-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose)即可看到所有的http/h2 request和response，注意这应该只用于线下调试，而不是线上程序。 

# HTTP错误

当Server返回的http status code不是2xx时，该次http/h2访问被视为失败，client端会把`cntl->ErrorCode()`设置为EHTTP，用户可通过`cntl->http_response().status_code()`获得具体的http错误。同时server端可以把代表错误的html或json置入`cntl->response_attachment()`作为http body传递回来。

如果Server也是brpc框架实现的服务，client端希望在http/h2失败时获取brpc Server返回的真实`ErrorCode`，而不是统一设置的`EHTTP`，则需要设置GFlag`-use_http_error_code=true`。

# 压缩request body

调用Controller::set_request_compress_type(brpc::COMPRESS_TYPE_GZIP)将尝试用gzip压缩http body。

“尝试”指的是压缩有可能不发生，条件有：

- body尺寸小于-http_body_compress_threshold指定的字节数，默认是512。这是因为gzip并不是一个很快的压缩算法，当body较小时，压缩增加的延时可能比网络传输省下的还多。

# 解压response body

出于通用性考虑brpc不会自动解压response body，解压代码并不复杂，用户可以自己做，方法如下：

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

http client往往需要等待到body下载完整才结束RPC，这个过程中body都会存在内存中，如果body超长或无限长（比如直播用的flv文件），那么内存会持续增长，直到超时。这样的http client不适合下载大文件。

brpc client支持在读取完body前就结束RPC，让用户在RPC结束后再读取持续增长的body。注意这个功能不等同于“支持http chunked mode”，brpc的http实现一直支持解析chunked mode，这里的问题是如何让用户处理超长或无限长的body，和body是否以chunked mode传输无关。

使用方法如下：

1. 首先实现ProgressiveReader，接口如下：

   ```c++
   #include <brpc/progressive_reader.h>
   ...
   class ProgressiveReader {
   public:
       // Called when one part was read.
       // Error returned is treated as *permanent* and the socket where the
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

# 发送https请求
https是http over SSL的简称，SSL并不是http特有的，而是对所有协议都有效。开启客户端SSL的一般性方法见[这里](client.md#开启ssl)。为方便使用，brpc会对https开头的uri自动开启SSL。
