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


#ifndef BRPC_PROGRESSIVE_READER_H
#define BRPC_PROGRESSIVE_READER_H

#include "brpc/shared_object.h"


namespace brpc {

// [Implement by user]
// To read a very long or infinitely long response progressively.
// Client-side usage:
//   cntl.response_will_be_read_progressively();                // before RPC
//   ...
//   channel.CallMethod(NULL, &cntl, NULL, NULL, NULL/*done*/);
//   ...
//   cntl.ReadProgressiveAttachmentBy(new MyProgressiveReader); // after RPC
//   ...
class ProgressiveReader {
public:
    // Called when one part was read.
    // Error returned is treated as *permenant* and the socket where the
    // data was read will be closed.
    // A temporary error may be handled by blocking this function, which
    // may block the HTTP parsing on the socket.
    virtual butil::Status OnReadOnePart(const void* data, size_t length) = 0;

    // Called when there's nothing to read anymore. The `status' is a hint for
    // why this method is called.
    // - status.ok(): the message is complete and successfully consumed.
    // - otherwise: socket was broken or OnReadOnePart() failed.
    // This method will be called once and only once. No other methods will
    // be called after. User can release the memory of this object inside.
    virtual void OnEndOfMessage(const butil::Status& status) = 0;
    
protected:
    virtual ~ProgressiveReader() {}
};

// [Implement by protocol handlers]
// Share ProgressiveReader between protocol handlers and controllers.
// Take chunked HTTP response as an example:
//  1. The protocol handler parses headers and goes to ProcessHttpResponse
//     before reading all body.
//  2. ProcessHttpResponse sets controller's RPA which is just the HttpContext
//     in this case. The RPC ends at the end of ProcessHttpResponse.
//  3. When the RPC ends, user may call Controller.ReadProgressiveAttachmentBy()
//     to read the body. If user does not set a reader, controller sets one
//     ignoring all bytes read before self's destruction.
//     The call chain:
//       Controller.ReadProgressiveAttachmentBy()
//       -> ReadableProgressiveAttachment.ReadProgressiveAttachmentBy()
//       -> HttpMesage.SetBodyReader()
//       -> ProgressiveReader.OnReadOnePart()
//     Already-read body will be fed immediately and the reader is remembered.
//  4. The protocol handler also sets a reference to the RPA in the socket.
//     When new part arrives, HttpMessage.on_body is called, which calls
//     ProgressiveReader.OnReadOnePart() when reader is set.
//  5. When all body is read, the socket releases the reference to the RPA.
//     If controller is deleted after all body is read, the RPA should be
//     destroyed at controller's deletion. If controller is deleted before
//     all body is read, the RPA should be destroyed when all body is read
//     or the socket is destroyed.
class ReadableProgressiveAttachment : public SharedObject {
public:
    // Read the constantly-appending attachment by a ProgressiveReader.
    // Any error occurred should destroy the reader by calling r->Destroy().
    // r->Destroy() should be guaranteed to be called once and only once.
    virtual void ReadProgressiveAttachmentBy(ProgressiveReader* r) = 0;
};

} // namespace brpc


#endif  // BRPC_PROGRESSIVE_READER_H
