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

#ifndef BRPC_SERVER_NODE_H
#define BRPC_SERVER_NODE_H

#include <string>
#include "butil/endpoint.h"

namespace brpc {

// Representing a server inside a NamingService.
struct ServerNode {
    ServerNode() {}
    
    explicit ServerNode(const butil::EndPoint& pt) : addr(pt) {}

    ServerNode(butil::ip_t ip, int port, const std::string& tag2)
        : addr(ip, port), tag(tag2) {}

    ServerNode(const butil::EndPoint& pt, const std::string& tag2)
        : addr(pt), tag(tag2) {}

    ServerNode(butil::ip_t ip, int port) : addr(ip, port) {}

    butil::EndPoint addr;
    std::string tag;
};

inline bool operator<(const ServerNode& n1, const ServerNode& n2)
{ return n1.addr != n2.addr ? (n1.addr < n2.addr) : (n1.tag < n2.tag); }

inline bool operator==(const ServerNode& n1, const ServerNode& n2)
{ return n1.addr == n2.addr && n1.tag == n2.tag; }

inline bool operator!=(const ServerNode& n1, const ServerNode& n2)
{ return !(n1 == n2); }

inline std::ostream& operator<<(std::ostream& os, const ServerNode& n) {
    os << n.addr;
    if (!n.tag.empty()) {
        os << "(tag=" << n.tag << ')';
    }
    return os;
}

} // namespace brpc

#endif  // BRPC_SERVER_NODE_H
