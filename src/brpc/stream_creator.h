// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_STREAM_CREATOR_H
#define BRPC_STREAM_CREATOR_H

#include "brpc/socket_id.h"

namespace brpc {
class Controller;
class StreamUserData;

// Abstract creation of "user-level connection" over a RPC-like process.
// Lifetime of this object should be guaranteed by user during the RPC,
// generally this object is created before RPC and destroyed after RPC.
class StreamCreator {
public:
    virtual ~StreamCreator() = default;

    // Called when the socket for sending request is about to be created.
    // If the RPC has retries, this function MAY be called before each retry.
    // This function would not be called if some preconditions are not
    // satisfied.
    // Params:
    //  inout: pointing to the socket to send requests by default,
    //    replaceable by user created ones (or keep as it is). remote_side()
    //    of the replaced socket must be same with the default socket.
    //    The replaced socket should take cntl->connection_type() into account
    //    since the framework sends request by the replaced socket directly
    //    when stream_creator is present.
    //  cntl: contains contexts of the RPC, if there's any error during
    //    replacement, call cntl->SetFailed().
    virtual StreamUserData* OnCreatingStream(SocketUniquePtr* inout,
                                             Controller* cntl) = 0;

    // Called when the StreamCreator is about to destroyed.
    // This function MUST be called only once at the end of successful RPC
    // Call to recycle resources.
    // Params:
    //   cntl: contexts of the RPC
    virtual void DestroyStreamCreator(Controller* cntl) = 0;
};

// The Intermediate user data created by StreamCreator to record the context
// of a specific stream request.
class StreamUserData {
public:
    virtual ~StreamUserData() = default;

    // Called when the streamUserData is about to destroyed.
    // This function MUST be called to clean up resources if OnCreatingStream
    // of StreamCreator has returned a valid StreamUserData pointer.
    // Params:
    //   sending_sock: The socket chosen by OnCreatingStream(), if an error
    //     happens during choosing, the enclosed socket is NULL.
    //   cntl: contexts of the RPC.
    //   error_code: Use this instead of cntl->ErrorCode().
    //   end_of_rpc: true if the RPC is about to destroyed.
    virtual void DestroyStreamUserData(SocketUniquePtr& sending_sock,
                                       Controller* cntl,
                                       int error_code,
                                       bool end_of_rpc) = 0;
};

} // namespace brpc


#endif  // BRPC_STREAM_CREATOR_H
