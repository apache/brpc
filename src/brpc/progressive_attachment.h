// Copyright (c) 2016 Baidu, Inc.
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

#ifndef BRPC_PROGRESSIVE_ATTACHMENT_H
#define BRPC_PROGRESSIVE_ATTACHMENT_H

#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/endpoint.h"             // butil::EndPoint
#include "bthread/types.h"             // bthread_id_t
#include "brpc/socket_id.h"       // SocketUniquePtr
#include "brpc/shared_object.h"   // SharedObject


namespace brpc {

class ProgressiveAttachment : public SharedObject {
friend class Controller;
public:
    // [Thread-safe]
    // Write `data' as one HTTP chunk to peer ASAP.
    // Returns 0 on success, -1 otherwise and errno is set.
    // Errnos are same as what Socket.Write may set.
    int Write(const butil::IOBuf& data);
    int Write(const void* data, size_t n);

    // Get ip/port of peer/self.
    butil::EndPoint remote_side() const;
    butil::EndPoint local_side() const;

    // [Not thread-safe and can only be called once]
    // Run the callback when the underlying connection is broken (thus
    // transmission of the attachment is permanently stopped), or when
    // this attachment is destructed. In another word, the callback will
    // always be run.
    void NotifyOnStopped(google::protobuf::Closure* callback);
    
protected:
    // Transfer-Encoding is added since HTTP/1.1. If the protocol of the
    // response is before_http_1_1, we will write the data directly to the
    // socket without any futher modification and close the socket after all the
    // data has been written (so the client would receive EOF). Otherwise we
    // will encode each piece of data in the format of chunked-encoding.
    ProgressiveAttachment(SocketUniquePtr& movable_httpsock,
                          bool before_http_1_1);
    ~ProgressiveAttachment();

    // Called by controller only.
    void MarkRPCAsDone(bool rpc_failed);
    
    bool _before_http_1_1;
    bool _pause_from_mark_rpc_as_done;
    butil::atomic<int> _rpc_state;
    butil::Mutex _mutex;
    SocketUniquePtr _httpsock;
    butil::IOBuf _saved_buf;
    bthread_id_t _notify_id;

private:
    static const int RPC_RUNNING;
    static const int RPC_SUCCEED;
    static const int RPC_FAILED;
};

} // namespace brpc


#endif  // BRPC_PROGRESSIVE_ATTACHMENT_H
