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

#ifndef BRPC_MONGO_SERVICE_ADAPTOR_H
#define BRPC_MONGO_SERVICE_ADAPTOR_H

#include "butil/iobuf.h"
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
    butil::intrusive_ptr<MongoContext> _context;
};

class MongoServiceAdaptor {
public:
    // Make an error msg when the cntl fails. If cntl fails, we must send mongo client a msg not 
    // only to indicate the error, but also to finish the round trip.
    virtual void SerializeError(int response_to, butil::IOBuf* out_buf) const = 0;

    // Create a custom context which is attached to socket. This func is called only when the first
    // msg from the socket comes. The context will be destroyed when the socket is closed.
    virtual MongoContext* CreateSocketContext() const = 0;
};

} // namespace brpc


#endif
