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

// Process-related Info

// Date: Wed Apr 11 14:35:56 CST 2018

#ifndef BUTIL_PROCESS_UTIL_H
#define BUTIL_PROCESS_UTIL_H

#include <sys/types.h>

namespace butil {

// Read command line of this program. If `with_args' is true, args are
// included and separated with spaces.
// Returns length of the command line on sucess, -1 otherwise.
// NOTE: `buf' does not end with zero.
ssize_t ReadCommandLine(char* buf, size_t len, bool with_args);

} // namespace butil

#endif // BUTIL_PROCESS_UTIL_H
