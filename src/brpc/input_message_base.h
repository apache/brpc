// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Sep  2 21:38:04 CST 2014

#ifndef BRPC_INPUT_MESSAGE_BASE_H
#define BRPC_INPUT_MESSAGE_BASE_H

#include "brpc/socket_id.h"           // SocketId
#include "brpc/destroyable.h"         // DestroyingPtr


namespace brpc {

// Messages returned by Parse handlers must extend this class
class InputMessageBase : public Destroyable {
protected:
    // Implement this method to customize deletion of this message.
    virtual void DestroyImpl() = 0;
    
public:
    // Called to release the memory of this message instead of "delete"
    void Destroy();
    
    // Own the socket where this message is from.
    Socket* ReleaseSocket();

    // Get the socket where this message is from.
    Socket* socket() const { return _socket.get(); }

    // Arg of the InputMessageHandler which parses this message successfully.
    const void* arg() const { return _arg; }

    // [Internal]
    int64_t received_us() const { return _received_us; }
    int64_t base_real_us() const { return _base_real_us; }

protected:
    virtual ~InputMessageBase();

private:
friend class InputMessenger;
friend void* ProcessInputMessage(void*);
friend class Stream;
    int64_t _received_us;
    int64_t _base_real_us;
    SocketUniquePtr _socket;
    void (*_process)(InputMessageBase* msg);
    const void* _arg;
};

} // namespace brpc


#endif  // BRPC_INPUT_MESSAGE_BASE_H
