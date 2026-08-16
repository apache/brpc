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


#include "brpc/stream.h"

#include <gflags/gflags.h>
#include "butil/time.h"
#include "butil/object_pool.h"
#include "butil/unique_ptr.h"
#include "bthread/unstable.h"
#include "brpc/log.h"
#include "brpc/socket.h"
#include "brpc/controller.h"
#include "brpc/input_messenger.h"
#include "brpc/policy/streaming_rpc_protocol.h"
#include "brpc/policy/baidu_rpc_protocol.h"
#include "brpc/stream_impl.h"


namespace brpc {

DECLARE_bool(usercode_in_pthread);
DECLARE_int64(socket_max_streams_unconsumed_bytes);
DEFINE_uint64(stream_write_max_segment_size, 512 * 1024 * 1024,
              "Stream message exceeding this size will be automatically split into smaller segments");
BRPC_VALIDATE_GFLAG(stream_write_max_segment_size, PositiveInteger);

const static butil::IOBuf *TIMEOUT_TASK = (butil::IOBuf*)-1L;

Stream::Stream(Forbidden f)
    : VersionedRefWithId<Stream>(f)
    , _host_socket(NULL)
    , _connected(false)
    , _error_code(0)
    , _produced(0)
    , _remote_consumed(0)
    , _socket_unconsumed_size(0)
    , _cur_buf_size(0)
    , _local_consumed(0)
    , _atomic_local_consumed(0)
    , _parse_rpc_response(false)
    , _pending_buf(NULL)
    , _start_idle_timer_us(0)
    , _idle_timer(0) {
    CHECK_EQ(0, bthread_mutex_init(&_connect_mutex, NULL));
    CHECK_EQ(0, bthread_mutex_init(&_congestion_control_mutex, NULL));
}

Stream::~Stream() {
    // Clear pending buffer
    if (_pending_buf != NULL) {
        delete _pending_buf;
        _pending_buf = NULL;
    }
    CHECK(_host_socket == NULL);
    bthread_mutex_destroy(&_connect_mutex);
    bthread_mutex_destroy(&_congestion_control_mutex);
}

int Stream::Create(const StreamOptions &options, 
                   const StreamSettings* remote_settings,
                   StreamId *id, bool parse_rpc_response) {
    return VersionedRefWithId<Stream>::Create(
        id, options, remote_settings, parse_rpc_response);
}

int Stream::OnCreated(const StreamOptions& options,
                      const StreamSettings* remote_settings,
                      bool parse_rpc_response) {
    _host_socket = NULL;
    _connected.store(false, butil::memory_order_relaxed);
    _options = options;
    _error_code = 0;
    _error_text.clear();
    _pending_writes.clear();
    _produced = 0;
    _remote_consumed = 0;
    _socket_unconsumed_size = 0;
    _local_consumed = 0;
    _atomic_local_consumed.store(0, butil::memory_order_relaxed);
    _parse_rpc_response = parse_rpc_response;
    _pending_buf = NULL;
    _start_idle_timer_us = 0;
    _idle_timer = 0;
    _remote_settings.Clear();

    _cur_buf_size = options.max_buf_size > 0 ? options.max_buf_size : 0;
    if (options.max_buf_size > 0 && options.min_buf_size > options.max_buf_size) {
        // set 0 if min_buf_size is invalid.
        _options.min_buf_size = 0;
        LOG(WARNING) << "options.min_buf_size is larger than options.max_buf_size, it will be set to 0.";
    }
    if (FLAGS_socket_max_streams_unconsumed_bytes > 0 && _options.min_buf_size > 0) {
        _cur_buf_size = _options.min_buf_size;
    }

    if (remote_settings != NULL) {
        _remote_settings.MergeFrom(*remote_settings);
    }

    CHECK_EQ(0, bthread_id_list_init(&_writable_wait_list, 8, 8/*FIXME*/));

    bthread::ExecutionQueueOptions q_opt;
    q_opt.bthread_attr 
        = FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
    if (bthread::execution_queue_start(&_consumer_queue, &q_opt, Consume, this) != 0) {
        LOG(FATAL) << "Fail to create ExecutionQueue";
        return -1;
    }

    // The consumer queue holds one reference to this Stream.
    AddReference();
    return 0;
}

void Stream::OnFailed(int error_code, const std::string& error_text) {
    bool connected = false;
    {
        // Record the error for on_failed callback fired in Consume(), and discard
        // any writes buffered before connecting.
        BAIDU_SCOPED_LOCK(_connect_mutex);
        _error_code = error_code;
        _error_text = error_text;
        connected = _connected.load(butil::memory_order_relaxed);
        _pending_writes.clear();
    }

    // Wake up all threads blocked on writable.
    bthread_id_list_reset(&_writable_wait_list, ECONNRESET);

    // Serialize the host Socket membership removal with SetHostSocket().
    // SetFailed() marks this Stream failed before entering OnFailed(), so a
    // later SetHostSocket() observes Failed() and cannot add it back.
    {
        BAIDU_SCOPED_LOCK(_connect_mutex);
        if (connected) {
            RPC_VLOG << "Send close frame";
            CHECK(_host_socket != NULL);
            policy::SendStreamClose(
                _host_socket, _remote_settings.stream_id(), id());
        }
        if (_host_socket != NULL) {
            if (FLAGS_socket_max_streams_unconsumed_bytes > 0) {
                BAIDU_SCOPED_LOCK(_congestion_control_mutex);
                if (_socket_unconsumed_size != 0) {
                    _host_socket->_total_streams_unconsumed_size.fetch_sub(
                        _socket_unconsumed_size, butil::memory_order_relaxed);
                    _socket_unconsumed_size = 0;
                }
            }
            _host_socket->RemoveStream(id());
        }
    }

    // Stop the consumer queue. Consume() will fire on_failed/on_closed and
    // release the reference held by the queue, which may recycle this instance.
    bthread::execution_queue_stop(_consumer_queue);
}

void Stream::BeforeRecycled() {
    if (_pending_buf != NULL) {
        delete _pending_buf;
        _pending_buf = NULL;
    }

    _pending_writes.clear();
    bthread_id_list_destroy(&_writable_wait_list);
    if (_host_socket != NULL) {
        DereferenceSocket(_host_socket);
        _host_socket = NULL;
    }
}

std::string Stream::OnDescription() const {
    BAIDU_SCOPED_LOCK(_connect_mutex);
    if (_host_socket != NULL) {
        return _host_socket->description();
    } else {
        return "host_socket=NULL";
    }
}

int Stream::WritePacked(const butil::IOBuf& data,
                        const StreamWriteOptions* options) {
    if (_host_socket == NULL) {
        CHECK(false) << "Not connected";
        errno = EBADF;
        return -1;
    }
    if (!_remote_settings.writable()) {
        LOG(WARNING) << "The remote side of Stream=" << id()
                     << "->" << _remote_settings.stream_id()
                     << "@" << _host_socket->remote_side()
                     << " doesn't have a handler";
        errno = EBADF;
        return -1;
    }

    Socket::WriteOptions wopt;
    wopt.write_in_background = options != NULL && options->write_in_background;

    // Pack the whole message (splitting large data into multiple STRM frames)
    // into a SINGLE IOBuf, then hand it to Socket::Write in one shot.
    butil::IOBuf remaining(data);
    butil::IOBuf out;
    bool has_continuation = true;
    do {
        butil::IOBuf segment;
        remaining.cutn(&segment, FLAGS_stream_write_max_segment_size);
        has_continuation = !remaining.empty();
        StreamFrameMeta fm;
        fm.set_stream_id(_remote_settings.stream_id());
        fm.set_source_stream_id(id());
        fm.set_frame_type(FRAME_TYPE_DATA);
        fm.set_has_continuation(has_continuation);
        policy::PackStreamMessage(&out, fm, &segment);
    } while (has_continuation);

    if (BRPC_HANDLE_EOVERCROWDED(_host_socket->Write(&out, &wopt)) != 0) {
        // Stream may be closed by peer before.
        LOG(WARNING) << "Fail to write to host socket of stream=" << id()
                     << ", " << berror();
        return -1;
    }
    return 0;
}

void Stream::WriteToHostSocket(butil::IOBuf* b) {
    BRPC_HANDLE_EOVERCROWDED(_host_socket->Write(b));
}

inline void Stream::RollbackProduced(size_t data_length) {
    if (_cur_buf_size > 0) {
        BAIDU_SCOPED_LOCK(_congestion_control_mutex);
        _produced -= data_length;
    }
}

int Stream::AppendIfNotFull(const butil::IOBuf &data,
                            const StreamWriteOptions* options) {
    if (Failed()) {
        errno = ECONNRESET;
        return -1;
    }

    size_t data_length = data.length();
    if (_cur_buf_size > 0) {
        std::unique_lock<bthread_mutex_t> lck(_congestion_control_mutex);
        if (_produced >= _remote_consumed + _cur_buf_size) {
            const size_t saved_produced = _produced;
            const size_t saved_remote_consumed = _remote_consumed;
            lck.unlock();
            RPC_VLOG << "Stream=" << id() << " is full"
                     << "_produced=" << saved_produced
                     << " _remote_consumed=" << saved_remote_consumed
                     << " gap=" << saved_produced - saved_remote_consumed
                     << " max_buf_size=" << _cur_buf_size;
            return 1;
        }
        _produced += data_length;
    }

    // Fast path (the common case): once connected, write directly WITHOUT
    // taking _connect_mutex. `_connected` is a one-way transition published
    // by SetConnected() after flushing pending writes, so ordering is preserved
    // and this path stays lock-free (besides the optional congestion window).
    if (_connected.load(butil::memory_order_acquire)) {
        if (WritePacked(data, options) != 0) {
            RollbackProduced(data_length);
            return -1;
        }
        if (FLAGS_socket_max_streams_unconsumed_bytes > 0) {
            BAIDU_SCOPED_LOCK(_congestion_control_mutex);
            if (!Failed()) {
                _host_socket->_total_streams_unconsumed_size.fetch_add(
                    data_length, butil::memory_order_relaxed);
                _socket_unconsumed_size += data_length;
            }
        }
        return 0;
    }

    // Slow path (rare): not connected yet, so the remote stream id is unknown.
    // Buffer the raw data and options under `_connect_mutex`. SetConnected()
    // will flush it.
    {
        BAIDU_SCOPED_LOCK(_connect_mutex);
        if (Failed()) {
            RollbackProduced(data_length);
            return -1;
        }
        if (!_connected.load(butil::memory_order_acquire)) {
            _pending_writes.emplace_back(data, options);
            return 0;
        }
        // Connected between the two checks; fall through to a direct write.
        // Ordering holds: reaching here means we acquired `_connect_mutex`,
        // which SetConnected() releases only after it has flushed all pending
        // writes (enqueued into the host socket) and published `_connected=true`.
        // Hence, this direct write is necessarily enqueued after those pending writes.
    }

    if (WritePacked(data, options) != 0) {
        RollbackProduced(data_length);
        return -1;
    }
    if (FLAGS_socket_max_streams_unconsumed_bytes > 0) {
        BAIDU_SCOPED_LOCK(_congestion_control_mutex);
        if (!Failed()) {
            _host_socket->_total_streams_unconsumed_size.fetch_add(
                data_length, butil::memory_order_relaxed);
            _socket_unconsumed_size += data_length;
        }
    }
    return 0;
}

void Stream::SetRemoteConsumed(size_t new_remote_consumed) {
    CHECK(_cur_buf_size > 0);
    bthread_id_list_t tmplist;
    CHECK_EQ(0, bthread_id_list_init(&tmplist, 0, 0));
    bthread_mutex_lock(&_congestion_control_mutex);
    if (_remote_consumed >= new_remote_consumed) {
        bthread_mutex_unlock(&_congestion_control_mutex);
        return;
    }
    const bool was_full = _produced >= _remote_consumed + _cur_buf_size;

    if (FLAGS_socket_max_streams_unconsumed_bytes > 0 && _host_socket != NULL) {
        const size_t consumed_delta = new_remote_consumed - _remote_consumed;
        const size_t accounted_delta =
            std::min(consumed_delta, _socket_unconsumed_size);
        if (accounted_delta != 0) {
            _host_socket->_total_streams_unconsumed_size.fetch_sub(
                accounted_delta, butil::memory_order_relaxed);
            _socket_unconsumed_size -= accounted_delta;
        }
        const int64_t total_unconsumed = _host_socket->_total_streams_unconsumed_size.load(
                butil::memory_order_relaxed);
        if (total_unconsumed > FLAGS_socket_max_streams_unconsumed_bytes) {
            if (_options.min_buf_size > 0) {
                _cur_buf_size = _options.min_buf_size;
            } else {
                _cur_buf_size /= 2;
            }
            LOG(INFO) << "stream consumers on socket " << _host_socket->id()
                      << " is crowded, cut stream " << id()
                      << " buffer to " << _cur_buf_size;
        } else if (_produced >= new_remote_consumed + _cur_buf_size &&
                   (_options.max_buf_size <= 0 || _cur_buf_size < (size_t)_options.max_buf_size)) {
            if (_options.max_buf_size > 0 && _cur_buf_size * 2 > (size_t)_options.max_buf_size) {
                _cur_buf_size = _options.max_buf_size;
            } else {
                _cur_buf_size *= 2;
            }
        }
    }

    _remote_consumed = new_remote_consumed;
    const bool is_full = _produced >= _remote_consumed + _cur_buf_size;
    if (was_full && !is_full) {
        bthread_id_list_swap(&tmplist, &_writable_wait_list);
    }
    bthread_mutex_unlock(&_congestion_control_mutex);

    // broadcast
    bthread_id_list_reset(&tmplist, 0);
    bthread_id_list_destroy(&tmplist);
}

void* Stream::RunOnWritable(void* arg) {
    WritableMeta *wm = (WritableMeta*)arg;
    wm->on_writable(wm->id, wm->arg, wm->error_code);
    delete wm;
    return NULL;
}

int Stream::TriggerOnWritable(bthread_id_t id, void *data, int error_code) {
    WritableMeta *wm = (WritableMeta*)data;
    
    if (wm->has_timer) {
        bthread_timer_del(wm->timer);
    }
    wm->error_code = error_code;
    if (wm->new_thread) {
        const bthread_attr_t* attr = 
            FLAGS_usercode_in_pthread ? &BTHREAD_ATTR_PTHREAD
            : &BTHREAD_ATTR_NORMAL;
        bthread_t tid;
        if (bthread_start_background(&tid, attr, RunOnWritable, wm) != 0) {
            LOG(FATAL) << "Fail to start bthread" << berror();
            RunOnWritable(wm);
        }
    } else {
        RunOnWritable(wm);
    }
    return bthread_id_unlock_and_destroy(id);
}

void OnTimedOut(void *arg) {
    bthread_id_t id = { reinterpret_cast<uint64_t>(arg) };
    bthread_id_error(id, ETIMEDOUT);
}

void Stream::Wait(void (*on_writable)(StreamId, void*, int), void* arg, 
                  const timespec* due_time, bool new_thread, bthread_id_t *join_id) {
    WritableMeta *wm = new WritableMeta;
    wm->on_writable = on_writable;
    wm->id = id();
    wm->arg = arg;
    wm->new_thread = new_thread;
    wm->has_timer = false;
    bthread_id_t wait_id;
    const int rc = bthread_id_create(&wait_id, wm, TriggerOnWritable);
    if (rc != 0) {
        CHECK(false) << "Fail to create bthread_id, " << berror(rc);
        wm->error_code = rc;
        RunOnWritable(wm);
        return;
    }
    if (join_id) {
        *join_id = wait_id;
    }
    CHECK_EQ(0, bthread_id_lock(wait_id, NULL));
    if (due_time != NULL) {
        wm->has_timer = true;
        const int rc = bthread_timer_add(&wm->timer, *due_time,
                                         OnTimedOut, 
                                         reinterpret_cast<void*>(wait_id.value));
        if (rc != 0) {
            LOG(ERROR) << "Fail to add timer, " << berror(rc);
            CHECK_EQ(0, TriggerOnWritable(wait_id, wm, rc));
        }
    }
    bthread_mutex_lock(&_congestion_control_mutex);
    if (_cur_buf_size <= 0 
            || _produced < _remote_consumed + _cur_buf_size) {
        bthread_mutex_unlock(&_congestion_control_mutex);
        CHECK_EQ(0, TriggerOnWritable(wait_id, wm, 0));
        return;
    } else {
        bthread_id_list_add(&_writable_wait_list, wait_id);
        bthread_mutex_unlock(&_congestion_control_mutex);
    }
    CHECK_EQ(0, bthread_id_unlock(wait_id));
}

void Stream::Wait(void (*on_writable)(StreamId, void *, int), void *arg,
                  const timespec* due_time) {
    return Wait(on_writable, arg, due_time, true, NULL);
}

void OnWritable(StreamId, void *arg, int error_code) {
    *(int*)arg = error_code;
}

int Stream::Wait(const timespec* due_time) {
    int rc;
    bthread_id_t join_id = INVALID_BTHREAD_ID;
    Wait(OnWritable, &rc, due_time, false, &join_id);
    if (join_id != INVALID_BTHREAD_ID) {
        bthread_id_join(join_id);
    }
    return rc;
}

void Stream::SetConnected() {
    return SetConnected(NULL);
}

void Stream::SetConnected(const StreamSettings* remote_settings) {
    bthread_mutex_lock(&_connect_mutex);
    if (Failed()) {
        bthread_mutex_unlock(&_connect_mutex);
        return;
    }
    if (_connected.load(butil::memory_order_relaxed)) {
        // SetConnected() may be driven more than once (and concurrently) for
        // the same stream, notably for extra streams in batch creation. It must
        // be idempotent: guarded by _connect_mutex, only the first call takes
        // effect and later calls simply return.
        bthread_mutex_unlock(&_connect_mutex);
        return;
    }
    CHECK(_host_socket != NULL);
    if (remote_settings != NULL) {
        CHECK(!_remote_settings.IsInitialized());
        _remote_settings.MergeFrom(*remote_settings);
    } else {
        CHECK(_remote_settings.IsInitialized());
    }
    RPC_VLOG << "stream=" << id() << " is connected to stream_id="
             << _remote_settings.stream_id() << " at host_socket=" << *_host_socket;

    // Flush writes buffered before connecting FIRST, while _connected is still
    // false so concurrent AppendIfNotFull() take the slow path and block on
    // _connect_mutex. Only after flushing do we publish _connected=true, so
    // subsequent lock-free fast-path writes are strictly ordered after these
    // pending writes.
    std::vector<PendingWrite> pending;
    pending.swap(_pending_writes);
    for (size_t i = 0; i < pending.size(); ++i) {
        if (Failed()) {
            size_t unsent_size = 0;
            for (size_t j = i; j < pending.size(); ++j) {
                unsent_size += pending[j].data.length();
            }
            RollbackProduced(unsent_size);
            bthread_mutex_unlock(&_connect_mutex);
            return;
        }

        size_t len = pending[i].data.length();
        if (WritePacked(pending[i].data, &pending[i].options) != 0) {
            int error_code = errno != 0 ? errno : EIO;
            // The congestion window accounted for every pending write when it
            // was accepted. Keep the successfully enqueued prefix accounted,
            // but roll back the failed write and the unsent suffix.
            size_t unsent_size = 0;
            for (size_t j = i; j < pending.size(); ++j) {
                unsent_size += pending[j].data.length();
            }
            RollbackProduced(unsent_size);
            bthread_mutex_unlock(&_connect_mutex);
            VersionedRefWithId<Stream>::SetFailed(
                error_code, "Failed to flush pending writes during connection");
            return;
        }
        if (FLAGS_socket_max_streams_unconsumed_bytes > 0) {
            BAIDU_SCOPED_LOCK(_congestion_control_mutex);
            if (!Failed()) {
                _host_socket->_total_streams_unconsumed_size.fetch_add(
                    len, butil::memory_order_relaxed);
                _socket_unconsumed_size += len;
            }
        }
    }

    // Check both before and after publishing. The second check closes the
    // window in which SetFailed() can bump the version between the first check
    // and the store. If failure happens after the second check, connection was
    // published first and OnFailed() will observe and close it normally.
    if (Failed()) {
        bthread_mutex_unlock(&_connect_mutex);
        return;
    }
    _connected.store(true, butil::memory_order_release);
    if (Failed()) {
        _connected.store(false, butil::memory_order_relaxed);
        bthread_mutex_unlock(&_connect_mutex);
        return;
    }
    bthread_mutex_unlock(&_connect_mutex);

    if (remote_settings == NULL) {
        // Start the timer at server-side
        // Client-side timer would triggered in Consume after received the first
        // message which is the very RPC response
        StartIdleTimer();
    } else {
        // send first feedback for client-side stream if it already consumed data
        if (_remote_settings.need_feedback()) {
            auto consumed_bytes = _atomic_local_consumed.load(butil::memory_order_acquire);
            if (consumed_bytes > 0)
                SendFeedback(consumed_bytes);
        }
    }
}

int Stream::OnReceived(const StreamFrameMeta& fm, butil::IOBuf *buf, Socket* sock) {
    if (!_connected.load(butil::memory_order_acquire)) {
        // Before connection is published, let the locked slow path initialize
        // the host socket or confirm that another thread already did so.
        if (SetHostSocket(sock) != 0) {
            return -1;
        }
    }

    switch (fm.frame_type()) {
    case FRAME_TYPE_FEEDBACK:
        if (_connected.load(butil::memory_order_acquire)) {
            SetRemoteConsumed(fm.feedback().consumed_size());
        }
        CHECK(buf->empty());
        break;
    case FRAME_TYPE_DATA:
        if (_pending_buf != NULL) {
            _pending_buf->append(*buf);
            buf->clear();
        } else {
            _pending_buf = new butil::IOBuf;
            _pending_buf->swap(*buf);
        }
        if (!fm.has_continuation()) {
            butil::IOBuf* tmp = _pending_buf;
            _pending_buf = NULL;
            int rc = bthread::execution_queue_execute(_consumer_queue, tmp);
            if (rc != 0) {
                CHECK(false) << "Fail to push into channel";
                delete tmp;
                Close(rc, "Fail to push into channel");
            }
        }
        break;
    case FRAME_TYPE_RST:
        RPC_VLOG << "stream=" << id() << " received rst frame";
        Close(ECONNRESET, "Received RST frame");
        break;
    case FRAME_TYPE_CLOSE:
        RPC_VLOG << "stream=" << id() << " received close frame";
        // TODO:: See the comments in Consume
        Close(0, "Received CLOSE frame");
        break;
    case FRAME_TYPE_UNKNOWN:
        RPC_VLOG << "Received unknown frame";
        return -1;
    }
    return 0;
}

class MessageBatcher {
public:
    MessageBatcher(butil::IOBuf* storage[], size_t cap, Stream* s) 
        : _storage(storage)
        , _cap(cap)
        , _size(0)
        , _total_length(0)
        , _s(s)
    {}
    ~MessageBatcher() { flush(); }
    void flush() {
        if (_size > 0 && _s->_options.handler != NULL) {
            _s->_options.handler->on_received_messages(
                    _s->id(), _storage, _size);
        }
        for (size_t i = 0; i < _size; ++i) {
            delete _storage[i];
        }
        _size = 0;
    }
    void push(butil::IOBuf* buf) {
        if (_size == _cap) {
            flush();
        }
        _storage[_size++] = buf;
        _total_length += buf->length();

    }
    size_t total_length() const { return _total_length; }
private:
    butil::IOBuf** _storage;
    size_t _cap;
    size_t _size;
    size_t _total_length;
    Stream* _s;
};

int Stream::Consume(void *meta, bthread::TaskIterator<butil::IOBuf*>& iter) {
    Stream* s = (Stream*)meta;
    s->StopIdleTimer();
    if (iter.is_queue_stopped()) {
        // The consumer queue is stopped (the stream was SetFailed). Fire the
        // user callbacks, then release the reference held by the queue (which
        // was added in OnCreated). This may recycle the instance via
        // BeforeRecycled(), so do not touch `s' afterwards.
        if (s->_options.handler != NULL) {
            int error_code;
            std::string error_text;
            {
                BAIDU_SCOPED_LOCK(s->_connect_mutex);
                error_code = s->_error_code;
                error_text = s->_error_text;
            }
            if (error_code != 0) {
                // The stream is closed abnormally.
                s->_options.handler->on_failed(s->id(), error_code, error_text);
            }
            s->_options.handler->on_closed(s->id());
        }
        DereferenceVersionedRefWithId(s);
        return 0;
    }

    DEFINE_SMALL_ARRAY(butil::IOBuf*, buf_list, s->_options.messages_in_batch, 256);
    MessageBatcher mb(buf_list, s->_options.messages_in_batch, s);
    bool has_timeout_task = false;
    for (; iter; ++iter) {
        butil::IOBuf* t= *iter;
        if (t == TIMEOUT_TASK) {
            has_timeout_task = true;
        } else {
            if (s->_parse_rpc_response) {
                s->_parse_rpc_response = false;
                s->HandleRpcResponse(t);
            } else {
                mb.push(t);
            }
        }
    }
    if (s->_options.handler != NULL) {
        if (has_timeout_task && mb.total_length() == 0) {
            s->_options.handler->on_idle_timeout(s->id());
        }
    }
    mb.flush();

    auto total_length = mb.total_length();
    if (total_length > 0) {
        // fast path for connected stream
        if (s->_connected.load(butil::memory_order_acquire)){
            if (s->_remote_settings.need_feedback()) {
                s->_local_consumed += total_length;
                s->SendFeedback(s->_local_consumed);
            }
        } else {
            // Under the scenario of batch creation of Streams, there is concurrency between SetConnected and Consume for the same stream,
            // and it is necessary to ensure the memory order.
            s->_local_consumed = s->_atomic_local_consumed.fetch_add(total_length, butil::memory_order_release) + total_length;
            if (s->_connected.load(butil::memory_order_acquire) && s->_remote_settings.need_feedback()) {
                s->SendFeedback(s->_local_consumed);
            }
        }
    }

    s->StartIdleTimer();
    return 0;
}

void Stream::SendFeedback(int64_t _consumed_bytes) {
    StreamFrameMeta fm;
    fm.set_frame_type(FRAME_TYPE_FEEDBACK);
    fm.set_stream_id(_remote_settings.stream_id());
    fm.set_source_stream_id(id());
    fm.mutable_feedback()->set_consumed_size(_consumed_bytes);
    butil::IOBuf out;
    policy::PackStreamMessage(&out, fm, NULL);
    WriteToHostSocket(&out);
}

int Stream::SetHostSocket(Socket* host_socket) {
    BAIDU_SCOPED_LOCK(_connect_mutex);
    if (Failed()) {
        return -1;
    }
    if (_host_socket != NULL) {
        return 0;
    }

    SocketUniquePtr ptr;
    host_socket->ReAddress(&ptr);
    if (ptr->AddStream(id()) != 0) {
        CHECK(false) << id() << " fail to add stream to host socket";
        return -1;
    }

    _host_socket = ptr.release();
    return 0;
}

void Stream::FillSettings(StreamSettings *settings) {
    settings->set_stream_id(id());
    settings->set_need_feedback(_cur_buf_size > 0);
    settings->set_writable(_options.handler != NULL);
}

void OnIdleTimeout(void *arg) {
    bthread::ExecutionQueueId<butil::IOBuf*> q = { (uint64_t)arg };
    bthread::execution_queue_execute(q, (butil::IOBuf*)TIMEOUT_TASK);
}

void Stream::StartIdleTimer() {
    if (_options.idle_timeout_ms < 0) {
        return;
    }
    _start_idle_timer_us = butil::gettimeofday_us();
    timespec due_time = butil::microseconds_to_timespec(
            _start_idle_timer_us + _options.idle_timeout_ms * 1000);
    const int rc = bthread_timer_add(&_idle_timer, due_time, OnIdleTimeout,
                                     (void*)(_consumer_queue.value));
    LOG_IF(WARNING, rc != 0) << "Fail to add timer";
}

void Stream::StopIdleTimer() {
    if (_options.idle_timeout_ms < 0) {
        return;
    }
    if (_idle_timer != 0) {
        bthread_timer_del(_idle_timer);
    }
}

void Stream::CloseV(int error_code, const char* reason_fmt, va_list ap) {
    if (Failed()) {
        return;
    }

    std::string error_text;
    butil::string_vappendf(&error_text, reason_fmt, ap);
    VersionedRefWithId<Stream>::SetFailed(error_code, error_text);
}

void Stream::Close(int error_code, const char* reason_fmt, ...) {
    va_list ap;
    va_start(ap, reason_fmt);
    CloseV(error_code, reason_fmt, ap);
    va_end(ap);
}

int Stream::SetFailedV(StreamId id, int error_code,
                       const char* reason_fmt, va_list ap) {
    StreamUniquePtr stream_ptr;
    if (AddressFailedAsWell(id, &stream_ptr) == -1) {
        // Don't care recycled stream.
        return 0;
    }
    stream_ptr->CloseV(error_code, reason_fmt, ap);
    return 0;
}

int Stream::SetFailed(StreamId id, int error_code, const char* reason_fmt, ...) {
    va_list ap;
    va_start(ap, reason_fmt);
    int rc = SetFailedV(id, error_code, reason_fmt, ap);
    va_end(ap);
    return rc;
}

int Stream::SetFailed(const StreamIds& ids, int error_code, const char* reason_fmt, ...) {
    va_list ap;
    va_start(ap, reason_fmt);
    for (auto id : ids) {
        va_list ap_copy;
        va_copy(ap_copy, ap);
        SetFailedV(id, error_code, reason_fmt, ap_copy);
        va_end(ap_copy);
    }
    va_end(ap);
    return 0;
}

void Stream::HandleRpcResponse(butil::IOBuf* response_buffer) {
    CHECK(!_remote_settings.IsInitialized());
    CHECK(_host_socket != NULL);
    std::unique_ptr<butil::IOBuf> buf_guard(response_buffer);
    ParseResult pr = policy::ParseRpcMessage(response_buffer, NULL, true, NULL);
    if (!pr.is_ok()) {
        CHECK(false);
        Close(EPROTO, "Fail to parse rpc response message");
        return;
    }
    InputMessageBase* msg = pr.message();
    if (msg == NULL) {
        CHECK(false);
        Close(ENOMEM, "Message is NULL");
        return;
    }
    _host_socket->PostponeEOF();
    _host_socket->ReAddress(&msg->_socket);
    msg->_received_us = butil::gettimeofday_us(); 
    msg->_base_real_us = butil::gettimeofday_us();
    msg->_arg = NULL; // ProcessRpcResponse() don't need arg
    policy::ProcessRpcResponse(msg);
}

int StreamWrite(StreamId stream_id, const butil::IOBuf& message,
                const StreamWriteOptions* options) {
    StreamUniquePtr stream_ptr;
    if (Stream::Address(stream_id, &stream_ptr) != 0) {
        return EINVAL;
    }
    Stream* s = stream_ptr.get();
    const int rc = s->AppendIfNotFull(message, options);
    if (rc == 0) {
        return 0;
    }
    return (rc == 1) ? EAGAIN : errno;
}

void StreamWait(StreamId stream_id, const timespec *due_time,
                void (*on_writable)(StreamId, void*, int), void *arg) {
    StreamUniquePtr stream_ptr;
    if (Stream::Address(stream_id, &stream_ptr) != 0) {
        Stream::WritableMeta* wm = new Stream::WritableMeta;
        wm->id = stream_id;
        wm->arg= arg;
        wm->has_timer = false;
        wm->on_writable = on_writable;
        wm->error_code = EINVAL;
        const bthread_attr_t* attr =
            FLAGS_usercode_in_pthread ? &BTHREAD_ATTR_PTHREAD
            : &BTHREAD_ATTR_NORMAL;
        bthread_t tid;
        if (bthread_start_background(&tid, attr, Stream::RunOnWritable, wm) != 0) {
            PLOG(FATAL) << "Fail to start bthread";
            Stream::RunOnWritable(wm);
        }
        return;
    }
    Stream* s = stream_ptr.get();
    return s->Wait(on_writable, arg, due_time);
}

int StreamWait(StreamId stream_id, const timespec* due_time) {
    StreamUniquePtr stream_ptr;
    if (Stream::Address(stream_id, &stream_ptr) != 0) {
        return EINVAL;
    }
    Stream* s = stream_ptr.get();
    return s->Wait(due_time);
}

int StreamClose(StreamId stream_id) {
    return Stream::SetFailed(stream_id, 0, "Local close");
}

int StreamCreate(StreamId *request_stream, Controller &cntl,
                 const StreamOptions* options) {
    if (request_stream == NULL) {
        LOG(ERROR) << "request_stream is NULL";
        return -1;
    }
    StreamIds request_streams;
    const int rc = StreamCreate(request_streams, 1, cntl, options);
    if (rc != 0) {
        return rc;
    }
    *request_stream = request_streams[0];
    return 0;
}

int StreamCreate(StreamIds& request_streams, int request_stream_size, Controller & cntl,
                 const StreamOptions* options) {
    if (!cntl._request_streams.empty()) {
        LOG(ERROR) << "Can't create request stream more than once";
        return -1;
    }
    if (!request_streams.empty()) {
        LOG(ERROR) << "request_streams should be empty";
        return -1;
    }
    StreamOptions opt;
    if (options != NULL) {
        opt = *options;
    }
    for (auto i = 0; i < request_stream_size; ++i) {
        StreamId stream_id;
        bool parse_rpc_response = (i == 0); // Only the first stream need parse rpc
        if (Stream::Create(opt, NULL, &stream_id, parse_rpc_response) != 0) {
            // Close already created streams
            Stream::SetFailed(request_streams, 0 , "Fail to create stream at %d index", i);
            LOG(ERROR) << "Fail to create stream";
            return -1;
        }
        cntl._request_streams.push_back(stream_id);
        request_streams.push_back(stream_id);
    }
    return 0;
}

int StreamAccept(StreamId* response_stream, Controller &cntl,
                 const StreamOptions* options) {
    if (response_stream == NULL) {
        LOG(ERROR) << "response_stream is NULL";
        return -1;
    }
    StreamIds response_streams;
    int res = StreamAccept(response_streams, cntl, options);
    if(res != 0) {
        return res;
    }
    if(response_streams.size() != 1) {
        Stream::SetFailed(response_streams, EINVAL,
                          "misusing StreamAccept for single stream to accept multiple streams");
        cntl._response_streams.clear();
        LOG(ERROR) << "misusing StreamAccept for single stream to accept multiple streams";
        return -1;
    }
    *response_stream = response_streams[0];
    return 0;
}

int StreamAccept(StreamIds& response_streams, Controller& cntl,
                 const StreamOptions* options) {
    if (!cntl._response_streams.empty()) {
        LOG(ERROR) << "Can't create response stream more than once";
        return -1;
    }

    if (!response_streams.empty()) {
        LOG(ERROR) << "response_streams should be empty";
        return -1;
    }
    if (!cntl.has_remote_stream()) {
        LOG(ERROR) << "No stream along with this request";
        return -1;
    }
    StreamOptions opt;
    if (options != NULL) {
        opt = *options;
    }
    StreamId stream_id;
    if (Stream::Create(opt, cntl._remote_stream_settings, &stream_id, false) != 0) {
        Stream::SetFailed(response_streams, 0, "Fail to accept stream");
        LOG(ERROR) << "Fail to accept stream";
        return -1;
    }

    cntl._response_streams.push_back(stream_id);
    response_streams.push_back(stream_id);
    if(!cntl._remote_stream_settings->extra_stream_ids().empty()) {
        StreamSettings stream_remote_settings;
        stream_remote_settings.MergeFrom(*cntl._remote_stream_settings);
        //Only the first stream needs extra_stream_ids settings
        stream_remote_settings.clear_extra_stream_ids();
        for (auto i = 0; i < cntl._remote_stream_settings->extra_stream_ids_size(); ++i) {
            stream_remote_settings.set_stream_id(cntl._remote_stream_settings->extra_stream_ids()[i]);
            StreamId extra_stream_id;
            if (Stream::Create(opt, &stream_remote_settings, &extra_stream_id, false) != 0) {
                Stream::SetFailed(response_streams, 0, "Fail to accept stream at %d index", i);
                cntl._response_streams.clear();
                response_streams.clear();
                LOG(ERROR) << "Fail to accept stream";
                return -1;
            }
            cntl._response_streams.push_back(extra_stream_id);
            response_streams.push_back(extra_stream_id);
        }
    }

    return 0;
}

} // namespace brpc
