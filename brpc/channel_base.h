// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#ifndef BRPC_CHANNEL_BASE_H
#define BRPC_CHANNEL_BASE_H

#include <stdlib.h>
#include <ostream>
#include "base/logging.h"
#include <google/protobuf/service.h>            // google::protobuf::RpcChannel
#include "brpc/describable.h"

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.


namespace brpc {

// Base of all baidu-rpc channels.
class ChannelBase : public google::protobuf::RpcChannel/*non-copyable*/,
                    public Describable {
public:
    virtual int Weight() {
        CHECK(false) << "Not implemented";
        abort();
    };

    virtual int CheckHealth() = 0;
};

} // namespace brpc


#endif  // BRPC_CHANNEL_BASE_H
