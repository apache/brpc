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


#ifndef BRPC_INPUT_MESSENGER_H
#define BRPC_INPUT_MESSENGER_H

#include "butil/iobuf.h"                    // butil::IOBuf
#include "brpc/socket.h"              // SocketId, SocketUser
#include "brpc/parse_result.h"        // ParseResult
#include "brpc/input_message_base.h"  // InputMessageBase


namespace brpc {

struct InputMessageHandler {
    // The callback to cut a message from `source'.
    // Returned message will be passed to process_request or process_response
    // later and Destroy()-ed by them.
    // Returns:
    //   MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA):
    //     `source' does not form a complete message yet.
    //   MakeParseError(PARSE_ERROR_TRY_OTHERS).
    //     `source' does not fit the protocol, the data should be tried by
    //     other protocols. If the data is definitely corrupted (e.g. magic 
    //     header matches but other fields are wrong), pop corrupted part
    //     from `source' before returning.
    //  MakeMessage(InputMessageBase*):
    //     The message is parsed successfully and cut from `source'.
    typedef ParseResult (*Parse)(butil::IOBuf* source, Socket *socket,
                                 bool read_eof, const void *arg);
    Parse parse;
    
    // The callback to handle `msg' created by a successful parse().
    // `msg' must be Destroy()-ed when the processing is done. To make sure
    // Destroy() is always called, consider using DestroyingPtr<> defined in
    // destroyable.h
    // May be called in a different thread from parse().
    typedef void (*Process)(InputMessageBase* msg);
    Process process;

    // The callback to verify authentication of this socket. Only called
    // on the first message that a socket receives. Can be NULL when 
    // authentication is not needed or this is the client side.
    // Returns true on successful authentication.
    typedef bool (*Verify)(const InputMessageBase* msg);
    Verify verify;

    // An argument associated with the handler.
    const void* arg;

    // Name of this handler, must be string constant.
    const char* name;
};

// Process messages from connections.
// `Message' corresponds to a client's request or a server's response.
class InputMessenger : public SocketUser {
public:
    explicit InputMessenger(size_t capacity = 128);
    ~InputMessenger();

    // [thread-safe] Must be called at least once before Start().
    // `handler' contains user-supplied callbacks to cut off and
    // process messages from connections.
    // Returns 0 on success, -1 otherwise.
    int AddHandler(const InputMessageHandler& handler);

    // [thread-safe] Create a socket to process input messages.
    int Create(const butil::EndPoint& remote_side,
               time_t health_check_interval_s,
               SocketId* id);
    // Overwrite necessary fields in `base_options' and create a socket with
    // the modified options.
    int Create(SocketOptions base_options, SocketId* id);

    // Returns the internal index of `InputMessageHandler' whose name=`name'
    // Returns -1 when not found
    int FindProtocolIndex(const char* name) const;
    int FindProtocolIndex(ProtocolType type) const;
    
    // Get name of the n-th handler
    const char* NameOfProtocol(int n) const;

    // Add a handler which doesn't belong to any registered protocol.
    // Note: Invoking this method indicates that you are using Socket without
    // Channel nor Server. 
    int AddNonProtocolHandler(const InputMessageHandler& handler);

protected:
    // Load data from m->fd() into m->read_buf, cut off new messages and
    // call callbacks.
    static void OnNewMessages(Socket* m);
    
private:
    // Find a valid scissor from `handlers' to cut off `header' and `payload'
    // from m->read_buf, save index of the scissor into `index'.
    ParseResult CutInputMessage(Socket* m, size_t* index, bool read_eof);

    // User-supplied scissors and handlers.
    // the index of handler is exactly the same as the protocol
    InputMessageHandler* _handlers;
    // Max added protocol type
    butil::atomic<int> _max_index;
    bool _non_protocol;
    size_t _capacity;

    butil::Mutex _add_handler_mutex;
};

// Get the global InputMessenger at client-side.
BUTIL_FORCE_INLINE InputMessenger* get_client_side_messenger() {
    extern InputMessenger* g_messenger;
    return g_messenger;
}

InputMessenger* get_or_new_client_side_messenger();

} // namespace brpc


#endif  // BRPC_INPUT_MESSENGER_H
