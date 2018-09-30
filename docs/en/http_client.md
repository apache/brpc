[中文版](../cn/http_client.md)

# Example

[example/http_c++](https://github.com/brpc/brpc/blob/master/example/http_c++/http_client.cpp)

# About h2

brpc names the HTTP/2 protocol to "h2", no matter encrypted or not. However HTTP/2 connections without SSL are shown on /connections with the official name "h2c", and the ones with SSL are shown as "h2".

The APIs for http and h2 in brpc are basically same. Without explicit statement, mentioned http features work for h2 as well.

# Create Channel

In order to use `brpc::Channel` to access http/h2 services, `ChannelOptions.protocol` must be set to `PROTOCOL_HTTP` or `PROTOCOL_H2`.

Once the protocol is set, the first parameter of `Channel::Init` can be any valid URL. *Note*: Only host and port inside the URL are used by Init(), other parts are discarded. Allowing full URL simply saves the user from additional parsing code.

```c++
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_HTTP;  // or brpc::PROTOCOL_H2
if (channel.Init("www.baidu.com" /*any url*/, &options) != 0) {
     LOG(ERROR) << "Fail to initialize channel";
     return -1;
}
```

http/h2 channel also support BNS address or other naming services.

# GET

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "www.baidu.com/index.html";  // Request URL
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

http/h2 does not relate to protobuf much, thus all parameters of `CallMethod` are NULL except `Controller` and `done`. Issue asynchronous RPC with non-NULL `done`.

`cntl.response_attachment()` is body of the http/h2 response and typed `butil::IOBuf`. `IOBuf` can be converted to `std::string` by `to_string()`, which needs to allocate memory and copy all data. If performance is important, the code should consider supporting `IOBuf` directly rather than requiring continuous memory.

# POST

The default HTTP Method is GET, which can be changed to POST or [other http methods](https://github.com/brpc/brpc/blob/master/src/brpc/http_method.h). The data to POST should be put into `request_attachment()`, which is typed [butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h) and able to append `std :: string` or `char *` directly.

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // Request URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
cntl.request_attachment().append("{\"message\":\"hello world!\"}");
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

If the body needs a lot of printing to build, consider using `butil::IOBufBuilder`, which has same interfaces as `std::ostringstream`, probably simpler and more efficient than c-style printf when lots of objects need to be printed.

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // Request URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
butil::IOBufBuilder os;
os << "A lot of printing" << printable_objects << ...;
os.move_to(cntl.request_attachment());
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

# Change HTTP version

brpc behaves as http 1.1 by default.

Comparing to 1.1, http 1.0 lacks of long connections(KeepAlive). To communicate brpc client with some legacy http servers, the client may be configured as follows:
```c++
cntl.http_request().set_version(1, 0);
```

Setting http version does not work for h2, but the versions in h2 responses received by client and h2 requests received by server are set to (2, 0).

brpc server recognizes http versions automically and responds accordingly without users' aid.

# URL

Genaral form of an URL:

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

As we saw in examples above, `Channel.Init()` and `cntl.http_request().uri()` both need the URL. Why does `uri()` need to be set additionally rather than using the URL to `Init()` directly?

Indeed, the settings are repeated in simple cases. But they are different in more complex scenes:

- Access multiple servers under a NamingService (for example BNS), in which case `Channel::Init` accepts a name meaningful to the NamingService(for example node names in BNS), while `uri()` is assigned with the URL.
- Access servers via http/h2 proxy, in which case `Channel::Init` takes the address of the proxy server, while `uri()` is still assigned with the URL.

## Host header

If user already sets `Host` header(case insensitive), framework makes no change.

If user does not set `Host` header and the URL has host, for example http://www.foo.com/path, the http request contains "Host: www.foo.com".

If user does not set host header and the URL does not have host as well,  for example "/index.html?name=value", framework sets `Host` header with IP and port of the target server. A http server at 10.46.188.39:8989 should see `Host: 10.46.188.39:8989`.

The header is named ":authority" in h2.

# Common usages

Take http request as an example (similar with http response), common operations are listed as follows:

Access an HTTP header named `Foo`
```c++
const std::string* value = cntl->http_request().GetHeader("Foo"); // NULL when not exist
```

Set an HTTP header named `Foo`
```c++
cntl->http_request().SetHeader("Foo", "value");
```

Access a query named `Foo`
```c++
const std::string* value = cntl->http_request().uri().GetQuery("Foo"); // NULL when not exist
```

Set a query named `Foo`
```c++
cntl->http_request().uri().SetQuery("Foo", "value");
```

Set HTTP method
```c++
cntl->http_request().set_method(brpc::HTTP_METHOD_POST);
```

Set the URL
```c++
cntl->http_request().uri() = "http://www.baidu.com";
```

Set the `content-type`
```c++
cntl->http_request().set_content_type("text/plain");
```

Get HTTP body
```c++
butil::IOBuf& buf = cntl->request_attachment();
std::string str = cntl->request_attachment().to_string(); // trigger copy underlying
```

Set HTTP body
```c++
cntl->request_attachment().append("....");
butil::IOBufBuilder os; os << "....";
os.move_to(cntl->request_attachment());
```

Notes on http header:
- field_name of the header is case-insensitive according to [rfc2616](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2). brpc supports case-insensitive field names and keeps same cases at printing as users set.
- If multiple headers have same field names, according to [rfc2616](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2), values should be merged and separated by comma (,). Users figure out how to use this kind of values by their own.
- Queries are separated by "&" and key/value in a query are separated by "=". Values can be omitted. For example, `key1=value1&key2&key3=value3` is a valid query string, in which the value for `key2` is an empty string.

# Debug HTTP messages

Turn on [-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose) so that the framework prints each http request and response. Note that this should only be used in tests or debuggings rather than online services.

# HTTP errors 

When server returns a non-2xx HTTP status code, the HTTP RPC is considered to be failed and `cntl->ErrorCode()` at client-side is set to `EHTTP`, users can check `cntl-> http_response().status_code()` for more specific HTTP error. In addition, server can put html or json describing the error into `cntl->response_attachment()` which is sent back to the client as http body.

# Compress Request Body

`Controller::set_request_compress_type(brpc::COMPRESS_TYPE_GZIP)` makes framework try to gzip the HTTP body. "try to" means the compression may not happen, because:

* Size of body is smaller than bytes specified by -http_body_compress_threshold, which is 512 by default. The reason is that gzip is not a very fast compression algorithm, when body is small, the delay caused by compression may even larger than the latency saved by faster transportation.

# Decompress Response Body

brpc does not decompress bodies of responses automatically due to universality. The decompression code is not complicated and users can do it by themselves. The code is as follows:

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
// Now cntl->response_attachment() contains the decompressed data
```

# Progressively Download

http client normally does not complete the RPC until http body has been fully downloaded. During the process http body is stored in memory. If the body is very large or infinitely large(a FLV file for live streaming), memory grows continuously until the RPC is timedout. Such http clients are not suitable for downloading very large files.

brpc client supports completing RPC before reading the full body, so that users can read http bodies progressively after RPC. Note that this feature does not mean "support for http chunked mode", actually the http implementation in brpc supports chunked mode from the very beginning. The real issue is how to let users handle very or infinitely large http bodies, which does not imply the chunked mode.

How to use:

1. Implement ProgressiveReader below:

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

   `OnReadOnePart` is called each time a piece of data is read. `OnEndOfMessage` is called at the end of data or the connection is broken. Read comments carefully before implementing.

2. Set `cntl.response_will_be_read_progressively();` before RPC to make brpc end RPC just after reading all headers.

3. Call `cntl.ReadProgressiveAttachmentBy(new MyProgressiveReader);` after RPC. `MyProgressiveReader` is an instance of user-implemented `ProgressiveReader`. User may delete the object inside `OnEndOfMessage`.

# Progressively Upload

Currently the POST data should be intact before launching the http call, thus brpc http client is still not suitable for uploading very large bodies.

# Access Servers with authentications

Generate `auth_data` according to authenticating method of the server and set it into `Authorization` header. If you're using curl, add option `-H "Authorization : <auth_data>"`.

# Send https requests
https is short for "http over SSL", SSL is not exclusive for http, but effective for all protocols. The generic method for turning on client-side SSL is [here](client.md#turn-on-ssl). brpc enables SSL automatically for URIs starting with https:// to make the usage more handy.
