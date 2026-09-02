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


#ifndef BRPC_INPUT_MESSENGER_PROCESSOR_H_
#define BRPC_INPUT_MESSENGER_PROCESSOR_H_

#include "butil/iobuf.h"                    // butil::IOPortal
#include "butil/logging.h"                   // DCHECK
#include "butil/macros.h"                   // DISALLOW_COPY_AND_ASSIGN
#include "brpc/parse_result.h"        // ParseResult

namespace brpc {

class Socket;
class InputMessenger;
class InputMessageClosure;

// The state of one input stream: the data read but not cut off yet, and the
// message-size statistics used to size the next read.
class InputMessengerProcessor {
public:
    // Which stream of a Socket a processor drains.
    enum StreamType {
        STREAM_NONE,
        STREAM_TCP_FD,
        STREAM_RDMA_QP,
    };

    InputMessengerProcessor()
        : _socket(nullptr), _stream_type(STREAM_NONE)
        , _last_msg_size(0), _avg_msg_size(0) {}

    DISALLOW_COPY_AND_ASSIGN(InputMessengerProcessor);

    void Init(Socket* socket, StreamType type) {
        DCHECK(socket != nullptr);
        DCHECK_NE(STREAM_NONE, type) << "STREAM_NONE is not a stream";
        _socket = socket;
        _stream_type = type;
    }

    // Cut off and process all complete messages in read_buf(), which just
    // grew by `bytes` bytes.
    // Returns 0 on success, -1 otherwise.
    int ProcessNewMessage(ssize_t bytes, bool read_eof,
                          uint64_t received_us,
                          uint64_t base_realtime,
                          InputMessageClosure& last_msg);

    // Data read from the stream but not cut off yet. Only the bthread
    // draining this particular stream may touch it.
    butil::IOPortal& read_buf() { return _read_buf; }
    const butil::IOPortal& read_buf() const { return _read_buf; }

    // How many bytes to ask for on the next read, derived from the sizes of
    // the messages seen recently.
    size_t OnceReadSize() const;

    uint32_t avg_msg_size() const { return _avg_msg_size; }

    // Drop buffered data and reset the statistics.
    void Reset();

    // Reset the message-size statistics only, keeping buffered data.
    void ResetMsgSizeStats() { _last_msg_size = 0; _avg_msg_size = 0; }

private:

    // Publishes on the Socket which stream the parse callbacks are cutting
    // from and clears it when they return, which is what makes
    // Socket::parsing_stream_type() mean "the stream being parsed right now".
    //
    // Entering also DCHECKs that no other stream of that Socket is parsing --
    // the one-stream-at-a-time rule above, checked instead of assumed -- and
    // that the processor was Init()ed.
    //
    // Nested, with Socket::set_parsing_stream_type() private to the enclosing
    // class, so nothing else can publish and nothing can forget to unpublish.
    class ParsingStreamGuard {
    public:
        ParsingStreamGuard(Socket* socket, StreamType type);
        DISALLOW_COPY_AND_ASSIGN(ParsingStreamGuard);
        ~ParsingStreamGuard();
    private:
        Socket* _socket;
    };

    // Find a valid scissor among messenger's handlers to cut off one message
    // from `_read_buf`, save the index of the scissor into `index`.
    ParseResult CutInputMessage(InputMessenger* messenger, size_t* index, bool read_eof);

    // The Socket this stream belongs to. Not owned.
    Socket* _socket;

    // Which of that Socket's streams this is, never STREAM_NONE once Init()ed.
    StreamType _stream_type;

    butil::IOPortal _read_buf;

    // Size of current incomplete message, set to 0 on complete.
    uint32_t _last_msg_size;
    // Average message size of last #MSG_SIZE_WINDOW messages (roughly)
    uint32_t _avg_msg_size;
};

} // namespace brpc

#endif  // BRPC_INPUT_MESSENGER_PROCESSOR_H_
