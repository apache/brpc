// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#ifndef BRPC_STREAM_CREATOR_H
#define BRPC_STREAM_CREATOR_H

#include "brpc/socket_id.h"


namespace brpc {
class Controller;

// Abstract creation of "user-level connection" over a RPC-like process.
class StreamCreator {
public:
    // Replace the socket in `inout' with another one (or keep as it is).
    // remote_side() of the replaced socket must be same with *inout.
    // Called each time before iteracting with a server. Notice that
    // if the RPC has retries, this function is called before each retry.
    // `cntl' contains necessary information about the RPC, if there's
    // any error during replacement, call cntl->SetFailed().
    // The replaced socket should take cntl->connection_type() into account
    // since the framework will send request by the replaced socket directly
    // when stream_creator is present.
    virtual void ReplaceSocketForStream(SocketUniquePtr* inout,
                                        Controller* cntl,
                                        int previous_error_code) = 0;

    // Called when one interation with the server completes. A RPC for
    // creating a stream may interact with servers more than once.
    // `cntl' contains necessary information about the call. `sending_sock'
    // is the socket to the server interacted with. If the RPC was failed,
    // `sending_sock' is prossibly NULL(fail before choosing a server). User
    // can own `sending_sock' and set the unique pointer to NULL, otherwise
    // the socket is cleaned by framework when connection_type is non-single.
    virtual void OnStreamCreationDone(SocketUniquePtr& sending_sock,
                                      Controller* cntl) = 0;
};

} // namespace brpc


#endif  // BRPC_STREAM_CREATOR_H
