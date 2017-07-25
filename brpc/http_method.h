// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/12/26 14:43:32

#ifndef  BRPC_HTTP_METHOD_H
#define  BRPC_HTTP_METHOD_H


namespace brpc {

enum HttpMethod {
    HTTP_METHOD_DELETE      =   0,
    HTTP_METHOD_GET         =   1,
    HTTP_METHOD_HEAD        =   2,
    HTTP_METHOD_POST        =   3,
    HTTP_METHOD_PUT         =   4,
    HTTP_METHOD_CONNECT     =   5,
    HTTP_METHOD_OPTIONS     =   6,
    HTTP_METHOD_TRACE       =   7,
    HTTP_METHOD_COPY        =   8,
    HTTP_METHOD_LOCK        =   9,
    HTTP_METHOD_MKCOL       =   10,
    HTTP_METHOD_MOVE        =   11,
    HTTP_METHOD_PROPFIND    =   12,
    HTTP_METHOD_PROPPATCH   =   13,
    HTTP_METHOD_SEARCH      =   14,
    HTTP_METHOD_UNLOCK      =   15,
    HTTP_METHOD_REPORT      =   16,
    HTTP_METHOD_MKACTIVITY  =   17,
    HTTP_METHOD_CHECKOUT    =   18,
    HTTP_METHOD_MERGE       =   19,
    HTTP_METHOD_MSEARCH     =   20,  // M-SEARCH
    HTTP_METHOD_NOTIFY      =   21,
    HTTP_METHOD_SUBSCRIBE   =   22,
    HTTP_METHOD_UNSUBSCRIBE =   23,
    HTTP_METHOD_PATCH       =   24,
    HTTP_METHOD_PURGE       =   25,
    HTTP_METHOD_MKCALENDAR  =   26
};

// Return the method description of |http_method| or "UNKNOWN" if |http_method|
// is unknown.
// NULL is never supposed to be returned
const char *GetMethodStr(HttpMethod http_method);

} // namespace brpc


#endif  //BRPC_HTTP_METHOD_H
