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

#include "butil/logging.h"
#include "butil/binary_printer.h"
#include "bthread/unstable.h"
#include "brpc/options.pb.h"
#include "brpc/transport.h"
#include "brpc/input_messenger_processor.h"
#include "brpc/input_messenger.h"

namespace brpc {

DECLARE_uint64(max_body_size);

const size_t MSG_SIZE_WINDOW = 10;  // Take last so many message into stat.
const size_t MIN_ONCE_READ = 4096;
const size_t MAX_ONCE_READ = 524288;

static const char* StreamTypeName(InputMessengerProcessor::StreamType type) {
    switch (type) {
    case InputMessengerProcessor::STREAM_NONE: return "none";
    case InputMessengerProcessor::STREAM_TCP_FD: return "tcp_fd";
    case InputMessengerProcessor::STREAM_RDMA_QP: return "rdma_qp";
    }
    return "unknown";
}

InputMessengerProcessor::ParsingStreamGuard::ParsingStreamGuard(Socket* socket, StreamType type)
    : _socket(socket) {
    CHECK(socket != nullptr)
        << "Parsing through a processor that was never Init()ed, socket is NULL";
    CHECK_NE(STREAM_NONE, type)
        << "Parsing through a processor that was never Init()ed, " << *socket;
    CHECK_EQ(STREAM_NONE, socket->parsing_stream_type())
        << "Two input streams of " << *socket << " are parsing at the same time: "
        << StreamTypeName(socket->parsing_stream_type()) << " and "
        << StreamTypeName(type);
    socket->set_parsing_stream_type(type);
}

InputMessengerProcessor::ParsingStreamGuard::~ParsingStreamGuard() {
    _socket->set_parsing_stream_type(STREAM_NONE);
}

ParseResult InputMessengerProcessor::CutInputMessage(InputMessenger* messenger,
                                                     size_t* index, bool read_eof) {
    ParsingStreamGuard parsing_stream_guard(_socket, _stream_type);
    InputMessageHandler* handlers = messenger->_handlers;
    int preferred = _socket->preferred_index();
    int max_index = (int)messenger->_max_index.load(butil::memory_order_acquire);
    // Try preferred handler first. The preferred_index is set on last
    // selection or by client.
    if (preferred >= 0 && preferred <= max_index
            && handlers[preferred].parse != nullptr) {
        int cur_index = preferred;
        do {
            ParseResult result =
                handlers[cur_index].parse(&_read_buf, _socket, read_eof,
                                          handlers[cur_index].arg);
            if (result.is_ok() ||
                result.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
                _socket->set_preferred_index(cur_index);
                *index = cur_index;
                return result;
            } else if (result.error() != PARSE_ERROR_TRY_OTHERS) {
                // Critical error, return directly.
                LOG_IF(ERROR, result.error() == PARSE_ERROR_TOO_BIG_DATA)
                    << "A message from " << _socket->remote_side()
                    << "(protocol=" << handlers[cur_index].name
                    << ") is bigger than " << FLAGS_max_body_size
                    << " bytes, the connection will be closed."
                    " Set max_body_size to allow bigger messages";
                return result;
            }

            if (_socket->CreatedByConnect()) {
                if((ProtocolType)cur_index == PROTOCOL_BAIDU_STD && cur_index == preferred) {
                    // baidu_std may fall to streaming_rpc.
                    cur_index = (int)PROTOCOL_STREAMING_RPC;
                    continue;
                } else if((ProtocolType)cur_index == PROTOCOL_STREAMING_RPC &&
                          cur_index == preferred) {
                    // streaming_rpc may fall to baidu_std.
                    cur_index = (int)PROTOCOL_BAIDU_STD;
                    continue;
                } else {
                    // The protocol is fixed at client-side, no need to try others.
                    LOG(ERROR) << "Fail to parse response from " << _socket->remote_side()
                        << " by " << handlers[preferred].name
                        << " at client-side";
                    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
                }
            } else {
                // Try other protocols.
                //
                // A handler may lean on this: returning PARSE_ERROR_NOT_ENOUGH_DATA
                // above keeps `preferred_index' pinned on it, TRY_OTHERS here gives
                // it up. RdmaEndpoint::ExecuteServerHandshake() pins itself that way
                // to keep the last handshake read from being taken for a protocol
                // detection. See the tail of its phase 2 before changing what
                // happens to `preferred_index' here.
                break;
            }
        } while (true);
        // Clear context before trying next protocol which probably has
        // an incompatible context with the current one.
        if (_socket->parsing_context()) {
            _socket->reset_parsing_context(nullptr);
        }
        _socket->set_preferred_index(-1);
    }
    for (int i = 0; i <= max_index; ++i) {
        if (i == preferred || handlers[i].parse == nullptr) {
            // Don't try preferred handler(already tried) or invalid handler
            continue;
        }
        ParseResult result = handlers[i].parse(&_read_buf, _socket, read_eof, handlers[i].arg);
        if (result.is_ok() ||
            result.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
            _socket->set_preferred_index(i);
            *index = i;
            return result;
        } else if (result.error() != PARSE_ERROR_TRY_OTHERS) {
            // Critical error, return directly.
            LOG_IF(ERROR, result.error() == PARSE_ERROR_TOO_BIG_DATA)
                << "A message from " << _socket->remote_side()
                << "(protocol=" << handlers[i].name
                << ") is bigger than " << FLAGS_max_body_size
                << " bytes, the connection will be closed."
                " Set max_body_size to allow bigger messages";
            return result;
        }
        // Clear context before trying next protocol which definitely has
        // an incompatible context with the current one.
        if (_socket->parsing_context()) {
            _socket->reset_parsing_context(nullptr);
        }
        // Try other protocols.
    }
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

size_t InputMessengerProcessor::OnceReadSize() const {
    size_t once_read = _avg_msg_size * 16;
    if (once_read < MIN_ONCE_READ) {
        once_read = MIN_ONCE_READ;
    } else if (once_read > MAX_ONCE_READ) {
        once_read = MAX_ONCE_READ;
    }
    return once_read;
}

void InputMessengerProcessor::Reset() {
    _read_buf.clear();
    _last_msg_size = 0;
    _avg_msg_size = 0;
}

int InputMessengerProcessor::ProcessNewMessage(ssize_t bytes, bool read_eof,
                                               uint64_t received_us,
                                               uint64_t base_realtime,
                                               InputMessageClosure& last_msg) {
    auto messenger = static_cast<InputMessenger*>(_socket->user());
    const InputMessageHandler* handlers = messenger->_handlers;
    _socket->AddInputBytes(bytes);

    // Avoid this socket to be closed due to idle_timeout_s
    _socket->_last_readtime_us.store(received_us, butil::memory_order_relaxed);

    size_t last_size = _read_buf.length();
    int num_bthread_created = 0;
    while (true) {
        size_t index = 8888;
        ParseResult pr = CutInputMessage(messenger, &index, read_eof);
        if (!pr.is_ok()) {
            if (pr.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
                // incomplete message, re-read.
                // However, some buffer may have been consumed
                // under protocols like HTTP. Record this size
                _last_msg_size += (last_size - _read_buf.length());
                break;
            } else if (pr.error() == PARSE_ERROR_TRY_OTHERS) {
                LOG(WARNING) << "Close " << *_socket << " due to unknown message: "
                             << butil::ToPrintable(_read_buf);
                _socket->SetFailed(EINVAL, "Close %s due to unknown message",
                                   _socket->description().c_str());
                return -1;
            } else {
                LOG(WARNING) << "Close " << *_socket << ": " << pr.error_str();
                _socket->SetFailed(EINVAL, "Close %s: %s",
                                   _socket->description().c_str(), pr.error_str());
                return -1;
            }
        }

        _socket->AddInputMessages(1);
        // Calculate average size of messages
        const size_t cur_size = _read_buf.length();
        if (cur_size == 0) {
            // _read_buf is consumed, it's good timing to return blocks
            // cached internally back to TLS, otherwise the memory is not
            // reused until next message arrives which is quite uncertain
            // in situations that most connections are idle.
            _read_buf.return_cached_blocks();
        }
        _last_msg_size += (last_size - cur_size);
        last_size = cur_size;
        const size_t old_avg = _avg_msg_size;
        if (old_avg != 0) {
            _avg_msg_size = (old_avg * (MSG_SIZE_WINDOW - 1) + _last_msg_size) / MSG_SIZE_WINDOW;
        } else {
            _avg_msg_size = _last_msg_size;
        }
        _last_msg_size = 0;

        if (pr.message() == nullptr) { // the Process() step can be skipped.
            continue;
        }
        pr.message()->_received_us = received_us;
        pr.message()->_base_real_us = base_realtime;

        // This unique_ptr prevents msg to be lost before transfering
        // ownership to last_msg
        DestroyingPtr<InputMessageBase> msg(pr.message());
        _socket->_transport->QueueMessage(last_msg, &num_bthread_created, false);
        if (handlers[index].process == nullptr) {
            LOG(ERROR) << "process of index=" << index << " is NULL";
            continue;
        }
        _socket->ReAddress(&msg->_socket);
        _socket->PostponeEOF();
        msg->_process = handlers[index].process;
        msg->_arg = handlers[index].arg;

        if (handlers[index].verify != nullptr) {
            int auth_error = 0;
            if (0 == _socket->FightAuthentication(&auth_error)) {
                // Get the right to authenticate
                if (handlers[index].verify(msg.get())) {
                    _socket->SetAuthentication(0);
                } else {
                    _socket->SetAuthentication(ERPCAUTH);
                    LOG(WARNING) << "Fail to authenticate " << *_socket;
                    _socket->SetFailed(ERPCAUTH, "Fail to authenticate %s",
                                    _socket->description().c_str());
                    return -1;
                }
            } else {
                LOG_IF(FATAL, auth_error != 0) <<
                    "Impossible! Socket should have been "
                    "destroyed when authentication failed";
            }
        }
        if (!_socket->is_read_progressive()) {
            // Transfer ownership to last_msg
            last_msg.reset(msg.release());
        } else {
            last_msg.reset(msg.release());
            _socket->_transport->QueueMessage(last_msg, &num_bthread_created, false);
            bthread_flush();
            num_bthread_created = 0;
        }
    }
    // In RDMA polling mode, all messages must be executed in a new bthread and
    // not in the bthread where the polling bthread is located, because the
    // method for processing messages may call synchronization primitives,
    // causing the polling bthread to be scheduled out.
    if (_socket->_socket_mode == SOCKET_MODE_RDMA ||
        _socket->_socket_mode == SOCKET_MODE_UBRING) {
        _socket->_transport->QueueMessage(last_msg, &num_bthread_created, true);
    }
    if (num_bthread_created) {
        bthread_flush();
    }
    return 0;
}

} // namespace brpc
