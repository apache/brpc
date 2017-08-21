// Baidu RPC - A framework to host and access services throughout Baidu. 
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep  7 17:24:45 CST 2014

#ifndef BRPC_SOCKET_ID_H
#define BRPC_SOCKET_ID_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include <stdint.h>               // uint64_t
#include "base/unique_ptr.h"      // std::unique_ptr


namespace brpc {

// Unique identifier of a Socket.
// Users shall store SocketId instead of Sockets and call Socket::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed Socket will not be recycled.
typedef uint64_t SocketId;

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
