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


#ifndef BRPC_SOCKET_ID_H
#define BRPC_SOCKET_ID_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include <stdint.h>               // uint64_t
#include "butil/unique_ptr.h"      // std::unique_ptr


namespace brpc {

// Unique identifier of a Socket.
// Users shall store SocketId instead of Sockets and call Socket::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed Socket will not be recycled.
typedef uint64_t SocketId;

const SocketId INVALID_SOCKET_ID = (SocketId)-1;

class Socket;

extern void DereferenceSocket(Socket*);

struct SocketDeleter {
    void operator()(Socket* m) const {
        DereferenceSocket(m);
    }
};

typedef std::unique_ptr<Socket, SocketDeleter> SocketUniquePtr;

} // namespace brpc


#endif  // BRPC_SOCKET_ID_H
