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


#include <gflags/gflags.h>
#include "butil/fd_guard.h"                      // fd_guard
#include "butil/logging.h"                       // CHECK
#include "butil/time.h"                          // cpuwide_time_us
#include "butil/fd_utility.h"                    // make_non_blocking
#include "bthread/bthread.h"                     // bthread_start_background
#include "bthread/unstable.h"                   // bthread_flush
#include "bvar/bvar.h"                          // bvar::Adder
#include "brpc/options.pb.h"               // ProtocolType
#include "brpc/reloadable_flags.h"         // BRPC_VALIDATE_GFLAG
#include "brpc/protocol.h"                 // ListProtocols
#include "brpc/input_messenger.h"


namespace brpc {

InputMessenger* g_messenger = NULL;
static pthread_once_t g_messenger_init = PTHREAD_ONCE_INIT;
static void InitClientSideMessenger() {
    g_messenger = new InputMessenger;
}
InputMessenger* get_or_new_client_side_messenger() {
    pthread_once(&g_messenger_init, InitClientSideMessenger);
    return g_messenger;
}

static ProtocolType FindProtocolOfHandler(const InputMessageHandler& h);

// NOTE: This flag was true by default before r31206. But since we have
// /connections to view all the active connections, logging closing does not
// help much to locate problems, and crashings are unlikely to be associated
// with an EOF.
DEFINE_bool(log_connection_close, false,
            "Print log when remote side closes the connection");
BRPC_VALIDATE_GFLAG(log_connection_close, PassValidate);

DECLARE_bool(usercode_in_pthread);
DECLARE_uint64(max_body_size);

const size_t MSG_SIZE_WINDOW = 10;  // Take last so many message into stat.
const size_t MIN_ONCE_READ = 4096;
const size_t MAX_ONCE_READ = 524288;

ParseResult InputMessenger::CutInputMessage(
        Socket* m, size_t* index, bool read_eof) {
    const int preferred = m->preferred_index();
    const int max_index = (int)_max_index.load(butil::memory_order_acquire);
    // Try preferred handler first. The preferred_index is set on last
    // selection or by client.
    if (preferred >= 0 && preferred <= max_index
            && _handlers[preferred].parse != NULL) {
        int cur_index = preferred;
        do {
            ParseResult result =
                _handlers[cur_index].parse(&m->_read_buf, m, read_eof, _handlers[cur_index].arg);
            if (result.is_ok() ||
                result.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
                m->set_preferred_index(cur_index);
                *index = cur_index;
                return result;
            } else if (result.error() != PARSE_ERROR_TRY_OTHERS) {
                // Critical error, return directly.
                LOG_IF(ERROR, result.error() == PARSE_ERROR_TOO_BIG_DATA)
                    << "A message from " << m->remote_side()
                    << "(protocol=" << _handlers[cur_index].name
                    << ") is bigger than " << FLAGS_max_body_size
                    << " bytes, the connection will be closed."
                    " Set max_body_size to allow bigger messages";
                return result;
            }

            if (m->CreatedByConnect()) {
                if((ProtocolType)cur_index == PROTOCOL_BAIDU_STD) {
                    // baidu_std may fall to streaming_rpc.
                    cur_index = (int)PROTOCOL_STREAMING_RPC;
                    continue;
                } else {
                    // The protocol is fixed at client-side, no need to try others.
                    LOG(ERROR) << "Fail to parse response from " << m->remote_side()
                        << " by " << _handlers[preferred].name 
                        << " at client-side";
                    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
                }
            } else {
                // Try other protocols.
                break;
            }
        } while (true);
        // Clear context before trying next protocol which probably has
        // an incompatible context with the current one.
        if (m->parsing_context()) {
            m->reset_parsing_context(NULL);
        }
        m->set_preferred_index(-1);
    }
    for (int i = 0; i <= max_index; ++i) {
        if (i == preferred || _handlers[i].parse == NULL) {
            // Don't try preferred handler(already tried) or invalid handler
            continue;
        }
        ParseResult result = _handlers[i].parse(&m->_read_buf, m, read_eof, _handlers[i].arg);
        if (result.is_ok() ||
            result.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
            m->set_preferred_index(i);
            *index = i;
            return result;
        } else if (result.error() != PARSE_ERROR_TRY_OTHERS) {
            // Critical error, return directly.
            LOG_IF(ERROR, result.error() == PARSE_ERROR_TOO_BIG_DATA)
                << "A message from " << m->remote_side()
                << "(protocol=" << _handlers[i].name
                << ") is bigger than " << FLAGS_max_body_size
                << " bytes, the connection will be closed."
                " Set max_body_size to allow bigger messages";
            return result;
        }
        // Clear context before trying next protocol which definitely has
        // an incompatible context with the current one.
        if (m->parsing_context()) {
            m->reset_parsing_context(NULL);
        }
        // Try other protocols.
    }
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

void* ProcessInputMessage(void* void_arg) {
    InputMessageBase* msg = static_cast<InputMessageBase*>(void_arg);
    msg->_process(msg);
    return NULL;
}

struct RunLastMessage {
    inline void operator()(InputMessageBase* last_msg) {
        ProcessInputMessage(last_msg);
    }
};

static void QueueMessage(InputMessageBase* to_run_msg,
                         int* num_bthread_created,
                         bthread_keytable_pool_t* keytable_pool) {
    if (!to_run_msg) {
        return;
    }
    // Create bthread for last_msg. The bthread is not scheduled
    // until bthread_flush() is called (in the worse case).
                
    // TODO(gejun): Join threads.
    bthread_t th;
    bthread_attr_t tmp = (FLAGS_usercode_in_pthread ?
                          BTHREAD_ATTR_PTHREAD :
                          BTHREAD_ATTR_NORMAL) | BTHREAD_NOSIGNAL;
    tmp.keytable_pool = keytable_pool;
    if (bthread_start_background(
            &th, &tmp, ProcessInputMessage, to_run_msg) == 0) {
        ++*num_bthread_created;
    } else {
        ProcessInputMessage(to_run_msg);
    }
}

void InputMessenger::OnNewMessages(Socket* m) {
    // Notes:
    // - If the socket has only one message, the message will be parsed and
    //   processed in this bthread. nova-pbrpc and http works in this way.
    // - If the socket has several messages, all messages will be parsed (
    //   meaning cutting from butil::IOBuf. serializing from protobuf is part of
    //   "process") in this bthread. All messages except the last one will be
    //   processed in separate bthreads. To minimize the overhead, scheduling
    //   is batched(notice the BTHREAD_NOSIGNAL and bthread_flush).
    // - Verify will always be called in this bthread at most once and before
    //   any process.
    InputMessenger* messenger = static_cast<InputMessenger*>(m->user());
    const InputMessageHandler* handlers = messenger->_handlers;
    int progress = Socket::PROGRESS_INIT;

    // Notice that all *return* no matter successful or not will run last
    // message, even if the socket is about to be closed. This should be
    // OK in most cases.
    std::unique_ptr<InputMessageBase, RunLastMessage> last_msg;
    bool read_eof = false;
    while (!read_eof) {
        const int64_t received_us = butil::cpuwide_time_us();
        const int64_t base_realtime = butil::gettimeofday_us() - received_us;

        // Calculate bytes to be read.
        size_t once_read = m->_avg_msg_size * 16;
        if (once_read < MIN_ONCE_READ) {
            once_read = MIN_ONCE_READ;
        } else if (once_read > MAX_ONCE_READ) {
            once_read = MAX_ONCE_READ;
        }

        // Read.
        const ssize_t nr = m->DoRead(once_read);
        if (nr <= 0) {
            if (0 == nr) {
                // Set `read_eof' flag and proceed to feed EOF into `Protocol'
                // (implied by m->_read_buf.empty), which may produce a new
                // `InputMessageBase' under some protocols such as HTTP
                LOG_IF(WARNING, FLAGS_log_connection_close) << *m << " was closed by remote side";
                read_eof = true;                
            } else if (errno != EAGAIN) {
                if (errno == EINTR) {
                    continue;  // just retry
                }
                const int saved_errno = errno;
                PLOG(WARNING) << "Fail to read from " << *m;
                m->SetFailed(saved_errno, "Fail to read from %s: %s",
                             m->description().c_str(), berror(saved_errno));
                return;
            } else if (!m->MoreReadEvents(&progress)) {
                return;
            } else { // new events during processing
                continue;
            }
        }
        
        m->AddInputBytes(nr);

        // Avoid this socket to be closed due to idle_timeout_s
        m->_last_readtime_us.store(received_us, butil::memory_order_relaxed);
        
        size_t last_size = m->_read_buf.length();
        int num_bthread_created = 0;
        while (1) {
            size_t index = 8888;
            ParseResult pr = messenger->CutInputMessage(m, &index, read_eof);
            if (!pr.is_ok()) {
                if (pr.error() == PARSE_ERROR_NOT_ENOUGH_DATA) {
                    // incomplete message, re-read.
                    // However, some buffer may have been consumed
                    // under protocols like HTTP. Record this size
                    m->_last_msg_size += (last_size - m->_read_buf.length());
                    break;
                } else if (pr.error() == PARSE_ERROR_TRY_OTHERS) {
                    LOG(WARNING)
                        << "Close " << *m << " due to unknown message: "
                        << butil::ToPrintable(m->_read_buf);
                    m->SetFailed(EINVAL, "Close %s due to unknown message",
                                 m->description().c_str());
                    return;
                } else {
                    LOG(WARNING) << "Close " << *m << ": " << pr.error_str();
                    m->SetFailed(EINVAL, "Close %s: %s",
                                 m->description().c_str(), pr.error_str());
                    return;
                }
            }

            m->AddInputMessages(1);
            // Calculate average size of messages
            const size_t cur_size = m->_read_buf.length();
            if (cur_size == 0) {
                // _read_buf is consumed, it's good timing to return blocks
                // cached internally back to TLS, otherwise the memory is not
                // reused until next message arrives which is quite uncertain
                // in situations that most connections are idle.
                m->_read_buf.return_cached_blocks();
            }
            m->_last_msg_size += (last_size - cur_size);
            last_size = cur_size;
            const size_t old_avg = m->_avg_msg_size;
            if (old_avg != 0) {
                m->_avg_msg_size = (old_avg * (MSG_SIZE_WINDOW - 1) + m->_last_msg_size)
                / MSG_SIZE_WINDOW;
            } else {
                m->_avg_msg_size = m->_last_msg_size;
            }
            m->_last_msg_size = 0;
            
            if (pr.message() == NULL) { // the Process() step can be skipped.
                continue;
            }
            pr.message()->_received_us = received_us;
            pr.message()->_base_real_us = base_realtime;
                        
            // This unique_ptr prevents msg to be lost before transfering
            // ownership to last_msg
            DestroyingPtr<InputMessageBase> msg(pr.message());
            QueueMessage(last_msg.release(), &num_bthread_created,
                             m->_keytable_pool);
            if (handlers[index].process == NULL) {
                LOG(ERROR) << "process of index=" << index << " is NULL";
                continue;
            }
            m->ReAddress(&msg->_socket);
            m->PostponeEOF();
            msg->_process = handlers[index].process;
            msg->_arg = handlers[index].arg;
            
            if (handlers[index].verify != NULL) {
                int auth_error = 0;
                if (0 == m->FightAuthentication(&auth_error)) {
                    // Get the right to authenticate
                    if (handlers[index].verify(msg.get())) {
                        m->SetAuthentication(0);
                    } else {
                        m->SetAuthentication(ERPCAUTH);
                        LOG(WARNING) << "Fail to authenticate " << *m;
                        m->SetFailed(ERPCAUTH, "Fail to authenticate %s",
                                     m->description().c_str());
                        return;
                    }
                } else {
                    LOG_IF(FATAL, auth_error != 0) <<
                      "Impossible! Socket should have been "
                      "destroyed when authentication failed";
                }
            }
            if (!m->is_read_progressive()) {
                // Transfer ownership to last_msg
                last_msg.reset(msg.release());
            } else {
                QueueMessage(msg.release(), &num_bthread_created,
                                 m->_keytable_pool);
                bthread_flush();
                num_bthread_created = 0;
            }
        }
        if (num_bthread_created) {
            bthread_flush();
        }
    }

    if (read_eof) {
        m->SetEOF();
    }
}

InputMessenger::InputMessenger(size_t capacity)
    : _handlers(NULL)
    , _max_index(-1)
    , _non_protocol(false)
    , _capacity(capacity) {
}

InputMessenger::~InputMessenger() {
    delete[] _handlers;
    _handlers = NULL;        
    _max_index.store(-1, butil::memory_order_relaxed);
    _capacity = 0;
}

int InputMessenger::AddHandler(const InputMessageHandler& handler) {
    if (handler.parse == NULL || handler.process == NULL 
            || handler.name == NULL) {
        CHECK(false) << "Invalid argument";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_add_handler_mutex);
    if (NULL == _handlers) {
        _handlers = new (std::nothrow) InputMessageHandler[_capacity];
        if (NULL == _handlers) {
            LOG(FATAL) << "Fail to new array of InputMessageHandler";
            return -1;
        }
        memset(_handlers, 0, sizeof(*_handlers) * _capacity);
        _non_protocol = false;
    }
    if (_non_protocol) {
        CHECK(false) << "AddNonProtocolHandler was invoked";
        return -1;
    }
    ProtocolType type = FindProtocolOfHandler(handler);
    if (type == PROTOCOL_UNKNOWN) {
        CHECK(false) << "Adding a handler which doesn't belong to any protocol";
        return -1;
    }
    const int index = type;
    if (index >= (int)_capacity) {
        LOG(FATAL) << "Can't add more handlers than " << _capacity;
        return -1;
    }
    if (_handlers[index].parse == NULL) {
        // The same protocol might be added more than twice
        _handlers[index] = handler;
    } else if (_handlers[index].parse != handler.parse 
               || _handlers[index].process != handler.process) {
        CHECK(_handlers[index].parse == handler.parse);
        CHECK(_handlers[index].process == handler.process);
        return -1;
    }
    if (index > _max_index.load(butil::memory_order_relaxed)) {
        _max_index.store(index, butil::memory_order_release);
    }
    return 0;
}

int InputMessenger::AddNonProtocolHandler(const InputMessageHandler& handler) {
    if (handler.parse == NULL || handler.process == NULL 
            || handler.name == NULL) {
        CHECK(false) << "Invalid argument";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_add_handler_mutex);
    if (NULL == _handlers) {
        _handlers = new (std::nothrow) InputMessageHandler[_capacity];
        if (NULL == _handlers) {
            LOG(FATAL) << "Fail to new array of InputMessageHandler";
            return -1;
        }
        memset(_handlers, 0, sizeof(*_handlers) * _capacity);
        _non_protocol = true;
    }
    if (!_non_protocol) {
        CHECK(false) << "AddHandler was invoked";
        return -1;
    }
    const int index = _max_index.load(butil::memory_order_relaxed) + 1;
    _handlers[index] = handler;
    _max_index.store(index, butil::memory_order_release);
    return 0;
}

int InputMessenger::Create(const butil::EndPoint& remote_side,
                           time_t health_check_interval_s,
                           SocketId* id) {
    SocketOptions options;
    options.remote_side = remote_side;
    options.user = this;
    options.on_edge_triggered_events = OnNewMessages;
    options.health_check_interval_s = health_check_interval_s;
    return Socket::Create(options, id);
}

int InputMessenger::Create(SocketOptions options, SocketId* id) {
    options.user = this;
    options.on_edge_triggered_events = OnNewMessages;
    return Socket::Create(options, id);
}

int InputMessenger::FindProtocolIndex(const char* name) const {
    for (size_t i = 0; i < _capacity; ++i) {
        if (_handlers[i].parse != NULL 
                && strcmp(name, _handlers[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

int InputMessenger::FindProtocolIndex(ProtocolType type) const {
    const Protocol* proto = FindProtocol(type);
    if (NULL == proto) {
        return -1;
    }
    return FindProtocolIndex(proto->name);

}

const char* InputMessenger::NameOfProtocol(int n) const {
    if (n < 0 || (size_t)n >= _capacity || _handlers[n].parse == NULL) {
        return "unknown";  // use lowercase to be consistent with valid names.
    }
    return _handlers[n].name;
}

static ProtocolType FindProtocolOfHandler(const InputMessageHandler& h) {
    std::vector<std::pair<ProtocolType, Protocol> > vec;
    ListProtocols(&vec);
    for (size_t i = 0; i < vec.size(); ++i) {
        if (vec[i].second.parse == h.parse &&
                ((vec[i].second.process_request == h.process)  
                                        //      ^^ server side
                 || (vec[i].second.process_response == h.process))
                                                 // ^^ client side
                && strcmp(vec[i].second.name, h.name) == 0) { 
            return vec[i].first;
        }
    }
    return PROTOCOL_UNKNOWN;
}

void InputMessageBase::Destroy() {
    // Release base-specific resources.
    if (_socket) {
        _socket->CheckEOF();
        _socket.reset();
    }
    DestroyImpl();
    // This object may be destroyed, don't touch fields anymore.
}

Socket* InputMessageBase::ReleaseSocket() {
    Socket* s = _socket.release();
    if (s) {
        s->CheckEOF();
    }
    return s;
}

InputMessageBase::~InputMessageBase() {}

} // namespace brpc
