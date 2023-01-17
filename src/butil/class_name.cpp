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

// Date: Mon. Nov 7 14:47:36 CST 2011

#include <cxxabi.h>                              // __cxa_demangle
#include <string>                                // std::string
#include <stdlib.h>                              // free()

namespace butil {

// Try to convert mangled |name| to human-readable name.
// Returns:
//   |name|    -  Fail to demangle |name|
//   otherwise -  demangled name
std::string demangle(const char* name) {
    // mangled_name
    //   A NULL-terminated character string containing the name to
    //   be demangled.
    // output_buffer:
    //   A region of memory, allocated with malloc, of *length bytes,
    //   into which the demangled name is stored. If output_buffer is
    //   not long enough, it is expanded using realloc. output_buffer
    //   may instead be NULL; in that case, the demangled name is placed
    //   in a region of memory allocated with malloc.
    // length
    //   If length is non-NULL, the length of the buffer containing the
    //   demangled name is placed in *length.
    // status
    //   *status is set to one of the following values:
    //    0: The demangling operation succeeded.
    //   -1: A memory allocation failure occurred.
    //   -2: mangled_name is not a valid name under the C++ ABI
    //       mangling rules.
    //   -3: One of the arguments is invalid.
    int status = 0;
    char* buf = abi::__cxa_demangle(name, NULL, NULL, &status);
    if (status == 0 && buf) {
        std::string s(buf);
        free(buf);
        return s;
    }
    return std::string(name);
}

}  // namespace butil

