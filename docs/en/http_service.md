[中文版](../cn/http_service.md)

This document talks about ordinary htt/h2 services rather than protobuf services accessible via http/h2. 
http/h2 services in brpc have to declare interfaces with empty request and response in a .proto file. This requirement keeps all service declarations inside proto files rather than scattering in code, configurations, and proto files.

# Example

[http_server.cpp](https://github.com/brpc/brpc/blob/master/example/http_c++/http_server.cpp)

# About h2

brpc names the HTTP/2 protocol to "h2", no matter encrypted or not. However HTTP/2 connections without SSL are shown on /connections with the official name "h2c", and the ones with SSL are shown as "h2".

The APIs for http and h2 in brpc are basically same. Without explicit statement, mentioned http features work for h2 as well.

# URL types

## /ServiceName/MethodName as the prefix

Define a service named `ServiceName`(not including the package name), with a method named `MethodName` and empty request/response, the service will provide http/h2 service on `/ServiceName/MethodName` by default.

The reason that request and response can be empty is that all data are in Controller:

- Header of the http/h2 request is in Controller.http_request() and the body is in Controller.request_attachment().
- Header of the http/h2 response is in Controller.http_response() and the body is in Controller.response_attachment().

Implementation steps:

1. Add the service declaration in a proto file.

```protobuf
option cc_generic_services = true;
 
message HttpRequest { };
message HttpResponse { };
 
service HttpService {
      rpc Echo(HttpRequest) returns (HttpResponse);
};
```

2. Implement the service by inheriting the base class generated in .pb.h, which is same as protobuf services.

```c++
class HttpServiceImpl : public HttpService {
public:
    ...
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const HttpRequest* /*request*/,
                      HttpResponse* /*response*/,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
 
        // body is plain text
        cntl->http_response().set_content_type("text/plain");
       
        // Use printed query string and body as the response.
        butil::IOBufBuilder os;
        os << "queries:";
        for (brpc::URI::QueryIterator it = cntl->http_request().uri().QueryBegin();
                it != cntl->http_request().uri().QueryEnd(); ++it) {
            os << ' ' << it->first << '=' << it->second;
        }
        os << "\nbody: " << cntl->request_attachment() << '\n';
        os.move_to(cntl->response_attachment());
    }
};
```

3. After adding the implemented instance into the server, the service is accessible via following URLs (Note that the path after `/HttpService/Echo ` is filled into `cntl->http_request().unresolved_path()`, which is always normalized):

| URL                        | Protobuf Method  | cntl->http_request().uri().path() | cntl->http_request().unresolved_path() |
| -------------------------- | ---------------- | --------------------------------- | -------------------------------------- |
| /HttpService/Echo          | HttpService.Echo | "/HttpService/Echo"               | ""                                     |
| /HttpService/Echo/Foo      | HttpService.Echo | "/HttpService/Echo/Foo"           | "Foo"                                  |
| /HttpService/Echo/Foo/Bar  | HttpService.Echo | "/HttpService/Echo/Foo/Bar"       | "Foo/Bar"                              |
| /HttpService//Echo///Foo// | HttpService.Echo | "/HttpService//Echo///Foo//"      | "Foo"                                  |
| /HttpService               | No such method   |                                   |                                        |

## /ServiceName as the prefix

http/h2 services for managing resources may need this kind of URL, such as `/FileService/foobar.txt` represents `./foobar.txt` and `/FileService/app/data/boot.cfg` represents `./app/data/boot.cfg`.

Implementation steps:

1. Use `FileService` as the service name and `default_method` as the method name in the proto file.

```protobuf
option cc_generic_services = true;

message HttpRequest { };
message HttpResponse { };

service FileService {
      rpc default_method(HttpRequest) returns (HttpResponse);
}
```

2. Implement the service.

```c++
class FileServiceImpl: public FileService {
public:
    ...
    virtual void default_method(google::protobuf::RpcController* cntl_base,
                                const HttpRequest* /*request*/,
                                HttpResponse* /*response*/,
                                google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->response_attachment().append("Getting file: ");
        cntl->response_attachment().append(cntl->http_request().unresolved_path());
    }
};
```

3. After adding the implemented instance into the server, the service is accessible via following URLs (the path after `/FileService` is filled in `cntl->http_request().unresolved_path()`, which is always normalized):

| URL                             | Protobuf Method            | cntl->http_request().uri().path() | cntl->http_request().unresolved_path() |
| ------------------------------- | -------------------------- | --------------------------------- | -------------------------------------- |
| /FileService                    | FileService.default_method | "/FileService"                    | ""                                     |
| /FileService/123.txt            | FileService.default_method | "/FileService/123.txt"            | "123.txt"                              |
| /FileService/mydir/123.txt      | FileService.default_method | "/FileService/mydir/123.txt"      | "mydir/123.txt"                        |
| /FileService//mydir///123.txt// | FileService.default_method | "/FileService//mydir///123.txt//" | "mydir/123.txt"                        |

## Restful URL

brpc supports specifying a URL for each method in a service. The API is as follows:

```c++
// If `restful_mappings' is non-empty, the method in service can
// be accessed by the specified URL rather than /ServiceName/MethodName.
// Mapping rules: "PATH1 => NAME1, PATH2 => NAME2 ..."
// where `PATH' is a valid path and `NAME' is the method name.
int AddService(google::protobuf::Service* service,
               ServiceOwnership ownership,
               butil::StringPiece restful_mappings);
```

`QueueService` defined below contains several methods. If the service is added into the server normally, it's accessible via URLs like `/QueueService/start` and ` /QueueService/stop`.

```protobuf
service QueueService {
    rpc start(HttpRequest) returns (HttpResponse);
    rpc stop(HttpRequest) returns (HttpResponse);
    rpc get_stats(HttpRequest) returns (HttpResponse);
    rpc download_data(HttpRequest) returns (HttpResponse);
};
```

By specifying the 3rd parameter `restful_mappings` to `AddService`, the URL can be customized:

```c++
if (server.AddService(&queue_svc,
                      brpc::SERVER_DOESNT_OWN_SERVICE,
                      "/v1/queue/start   => start,"
                      "/v1/queue/stop    => stop,"
                      "/v1/queue/stats/* => get_stats") != 0) {
    LOG(ERROR) << "Fail to add queue_svc";
    return -1;
}
 
if (server.AddService(&queue_svc,
                      brpc::SERVER_DOESNT_OWN_SERVICE,
                      "/v1/*/start   => start,"
                      "/v1/*/stop    => stop,"
                      "*.data        => download_data") != 0) {
    LOG(ERROR) << "Fail to add queue_svc";
    return -1;
}
```

There are 3 mappings separated by comma in the 3rd parameter (which is a string spanning 3 lines) to the `AddService`. Each mapping tells brpc to call the method at right side of the arrow if the left side matches the URL. The asterisk in `/v1/queue/stats/*` matches any string.

More about mapping rules:

- Multiple paths can be mapped to a same method.
- Both http/h2 and protobuf services are supported.
- Un-mapped methods are still accessible via `/ServiceName/MethodName`. Mapped methods are **not** accessible via `/ServiceName/MethodName` anymore.
- `==>` and ` ===>` are both OK, namely extra spaces at the beginning or the end, extra slashes, extra commas at the end, are all accepted.
- Pattern `PATH` and `PATH/*` can coexist.
- Support suffix matching: characters can appear after the asterisk.
- At most one asterisk is allowed in a path.

The path after asterisk can be obtained by `cntl.http_request().unresolved_path()`, which is always normalized, namely no slashes at the beginning or the end, and no repeated slashes in the middle. For example:

![img](../images/restful_1.png)

or:

![img](../images/restful_2.png)

in which unresolved_path are both `foo/bar`. The extra slashes at the left, the right, or the middle are removed.

Note that `cntl.http_request().uri().path()` is not ensured to be normalized, which is `"//v1//queue//stats//foo///bar//////"` and `"//vars///foo////bar/////"` respectively in the above example.

The built-in service page of `/status` shows customized URLs after the methods, in form of `@URL1 @URL2` ...

![img](../images/restful_3.png)

# HTTP Parameters

## HTTP headers

HTTP headers are a series of key/value pairs, some of them are defined by the HTTP specification, while others are free to use.

Query strings are also key/value pairs. Differences between HTTP headers and query strings:

* Although operations on HTTP headers are accurately defined by the http specification, but http headers cannot be modified directly from an address bar, they are often used for passing parameters of a protocol or framework.
* Query strings is part of the URL and **often** in form of `key1=value1&key2=value2&...`, which is easy to read and modify. They're often used for passing application-level parameters. However format of query strings is not defined in HTTP spec, just a convention.

```c++
// Get value for header "User-Agent" (case insensitive)
const std::string* user_agent_str = cntl->http_request().GetHeader("User-Agent");
if (user_agent_str != NULL) {  // has the header
    LOG(TRACE) << "User-Agent is " << *user_agent_str;
}
...
 
// Add a header "Accept-encoding: gzip" (case insensitive)
cntl->http_response().SetHeader("Accept-encoding", "gzip");
// Overwrite the previous header "Accept-encoding: deflate"
cntl->http_response().SetHeader("Accept-encoding", "deflate");
// Append value to the previous header so that it becomes
// "Accept-encoding: deflate,gzip" (values separated by comma)
cntl->http_response().AppendHeader("Accept-encoding", "gzip");
```

## Content-Type

`Content-type` is a frequently used header for storing type of the HTTP body, and specially processed in brpc and accessible by `cntl->http_request().content_type()` . As a correspondence, `cntl->GetHeader("Content-Type")` returns nothing.

```c++
// Get Content-Type
if (cntl->http_request().content_type() == "application/json") {
    ...
}
...
// Set Content-Type
cntl->http_response().set_content_type("text/html");
```

If the RPC fails (`Controller` has been `SetFailed`), the framework overwrites `Content-Type` with `text/plain` and sets the response body with `Controller::ErrorText()`.

## Status Code

Status code is a special field in HTTP response to store processing result of the http request. Possible values are defined in [http_status_code.h](https://github.com/brpc/brpc/blob/master/src/brpc/http_status_code.h).

```c++
// Get Status Code
if (cntl->http_response().status_code() == brpc::HTTP_STATUS_NOT_FOUND) {
    LOG(FATAL) << "FAILED: " << controller.http_response().reason_phrase();
}
...
// Set Status code
cntl->http_response().set_status_code(brpc::HTTP_STATUS_INTERNAL_SERVER_ERROR);
cntl->http_response().set_status_code(brpc::HTTP_STATUS_INTERNAL_SERVER_ERROR, "My explanation of the error...");
```

For example, following code implements redirection with status code 302:

```c++
cntl->http_response().set_status_code(brpc::HTTP_STATUS_FOUND);
cntl->http_response().SetHeader("Location", "http://bj.bs.bae.baidu.com/family/image001(4979).jpg");
```

![img](../images/302.png)

## Query String

As mentioned in above [HTTP headers](#http-headers), query strings are interpreted in common convention, whose form is `key1=value1&key2=value2&…`. Keys without values are acceptable as well and accessible by `GetQuery` which returns an empty string. Such keys are often used as boolean flags. Full API are defined in [uri.h](https://github.com/brpc/brpc/blob/master/src/brpc/uri.h).

```c++
const std::string* time_value = cntl->http_request().uri().GetQuery("time");
if (time_value != NULL) {  // the query string is present
    LOG(TRACE) << "time = " << *time_value;
}

...
cntl->http_request().uri().SetQuery("time", "2015/1/2");
```

# Debugging

Turn on [-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose) to print contents of all http requests and responses. Note that this should only be used for debugging rather than online services.

# Compress the response body

HTTP services often compress http bodies to reduce transmission latency of web pages and speed up the presentations to end users.

Call `Controller::set_response_compress_type(brpc::COMPRESS_TYPE_GZIP)` to **try to** compress the http body with gzip. "Try to" means the compression may not happen in following conditions:

* The request does not set `Accept-encoding` or the value does not contain "gzip". For example, curl does not support compression without option `--compressed`, in which case the server always returns uncompressed results.

* Body size is less than the bytes specified by -http_body_compress_threshold (512 by default). gzip is not a very fast compression algorithm. When the body is small, the delay added by compression may be larger than the time saved by network transmission. No compression when the body is relatively small is probably a better choice.

  | Name                         | Value | Description                              | Defined At                            |
  | ---------------------------- | ----- | ---------------------------------------- | ------------------------------------- |
  | http_body_compress_threshold | 512   | Not compress http body when it's less than so many bytes. | src/brpc/policy/http_rpc_protocol.cpp |

# Decompress the request body

Due to generality, brpc does not decompress request bodies automatically, but users can do the job by themselves as follows:

```c++
#include <brpc/policy/gzip_compress.h>
...
const std::string* encoding = cntl->http_request().GetHeader("Content-Encoding");
if (encoding != NULL && *encoding == "gzip") {
    butil::IOBuf uncompressed;
    if (!brpc::policy::GzipDecompress(cntl->request_attachment(), &uncompressed)) {
        LOG(ERROR) << "Fail to un-gzip request body";
        return;
    }
    cntl->request_attachment().swap(uncompressed);
}
// cntl->request_attachment() contains the data after decompression
```

# Serve https requests
https is short for "http over SSL", SSL is not exclusive for http, but effective for all protocols. The generic method for turning on server-side SSL is [here](server.md#turn-on-ssl).

# Performance

Productions without extreme performance requirements tend to use HTTP protocol, especially mobile products. Thus we put great emphasis on implementation qualities of HTTP. To be more specific:

- Use [http parser](https://github.com/brpc/brpc/blob/master/src/brpc/details/http_parser.h) of node.js to parse http messages, which is a lightweight, well-written, and extensively used implementation.
- Use [rapidjson](https://github.com/miloyip/rapidjson) to parse json, which is a json library focuses on performance.
- In the worst case, the time complexity of parsing http requests is still O(N), where N is byte size of the request. As a contrast, parsing code that requires the http request to be complete, may cost O(N^2) time in the worst case. This feature is very helpful since many HTTP requests are large.
- Processing HTTP messages from different clients is highly concurrent, even a pretty complicated http message does not block responding other clients. It's difficult to achieve this for other RPC implementations and http servers often based on [single-threaded reactor](threading_overview.md#single-threaded-reactor).

# Progressive sending

brpc server is capable of sending large or infinite sized body, in following steps:

1. Call `Controller::CreateProgressiveAttachment()` to create a body that can be written progressively. The returned `ProgressiveAttachment` object should be managed by `intrusive_ptr`
  ```c++
  #include <brpc/progressive_attachment.h>
  ...
  butil::intrusive_ptr<brpc::ProgressiveAttachment> pa = cntl->CreateProgressiveAttachment();
  ```

2. Call `ProgressiveAttachment::Write()` to send the data.

   * If the write occurs before running of the server-side done, the sent data is cached until the done is called.
   * If the write occurs after running of the server-side done, the sent data is written out in chunked mode immediately.

3. After usage, destruct all `butil::intrusive_ptr<brpc::ProgressiveAttachment>` to release related resources.

# Progressive receiving

Currently brpc server doesn't support calling the service callback once header part in the http request is parsed. In other words, brpc server is not suitable for receiving large or infinite sized body.

# FAQ

### Q: The nginx before brpc encounters final fail

The error is caused by that brpc server closes the http connection directly without sending a response.

brpc server supports a variety of protocols on the same port. When a request is failed to be parsed in HTTP, it's hard to tell that the request is definitely in HTTP. If the request is very likely to be one, the server sends HTTP 400 errors and closes the connection. However, if the error is caused HTTP method(at the beginning) or ill-formed serialization (may be caused by bugs at the HTTP client), the server still closes the connection without sending a response, which leads to "final fail" at nginx.

Solution: When using Nginx to forward traffic, set `$HTTP_method` to allowed HTTP methods or simply specify the HTTP method in `proxy_method`.

### Q: Does brpc support http chunked mode

Yes.

### Q: Why do HTTP requests containing BASE64 encoded query string fail to parse sometimes?

According to the [HTTP specification](http://tools.ietf.org/html/rfc3986#section-2.2), following characters need to be encoded with `%`.

```
       reserved    = gen-delims / sub-delims

       gen-delims  = ":" / "/" / "?" / "#" / "[" / "]" / "@"

       sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
                   / "*" / "+" / "," / ";" / "="
```

Base64 encoded string may end with `=` which is a reserved character (take `?wi=NDgwMDB8dGVzdA==&anothorkey=anothervalue` as an example). The strings may be parsed successfully, or may be not, depending on the implementation which should not be assumed in principle.

One solution is to remove the trailing `=` which does not affect the [Base64 decoding](http://en.wikipedia.org/wiki/Base64#Padding). Another method is to [percent-encode](https://en.wikipedia.org/wiki/Percent-encoding) the URI, and do percent-decoding before Base64 decoding.
