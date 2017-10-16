This document describes the "pure" HTTP service rather than protobuf based ones (use pure pb as input/output format). The HTTP service declaration inside the .proto file is still necessary although the request/response structure is empty. The reason is to keep all the service declaration inside proto files rather than scattering in code, conf, proto and other places. For examples please refer to [http_server.cpp](https://github.com/brpc/brpc/blob/master/example/http_c++/http_server.cpp).

# URL Prefix: /ServiceName/MethodName

All protobuf based services can be accessed through URL `/ServiceName/MethodName` by default, where the `ServiceName` does not contain package name. This URL rule should cover many cases in general. The detail is as follows:

1. Add service declaration in the proto file.

   Note that `HttpRequest` and `HttpResponse` are empty in the proto because the HTTP request body is inside  `Controller.request_attachment()` while the HTTP request header is inside `Controller.http_request()`. Similarly, those for the HTTP response locate in `Controller.response_attachment()` and `Controller.http_response()`.	

```protobuf
option cc_generic_services = true;
 
message HttpRequest { };
message HttpResponse { };
 
service HttpService {
      rpc Echo(HttpRequest) returns (HttpResponse);
};
```

2. Implement the service by inheriting the base class inside .pb.h which is the same process as other protobuf based services.

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
 
        // Return plain text
        cntl->http_response().set_content_type("text/plain");
       
        // Print the query string and the request body
        // and use these as response
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

3. Add the implemented service into Server and then access it using the following URL (Note that path 									after `/HttpService/Echo ` is filled into `cntl->http_request().unresolved_path()`, which is always 			normalized):

| URL                        | Protobuf Method  | cntl->http_request().uri().path() | cntl->http_request().unresolved_path() |
| -------------------------- | ---------------- | --------------------------------- | -------------------------------------- |
| /HttpService/Echo          | HttpService.Echo | "/HttpService/Echo"               | ""                                     |
| /HttpService/Echo/Foo      | HttpService.Echo | "/HttpService/Echo/Foo"           | "Foo"                                  |
| /HttpService/Echo/Foo/Bar  | HttpService.Echo | "/HttpService/Echo/Foo/Bar"       | "Foo/Bar"                              |
| /HttpService//Echo///Foo// | HttpService.Echo | "/HttpService//Echo///Foo//"      | "Foo"                                  |
| /HttpService               | No such method   |                                   |                                        |

# URL Prefix: /ServiceName

Some resource management HTTP services may need this URL rule, such as a FileService to provide access to files: Use `/FileService/foobar.txt` to represent file `foobar.txt` in the working directory, or `/FileService/app/data/boot.cfg` to represent file `boot.cfg` in directory `app/data`.

To implement this:

1. In the proto file, use `FileService` as the service name and `default_method` for the method name.

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

3. Add the implemented service into Server and then access it using the following URL (the path after `/FileService` locates in `cntl->http_request().unresolved_path()`, which is always normalized):

| URL                             | Protobuf Method            | cntl->http_request().uri().path() | cntl->http_request().unresolved_path() |
| ------------------------------- | -------------------------- | --------------------------------- | -------------------------------------- |
| /FileService                    | FileService.default_method | "/FileService"                    | ""                                     |
| /FileService/123.txt            | FileService.default_method | "/FileService/123.txt"            | "123.txt"                              |
| /FileService/mydir/123.txt      | FileService.default_method | "/FileService/mydir/123.txt"      | "mydir/123.txt"                        |
| /FileService//mydir///123.txt// | FileService.default_method | "/FileService//mydir///123.txt//" | "mydir/123.txt"                        |

# Restful URL

brpc also supports to specify a URL for each method in a service:

```c++
// If `restful_mappings' is non-empty, the method in service can
// be accessed by the specified URL rather than /ServiceName/MethodName. 
// Mapping rules: "PATH1 => NAME1, PATH2 => NAME2 ..."
// where `PATH' is a valid HTTP path and `NAME' is the method name.       
int AddService(google::protobuf::Service* service,
               ServiceOwnership ownership,
               butil::StringPiece restful_mappings);
```

For example, the following `QueueService` contains several HTTP methods:

```protobuf
service QueueService {
    rpc start(HttpRequest) returns (HttpResponse);
    rpc stop(HttpRequest) returns (HttpResponse);
    rpc get_stats(HttpRequest) returns (HttpResponse);
    rpc download_data(HttpRequest) returns (HttpResponse);
};
```

If we add it into the server as before, it could only be accessed by URLs such as `/QueueService/start` or ` /QueueService/stop`.

However, adding the third parameter `restful_mappings` in `AddService` allows us to customize URL:

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

There are 3 mappings in the third parameter (which is a string separated by comma) of `AddService` above. Each tells bpc to call the method at the right side of the arrow when it sees a incoming URL matching the left side. The star in `/v1/queue/stats/*` matches any string. 

More about the mapping rules:

- Multiple paths can map to the same method.
- Besides pure HTTP services, protobuf based ones are also supported.
- Methods that are not in the mapping can still be accessed by `/ServiceName/MethodName`. Otherwise, this rule will be disabled.
- The lexical rules are relax. For example,  `==>` and ` ===>` the same. Extra spaces from the beginning and the end of the mapping string, extra slashes in the URL, and extra commas at the end, are ignored automatically.
- The URL pattern `PATH` and `PATH/*` can coexist.
- Characters can appear after asterisk, which means suffix matching is supported.
- At most one star is allowed in a path.

The extra part after the asterisk can be obtained by `cntl.http_request().unresolved_path()`, which is ensured to be normalized: No slash from the beginning and the end. No repeated slash in the middle. For example, the following URL:

![img](../images/restful_1.png)

or:

![img](../images/restful_2.png)

will be normalized to `foo/bar`, where extra slashes from both sides and the middle are removed.

Note that `cntl.http_request().uri().path()` is not ensured to be normalized, which is `"//v1//queue//stats//foo///bar//////"` and `"//vars///foo////bar/////"` respectively in the above example.

The built-in service page of `/status` shows all the methods along with their URLs, whose form are: `@URL1 @URL2` ...

![img](../images/restful_3.png)

# HTTP Parameters

## HTTP headers

HTTP headers are a series of key value pairs, some of which are defined by the HTTP specification, while others are free to use.

It's easy to confuse HTTP headers with query string, which is part of the URL. Query string can also express the key value relationship using the form `key1=value1&key2=value2&...`, and it's simpler to manipulate on GUI such as browsers. However, this usage is more of a custom convention rather than part of the HTTP specification. According our observation, in general, HTTP headers are used for passing framework and protocol level parameters since they are part of the specification which will be recognized by all http servers, while query string, as part of the URL, is suitable for user-level parameters as it's easy to read and modify.

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

As a frequently used header, `Content-type` marks the type of the HTTP body, and it can be fetched through a specialized interface. Accordingly, it can't be fetched by `GetHeader()`.

```c++
// Get Content-Type
if (cntl->http_request().content_type() == "application/json") {
    ...
}
...
// Set Content-Type
cntl->http_response().set_content_type("text/html");
```

If RPC fails (`Controller` has been `SetFailed`), the framework will overwrite `Content-Type` with `text/plain` and fill the response body with `Controller::ErrorText()`.

## Status Code

Status code is a special field for HTTP response, which marks the completion status of the http request. Please use the `enums` defined in [http_status_code.h](https://github.com/brpc/brpc/blob/master/src/brpc/http_status_code.h) to follow the HTTP specification.

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

The following code implements server's redirection with status code 302:

```c++
cntl->http_response().set_status_code(brpc::HTTP_STATUS_FOUND);
cntl->http_response().SetHeader("Location", "http://bj.bs.bae.baidu.com/family/image001(4979).jpg");
```

![img](../images/302.png)

## Query String

As mentioned above in the [HTTP headers](http_service.md#http-headers), brpc interpret query string as in a custom convention, whose form is `key1=value1&key2=value2&…`. Keys without value are also acceptable and accessible through `GetQuery` (returns an empty string), which is often used as bool-type switch. The interface is defined in [uri.h](https://github.com/brpc/brpc/blob/master/src/brpc/uri.h).

```c++
const std::string* time_value = cntl->http_request().uri().GetQuery("time");
if (time_value != NULL) {  // the query string is present
    LOG(TRACE) << "time = " << *time_value;
}
 
...
cntl->http_request().uri().SetQuery("time", "2015/1/2");
```

# Debug

Turn on [-http_verbose](http://brpc.baidu.com:8765/flags/http_verbose) to print contents of all http requests and responses to stderr. Note that this should only be used for debugging rather than online services.

# Compress response body

HTTP services usually apply compression on the http body in order to reduce the transmission time of text-based pages and thus speed up the loading process effectively. 

Calls to `Controller::set_response_compress_type(brpc::COMPRESS_TYPE_GZIP)` will try to compress the http body using gzip and set `Content-Encoding` to `gzip`. It has no effect when request does not specify the `Accept-encoding` or value does not contain gzip. For example, curl does not support compression without option `—compressed`, thus the server will always return the uncompressed results.

# Decompress request body

Due to generality concern, brpc won't decompress request body automatically. You can do it yourself as it's not that complicate:

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

# Turn on HTTPS

Make sure to update openssl library to the latest version before turning on HTTPS. Old verions of openssl have severe security problems and support only a few encryption algorithms, which conflicts with the purpose of using SSL. Set the `SSLOptions` inside `ServerOptions` to turn on HTTPS.

```c++
// Certificate structure
struct CertInfo {
    // Certificate in PEM format.
    // Note that CN and alt subjects will be extracted from the certificate,
    // and will be used as hostnames. Requests to this hostname (provided SNI
    // extension supported) will be encrypted using this certifcate. 
    // Supported both file path and raw string
    std::string certificate;

    // Private key in PEM format.
    // Supported both file path and raw string based on prefix:
    std::string private_key;
        
    // Additional hostnames besides those inside the certificate. Wildcards
    // are supported but it can only appear once at the beginning (i.e. *.xxx.com).
    std::vector<std::string> sni_filters;
};

struct SSLOptions {
    // Default certificate which will be loaded into server. Requests
    // without hostname or whose hostname doesn't have a corresponding
    // certificate will use this certificate. MUST be set to enable SSL.
    CertInfo default_cert;
    
    // Additional certificates which will be loaded into server. These
    // provide extra bindings between hostnames and certificates so that
    // we can choose different certificates according to different hostnames.
    // See `CertInfo' for detail.
    std::vector<CertInfo> certs;

    // When set, requests without hostname or whose hostname can't be found in
    // any of the cerficates above will be dropped. Otherwise, `default_cert'
    // will be used.
    // Default: false
    bool strict_sni;
 
    // ... Other options
};
```
Other options include: cipher suites (`ECDHE-RSA-AES256-GCM-SHA384` is recommended since it's the default suite used by chrome with the highest priority. It‘s one of the safest suites but costs more CPU), session reuse and so on. For more information please refer to [server.h](https://github.com/brpc/brpc/blob/master/src/brpc/server.h).

After turning on HTTPS, you can still send HTTP requests to the same port. Server will identify whether it's HTTP or HTTPS automatically. The result can be fetched through `Controller` in service callback: 

```c++
bool Controller::is_ssl() const;
```

# Performance

Productions without extreme performance requirements tend to use HTTP protocol, especially those mobile products. As a result, we put great emphasis on the implementation quality of HTTP. To be more specific:

- Use [http parser](https://github.com/brpc/brpc/blob/master/src/brpc/details/http_parser.h) (some part comes from nginx) of node.js to parse http message, which is a lightweight, excellent implementation.
- Use [rapidjson](https://github.com/miloyip/rapidjson) to parse json, which is a json library developed by a Tencent expert for extreme performance.
- In the worst case, the time complexity of parsing http requests is still O(N), where N is the number of request bytes. In other words, if the parsing code requires the http request to be complete, then it may cost O(N^2). This feature is very helpful since most HTTP requests have large body.
- The process of multiple HTTP messages from different clients is highly concurrent. Even a complicate http messages won't affect response to other clients. It's difficult to achieve this for other rpc implementations and http servers based on [single-threaded reactor](threading_overview.md#单线程reactor).

# Progressive sending

brpc server is also suitable for sending large or infinite size body. Uses the following method:

1. Call `Controller::CreateProgressiveAttachment()` to create a body that can ben sent progressively. Use `boost::intrusive_ptr<>` to manage the returning `ProgressiveAttachment` object: `boost::intrusive_ptr <brpc::ProgressiveAttachment> pa (cntl->CreateProgressiveAttachment());`. The detail is defined in `<brpc/progressive_attachment.h>`.
2. Call `ProgressiveAttachment::Write()` to send the data. If the write occurs before the end of server callback, the sent data will be cached until the server callback ends. It starts writing after the header portion is sent. If the write occurs after the server callback, the data sent will be written in chunked mode immediately.
3. After sending, make sure that all `boost::intrusive_ptr<brpc::ProgressiveAttachment>` have been  destructed.

# Progressive receiving

Currently brpc server doesn't support calling user's callback once it finishes parsing the header portion of the http request. In other words, it's not suitable for receiving large or infinite size body.

# FAQ

### Q: nginx which uses brpc as upstream encounters final fail (ff)

brpc server supports running a variety of protocols on the same port. As a result, when it encounters an illegal HTTP request due to parsing failure, it can not be certain that this request is using HTTP. The server treats errors after paring the query-string portion with an HTTP 400 followed and then closes the connection, since it has a large probability that this is an HTTP request. However, for HTTP method errors (such as invalid methods other than GET, POST, HEAD, etc), or other serious format errors (may be caused by HTTP client bug), the server will close the connection immediately, which leads to nginx ff.

Solution: When using Nginx to forward traffic, filter out the unexpected HTTP method using `$HTTP_method`, or simply specify the HTTP method using `proxy_method` to avoid ff. 

### Q: Does brpc support http chunked mode

Yes

### Q: Why does HTTP requests containing BASE64 encoded query string fail to parse sometimes?

According to the [HTTP specification](http://tools.ietf.org/html/rfc3986#section-2.2), the following characters need to be encoded using `%`.

```
       reserved    = gen-delims / sub-delims

       gen-delims  = ":" / "/" / "?" / "#" / "[" / "]" / "@"

       sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
                   / "*" / "+" / "," / ";" / "="
```

Base64 encoded string may end with `=` or `==` (for example, `?wi=NDgwMDB8dGVzdA==&anothorkey=anothervalue`). These string may be parsed successfully, or may be not. It depends on the implementation which users should not assume.

One solution is to remove the trailing `=` since it won't affect the [Base64 decode](http://en.wikipedia.org/wiki/Base64#Padding). Another way is to encode the URI using Base64 followed by `%`, and then decode it using `%` followed by Base64 before access. 