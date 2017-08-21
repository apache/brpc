// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 26 12:18:15 2015

#ifndef BRPC_MONGO_SERVICE_ADAPTOR_H
#define BRPC_MONGO_SERVICE_ADAPTOR_H

#include "base/iobuf.h"
#include "brpc/input_message_base.h"
#include "brpc/shared_object.h"


namespace brpc {

// custom mongo context. derive this and implement your own functionalities.
class MongoContext : public SharedObject {
public:
    virtual ~MongoContext() {}
};

// a container of custom mongo context. created by ParseMongoRequest when the first msg comes over
// a socket. it lives as long as the socket.
class MongoContextMessage : public InputMessageBase {
public:
    MongoContextMessage(MongoContext *context) : _context(context) {}
    // @InputMessageBase
    void DestroyImpl() { delete this; }
    MongoContext* context() { return _context.get(); }

private:
    base::intrusive_ptr<MongoContext> _context;
};

class MongoServiceAdaptor {
public:
    // Make an error msg when the cntl fails. If cntl fails, we must send mongo client a msg not 
    // only to indicate the error, but also to finish the round trip.
    virtual void SerializeError(int response_to, base::IOBuf* out_buf) const = 0;

    // Create a custom context which is attached to socket. This func is called only when the first
    // msg from the socket comes. The context will be destroyed when the socket is closed.
    virtual MongoContext* CreateSocketContext() const = 0;
};

} // namespace brpc


#endif
