// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
 
// Date: Fri Sep 10 13:34:25 CST 2010

// Add customized errno.

#ifndef BUTIL_BAIDU_ERRNO_H
#define BUTIL_BAIDU_ERRNO_H

#ifndef __const__
// Avoid over-optimizations of TLS variables by GCC>=4.8
// See: https://github.com/apache/incubator-brpc/issues/1693
#define __const__ __unused__
#endif

#include <errno.h>                           // errno
#include "butil/macros.h"                     // BAIDU_CONCAT

//-----------------------------------------
// Use system errno before defining yours !
//-----------------------------------------
//
// To add new errno, you shall define the errno in header first, either by
// macro or constant, or even in protobuf.
//
//     #define ESTOP -114                // C/C++
//     static const int EMYERROR = 30;   // C/C++
//     const int EMYERROR2 = -31;        // C++ only
//
// Then you can register description of the error by calling
// BAIDU_REGISTER_ERRNO(the_error_number, its_description) in global scope of
// a .cpp or .cc files which will be linked.
// 
//     BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")
//     BAIDU_REGISTER_ERRNO(EMYERROR, "my error")
//
// Once the error is successfully defined:
//     berror(error_code) returns the description.
//     berror() returns description of last system error code.
//
// %m in printf-alike functions does NOT recognize errors defined by
// BAIDU_REGISTER_ERRNO, you have to explicitly print them by %s.
//
//     errno = ESTOP;
//     printf("Something got wrong, %m\n");            // NO
//     printf("Something got wrong, %s\n", berror());  // YES
//
// When the error number is re-defined, a linking error will be reported:
// 
//     "redefinition of `class BaiduErrnoHelper<30>'"
//
// Or the program aborts at runtime before entering main():
// 
//     "Fail to define EMYERROR(30) which is already defined as `Read-only file system', abort"
//

namespace butil {
// You should not call this function, use BAIDU_REGISTER_ERRNO instead.
extern int DescribeCustomizedErrno(int, const char*, const char*);
}

template <int error_code> class BaiduErrnoHelper {};

#define BAIDU_REGISTER_ERRNO(error_code, description)                   \
    const int ALLOW_UNUSED BAIDU_CONCAT(baidu_errno_dummy_, __LINE__) =              \
        ::butil::DescribeCustomizedErrno((error_code), #error_code, (description)); \
    template <> class BaiduErrnoHelper<(int)(error_code)> {};

const char* berror(int error_code);
const char* berror();

#endif  //BUTIL_BAIDU_ERRNO_H
