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


#ifndef BRPC_HAS_EPOLLRDHUP_H
#define BRPC_HAS_EPOLLRDHUP_H


namespace brpc {

// Check if the kernel supports EPOLLRDHUP which is added in Linux 2.6.17
// This flag is useful in Edge Triggered mode. Without the flag user has
// to call an additional read() even if return value(positive) is less
// than given `count', otherwise return value=0(indicating EOF) may be lost.
extern const unsigned int has_epollrdhup;

} // namespace brpc


#endif  // BRPC_HAS_EPOLLRDHUP_H
