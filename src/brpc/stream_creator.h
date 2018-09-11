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

// Abstract creation of "user-level connection" over a RPC-like process.
// Lifetime of this object should be guaranteed by user during the RPC,
// generally this object is created before RPC and destroyed after RPC.
class StreamCreator {
public:
    // Called when the socket for sending request is about to be created.
    // If the RPC has retries, this function is called before each retry.
    // Params:
    //  inout: pointing to the socket to send requests by default,
    //    replaceable by user created ones (or keep as it is). remote_side()
    //    of the replaced socket must be same with the default socket.
    //    The replaced socket should take cntl->connection_type() into account
    //    since the framework sends request by the replaced socket directly
    //    when stream_creator is present.
    //  cntl: contains contexts of the RPC, if there's any error during
    //    replacement, call cntl->SetFailed().
    virtual void OnCreatingStream(SocketUniquePtr* inout,
                                  Controller* cntl) = 0;

    // Called when the stream is about to destroyed.
    // If the RPC has retries, this function is called before each retry.
    // This method is always called even if OnCreatingStream() is not called.
    // Params:
    //   sending_sock: The socket chosen by OnCreatingStream(), if OnCreatingStream
    //     is not called, the enclosed socket may be NULL.
    //   cntl: contexts of the RPC
    //   error_code: Use this instead of cntl->ErrorCode()
    //   end_of_rpc: true if the RPC is about to destroyed.
    virtual void OnDestroyingStream(SocketUniquePtr& sending_sock,
                                    Controller* cntl,
                                    int error_code,
                                    bool end_of_rpc) = 0;
};

} // namespace brpc


#endif  // BRPC_STREAM_CREATOR_H
