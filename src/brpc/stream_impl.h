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


#ifndef  BRPC_STREAM_IMPL_H
#define  BRPC_STREAM_IMPL_H

#include <cstdarg>
#include <vector>
#include "bthread/bthread.h"
#include "bthread/execution_queue.h"
#include "brpc/socket.h"
#include "brpc/stream.h"
#include "brpc/versioned_ref_with_id.h"
#include "brpc/streaming_rpc_meta.pb.h"

namespace brpc {

// Stream is implemented on top of VersionedRefWithId<Stream>, so that StreamId
// is a self-contained versioned reference id and no longer depends on a fake
// Socket. The instance is managed by a ResourcePool: it is reused rather than
// re-constructed, thus all per-stream state must be (re)initialized in
// OnCreated() and cleaned up in OnFailed()/BeforeRecycled().
class BAIDU_CACHELINE_ALIGNMENT Stream : public VersionedRefWithId<Stream> {
public:
    // NOTE: Users cannot create Stream from constructor. Use Create() instead.
    // It's public only because of the requirement of ResourcePool.
    explicit Stream(Forbidden);
    ~Stream();

    // Write `msg' into this stream. Returns 0 on success, 1 when the stream is
    // full, -1 on error.
    int AppendIfNotFull(const butil::IOBuf& msg,
                        const StreamWriteOptions* options = NULL);
    static int Create(const StreamOptions& options,
                      const StreamSettings* remote_settings,
                      StreamId *id, bool parse_rpc_response = true);

    int OnReceived(const StreamFrameMeta& fm, butil::IOBuf *buf, Socket* sock);
    void SetRemoteSettings(const StreamSettings& remote_settings) {
        _remote_settings.MergeFrom(remote_settings);
    }
    int SetHostSocket(Socket* host_socket);
    void SetConnected();
    void SetConnected(const StreamSettings *remote_settings);

    void Wait(void (*on_writable)(StreamId, void*, int), void *arg,
                    const timespec *due_time);
    int Wait(const timespec* due_time);
    void FillSettings(StreamSettings *settings);

    static int SetFailed(StreamId id, int error_code, const char* reason_fmt, ...)
        __attribute__ ((__format__ (__printf__, 3, 4)));
    static int SetFailed(const StreamIds& ids, int error_code, const char* reason_fmt, ...)
    __attribute__ ((__format__ (__printf__, 3, 4)));
    void Close(int error_code, const char* reason_fmt, ...)
        __attribute__ ((__format__ (__printf__, 3, 4)));

private:
friend void StreamWait(StreamId stream_id, const timespec *due_time,
                       void (*on_writable)(StreamId, void*, int), void *arg);
friend class MessageBatcher;
friend class VersionedRefWithId<Stream>;

    // Initialize (or reset for a reused instance) the stream.
    // Returns 0 on success, non-zero on failure.
    int OnCreated(const StreamOptions& options,
                  const StreamSettings* remote_settings,
                  bool parse_rpc_response);
    // Called once when SetFailed() succeeds. Performs the close actions
    // (wake up waiters, send CLOSE frame, stop the consumer queue, etc.).
    void OnFailed(int error_code, const std::string& error_text);
    // Called right before the instance is recycled to the ResourcePool.
    void BeforeRecycled();
    std::string OnDescription() const;

    void SetRemoteConsumed(size_t _remote_consumed);
    void Wait(void (*on_writable)(StreamId, void*, int), void* arg, 
              const timespec* due_time, bool new_thread, bthread_id_t *join_id);
    void SendFeedback(int64_t _consumed_bytes);
    void StartIdleTimer();
    void StopIdleTimer();
    void HandleRpcResponse(butil::IOBuf* response_buffer);
    void WriteToHostSocket(butil::IOBuf* b);
    // Pack `data` into one or more STRM DATA frames (splitting large data into
    // segments) and write them into the host socket in a single Write.
    int WritePacked(const butil::IOBuf& data, const StreamWriteOptions* options);
    // Roll back `_produced` by `data_length` (under `_congestion_control_mutex`)
    // when a write fails. No-op when the congestion window is disabled.
    void RollbackProduced(size_t data_length);

    static int Consume(void *meta, bthread::TaskIterator<butil::IOBuf*>& iter);
    static int TriggerOnWritable(bthread_id_t id, void *data, int error_code);
    static void *RunOnWritable(void* arg);

    static int SetFailedV(StreamId id, int error_code,
                          const char* reason_fmt, va_list ap);
    void CloseV(int error_code, const char* reason_fmt, va_list ap);

    struct WritableMeta {
        void (*on_writable)(StreamId, void*, int);
        StreamId id;
        void *arg;
        int error_code;
        bool new_thread;
        bool has_timer;
        bthread_timer_t timer;
    };

    struct PendingWrite {
        butil::IOBuf data;
        StreamWriteOptions options;

        PendingWrite() = default;
        explicit PendingWrite(const butil::IOBuf& d, const StreamWriteOptions* opts)
            : data(d) {
            if (opts != NULL) {
                options = *opts;
            }
        }
    };

    Socket* _host_socket; // Every stream within a Socket holds a reference.
    StreamOptions _options;

    mutable bthread_mutex_t _connect_mutex;
    butil::atomic<bool> _connected;
    int _error_code;
    std::string _error_text;
    // Writes buffered before the stream is connected (the remote stream id
    // is unknown until then). Flushed in SetConnected().
    std::vector<PendingWrite> _pending_writes;
    
    bthread_mutex_t _congestion_control_mutex;
    size_t _produced;
    size_t _remote_consumed;
    // Bytes of this Stream currently included in the host Socket's aggregate
    // unconsumed counter. Protected by _congestion_control_mutex.
    size_t _socket_unconsumed_size;
    size_t _cur_buf_size;
    bthread_id_list_t _writable_wait_list;

    int64_t _local_consumed;
    butil::atomic<int64_t> _atomic_local_consumed;
    StreamSettings _remote_settings;

    bool _parse_rpc_response;
    bthread::ExecutionQueueId<butil::IOBuf*> _consumer_queue;
    butil::IOBuf* _pending_buf;
    int64_t _start_idle_timer_us;
    bthread_timer_t _idle_timer;
};

typedef VersionedRefWithIdUniquePtr<Stream> StreamUniquePtr;

} // namespace brpc



#endif  //BRPC_STREAM_IMPL_H
