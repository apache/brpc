// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Zhangyi Chen(chenzhangyi01@baidu.com)
//          Ge,Jun (gejun@baidu.com)

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

// Returns literal description of `http_method'. "UNKNOWN" on not found.
const char *HttpMethod2Str(HttpMethod http_method);

// Convert case-insensitive `method_str' to enum HttpMethod.
// Returns true on success. 
bool Str2HttpMethod(const char* method_str, HttpMethod* method);

} // namespace brpc

#endif  //BRPC_HTTP_METHOD_H
