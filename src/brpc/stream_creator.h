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
                                        Controller* cntl) = 0;

    // `cntl' contains necessary information about the call. `sending_sock'
    // is the socket to the server interacted with. If the RPC was failed,
    // `sending_sock' is prossibly NULL(fail before choosing a server). User
    // can own `sending_sock' and set the unique pointer to NULL, otherwise
    // the socket is cleaned-up by framework and then CleanupSocketForStream()
    virtual void OnStreamCreationDone(SocketUniquePtr& sending_sock,
                                      Controller* cntl) = 0;
    
    // Called when one interation with the server completes. A RPC for
    // creating a stream may interact with servers more than once.
    // This method is paired with _each_ ReplaceSocketForStream().
    // OnStreamCreationDone() is called _before_ last CleanupSocketForStream(),
    // If OnStreamCreationDone() moved the `sending_sock', `prev_sock' to this
    // method is NULL.
    // Use `error_code' instead of cntl->ErrorCode().
    virtual void CleanupSocketForStream(Socket* prev_sock,
                                        Controller* cntl,
                                        int error_code) = 0;
};

} // namespace brpc


#endif  // BRPC_STREAM_CREATOR_H
