Examples for Http Client: [example/http_c++](https://github.com/brpc/brpc/blob/master/example/http_c++/http_client.cpp)

# Create Channel

In order to use`brpc::Channel` to access the HTTP service, `ChannelOptions.protocol` must be specified as `PROTOCOL_HTTP`.

After setting the HTTP protocol, the first parameter of `Channel::Init` can be any valid URL. *Note*: We only use the host and port part inside the URL here in order to save the user from additional parsing work. Other parts of the URL in `Channel::Init`  will be discarded.

```c++
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_HTTP;
if (channel.Init("www.baidu.com" /*any url*/, &options) != 0) {
     LOG(ERROR) << "Fail to initialize channel";
     return -1;
}
```

http channel also support BNS address.

# GET

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "www.baidu.com/index.html";  // Request URL
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

HTTP has nothing to do with protobuf, so every parameters of  `CallMethod` are NULL except `Controller` and `done`,  which can be used to issue RPC asynchronously.

`cntl.response_attachment ()` is the response body whose type is `butil :: IOBuf`. Note that converting  `IOBuf` to `std :: string` using `to_string()` needs to allocate memory and copy all the content. As a result, if performance comes first, you should use `IOBuf` directly rather than continuous memory.

# POST

The default HTTP Method is GET. You can set the method to POST if needed, and you should append the POST data into `request_attachment()`, which ([butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h)) supports `std :: string` or `char *`

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // Request URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
cntl.request_attachment().append("{\"message\":\"hello world!\"}");
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

If you need a lot print, we suggest using `butil::IOBufBuilder`, which has the same interface as `std::ostringstream`. It's much simpler and more efficient to print lots of objects using `butil::IOBufBuilder`.

```c++
brpc::Controller cntl;
cntl.http_request().uri() = "...";  // Request URL
cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
butil::IOBufBuilder os;
os << "A lot of printing" << printable_objects << ...;
os.move_to(cntl.request_attachment());
channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
```

# URL

Below is the normal form of an URL:

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

Here's the question, why to pass URL parameter twice (via `set_uri`) instead of using the URL inside `Channel::Init()` ?

For most simple cases, it's a repeat work. But in complex scenes, they are very different in:

- Access multiple servers under a BNS node. At this time `Channel::Init` accepts the BNS node name, the value of `set_uri()` is the whole URL including Host (such as `www.foo.com/index.html?name=value`). As a result, all servers under BNS will see `Host: www.foo.com`. `set_uri()` also takes URL with the path only, such as `/index.html?name=value`. RPC framework will automatically fill the `Host` header using of the target server's ip and port. For example, http server at 10.46.188.39: 8989 will see `Host: 10.46.188.39: 8989`.
- Access the target server via http proxy. At this point `Channel::Init` takes the address of the proxy server, while `set_uri()` takes the URL of the target server.

# Basic Usage

We use `http request` as example (which is the same to `http response`). Here's some basic operations:

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

Access HTTP body

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

- The field_name of the header is case-insensitive according to [standard](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2). The framework supports that while leaving the case unchanged.
- If we have multiple headers with the same field_name, according to [standard](http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2), values will be merged together separating by comma (,). Users should figure out how to use this value according to own needs.
- Queries are separated by "&", while key and value are partitioned by "=". Value may be omitted. For example, `key1=value1&key2&key3=value3` is a valid query string, and the value for `key2` is an empty string.

# Debug for HTTP client

Turn on [-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose) so that the framework will print each request and response in stderr. Note that this should only be used for test and debug rather than online cases. 

# Error Handle for HTTP 

When server returns a non-2xx HTTP status code, the HTTP request is considered to be failed and sets the corresponding ErrorCode:

- All errors are unified as `EHTTP`. If you find `cntl->ErrorCode()` as `EHTTP`, you can check `cntl-> http_response().status_code()` to get a more specific HTTP error. In the meanwhile, HTTP body will be placed inside `cntl->response_attachment()`, you can check for error body such as html or json there.

# Compress Request Body

Calling `Controller::set_request_compress_type(brpc::COMPRESS_TYPE_GZIP)` makes framework try to gzip the HTTP body. "try to" means the compression may not happen, because:

* Size of body is smaller than bytes specified by -http_body_compress_threshold, which is 512 by default. The reason is that gzip is not a very fast compression algorithm, when body is small, the delay caused by compression may even larger than the latency saved by faster transportation.

# Decompress Response Body

For generality, brpc will not decompress response body automatically. You can do it yourself as the code won't be complicate:

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

# Continuous Download

When downloading a large file, normally the client needs to wait until the whole file has been loaded into its memory to finish this RPC. In order to leverage the problem of memory growth and RPC resourses, in brpc the client can end its RPC first and then continuously read the rest of the file. Note that it's not HTTP chunked mode as brpc always supports for parsing chunked mode body. This is the solution to allow user the deal with super large body.

Basic usage:

1. Implement ProgressiveReader:

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

   `OnReadOnePart` is called each time data is read. `OnEndOfMessage` is called each time data has finished or connection has broken. Please refer to comments before implementing.

2. Set `cntl.response_will_be_read_progressively();` before RPC so that brpc knows to end RPC after reading the header part.

3. Call `cntl.ReadProgressiveAttachmentBy(new MyProgressiveReader);` after RPC so that you can use your own implemented object `MyProgressiveReader` . You may delete this object inside `OnEndOfMessage`.

# Continuous Upload

Currently the POST data should be intact so that we do not support large POST body.

# Access Server with Authentication

Generate `auth_data` according to the server's authentication method and then set it into header `Authorization`. This is the same as using curl to add option `-H "Authorization : <auth_data>"`.