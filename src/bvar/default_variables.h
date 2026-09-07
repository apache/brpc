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

#ifndef BVAR_DEFAULT_VARIABLES_H
#define BVAR_DEFAULT_VARIABLES_H

#include <sys/utsname.h>                   // struct utsname
#include <sstream>                         // std::ostringstream
#include <string>                          // std::string

namespace bvar {

// Build the value of the `kernel_version` bvar from a uname(2) result.
// The field layout mirrors `uname -ap` on the major platforms:
//   Linux : sysname nodename release version machine processor machine GNU/Linux
//   macOS : sysname nodename release version machine processor
//
// This is intentionally a header-only helper so that it is shared by both
// default_variables.cpp and the unit tests. default_variables.o is stripped
// from unit-test binaries (see BVAR_NOT_LINK_DEFAULT_VARIABLES in
// variable.cpp), so keeping the formatting logic here lets tests exercise the
// exact production formatter without depending on that object being linked.
inline std::string make_kernel_version_string(const struct utsname& buf) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    const char* processor = "arm";
#elif defined(__APPLE__) && defined(__x86_64__)
    const char* processor = "i386";
#else
    const char* processor = buf.machine;
#endif
    std::ostringstream oss;
    oss << buf.sysname << ' ' << buf.nodename << ' '
        << buf.release << ' ' << buf.version << ' '
        << buf.machine << ' ' << processor;
#if defined(__linux__) && !defined(__ANDROID__)
    // `uname -a` appends the hardware platform and the operating-system
    // identifier on Linux; the hardware platform equals `machine` here.
    oss << ' ' << buf.machine << " GNU/Linux";
#endif
    oss << '\n';
    return oss.str();
}

}  // namespace bvar

#endif  // BVAR_DEFAULT_VARIABLES_H
