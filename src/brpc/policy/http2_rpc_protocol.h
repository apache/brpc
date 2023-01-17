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


#ifndef BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H
#define BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H

#include "brpc/policy/http_rpc_protocol.h"   // HttpContext
#include "brpc/input_message_base.h"
#include "brpc/protocol.h"
#include "brpc/details/hpack.h"
#include "brpc/stream_creator.h"
#include "brpc/controller.h"

#ifndef NDEBUG
#include "bvar/bvar.h"
#endif

namespace brpc {
namespace policy {

class H2StreamContext;

class H2ParseResult {
public:
    explicit H2ParseResult(H2Error err, int stream_id)
        : _msg(NULL), _err(err), _stream_id(stream_id) {}
    explicit H2ParseResult(H2StreamContext* msg)
        : _msg(msg), _err(H2_NO_ERROR), _stream_id(0) {}
    
    // Return H2_NO_ERROR when the result is successful.
    H2Error error() const { return _err; }
    const char* error_str() const { return H2ErrorToString(_err); }
    bool is_ok() const { return error() == H2_NO_ERROR; }
    int stream_id() const { return _stream_id; }

    // definitely NULL when result is failed.
    H2StreamContext* message() const { return _msg; }
 
private:
    H2StreamContext* _msg;
    H2Error _err;
    int _stream_id;
};

inline H2ParseResult MakeH2Error(H2Error err, int stream_id)
{ return H2ParseResult(err, stream_id); }
inline H2ParseResult MakeH2Error(H2Error err)
{ return H2ParseResult(err, 0); }
inline H2ParseResult MakeH2Message(H2StreamContext* msg)
{ return H2ParseResult(msg); }

class H2Context;

enum H2FrameType {
    H2_FRAME_DATA          = 0x0,
    H2_FRAME_HEADERS       = 0x1,
    H2_FRAME_PRIORITY      = 0x2,
    H2_FRAME_RST_STREAM    = 0x3,
    H2_FRAME_SETTINGS      = 0x4,
    H2_FRAME_PUSH_PROMISE  = 0x5,
    H2_FRAME_PING          = 0x6,
    H2_FRAME_GOAWAY        = 0x7,
    H2_FRAME_WINDOW_UPDATE = 0x8,
    H2_FRAME_CONTINUATION  = 0x9,
    // ============================
    H2_FRAME_TYPE_MAX      = 0x9
};

// https://tools.ietf.org/html/rfc7540#section-4.1
struct H2FrameHead {
    // The length of the frame payload expressed as an unsigned 24-bit integer.
    // Values greater than H2Settings.max_frame_size MUST NOT be sent
    uint32_t payload_size;

    // The 8-bit type of the frame. The frame type determines the format and
    // semantics of the frame.  Implementations MUST ignore and discard any
    // frame that has a type that is unknown.
    H2FrameType type;

    // An 8-bit field reserved for boolean flags specific to the frame type.
    // Flags are assigned semantics specific to the indicated frame type.
    // Flags that have no defined semantics for a particular frame type
    // MUST be ignored and MUST be left unset (0x0) when sending.
    uint8_t flags;

    // A stream identifier (see Section 5.1.1) expressed as an unsigned 31-bit
    // integer. The value 0x0 is reserved for frames that are associated with
    // the connection as a whole as opposed to an individual stream.
    int stream_id;
};

enum H2StreamState {
    H2_STREAM_IDLE = 0,
    H2_STREAM_RESERVED_LOCAL,
    H2_STREAM_RESERVED_REMOTE,
    H2_STREAM_OPEN,
    H2_STREAM_HALF_CLOSED_LOCAL,
    H2_STREAM_HALF_CLOSED_REMOTE,
    H2_STREAM_CLOSED,
};
const char* H2StreamState2Str(H2StreamState);

#ifndef NDEBUG
struct H2Bvars {
    bvar::Adder<int> h2_unsent_request_count;
    bvar::Adder<int> h2_stream_context_count;

    H2Bvars()
        : h2_unsent_request_count("h2_unsent_request_count")
        , h2_stream_context_count("h2_stream_context_count") {
    }
};
inline H2Bvars* get_h2_bvars() {
    return butil::get_leaky_singleton<H2Bvars>();
}
#endif

class H2UnsentRequest : public SocketMessage, public StreamUserData {
friend void PackH2Request(butil::IOBuf*, SocketMessage**,
                          uint64_t, const google::protobuf::MethodDescriptor*,
                          Controller*, const butil::IOBuf&, const Authenticator*);
public:
    static H2UnsentRequest* New(Controller* c);
    void Print(std::ostream& os) const;

    int AddRefManually()
    { return _nref.fetch_add(1, butil::memory_order_relaxed); }

    void RemoveRefManually() {
        if (_nref.fetch_sub(1, butil::memory_order_release) == 1) {
            butil::atomic_thread_fence(butil::memory_order_acquire);
            Destroy();
        }
    }

    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*) override;
    size_t EstimatedByteSize() override;

    // @StreamUserData
    void DestroyStreamUserData(SocketUniquePtr& sending_sock,
                               Controller* cntl,
                               int error_code,
                               bool end_of_rpc) override;

private:
    std::string& push(const std::string& name)
    { return (new (&_list[_size++]) HPacker::Header(name))->value; }
    
    void push(const std::string& name, const std::string& value)
    { new (&_list[_size++]) HPacker::Header(name, value); }

    H2UnsentRequest(Controller* c)
        : _nref(1)
        , _size(0)
        , _stream_id(0)
        , _cntl(c) {
#ifndef NDEBUG
        get_h2_bvars()->h2_unsent_request_count << 1;
#endif
    }
    ~H2UnsentRequest() {
#ifndef NDEBUG
        get_h2_bvars()->h2_unsent_request_count << -1;
#endif
    }
    H2UnsentRequest(const H2UnsentRequest&);
    void operator=(const H2UnsentRequest&);
    void Destroy();

private:
    butil::atomic<int> _nref;
    uint32_t _size;
    int _stream_id;
    mutable butil::Mutex _mutex;
    Controller* _cntl;
    std::unique_ptr<H2StreamContext> _sctx;
    HPacker::Header _list[0];
};

class H2UnsentResponse : public SocketMessage {
public:
    static H2UnsentResponse* New(Controller* c, int stream_id, bool is_grpc);
    void Destroy();
    void Print(std::ostream& os) const;
    // @SocketMessage
    butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket*) override;
    size_t EstimatedByteSize() override;
    
private:
    std::string& push(const std::string& name)
    { return (new (&_list[_size++]) HPacker::Header(name))->value; }
    
    void push(const std::string& name, const std::string& value)
    { new (&_list[_size++]) HPacker::Header(name, value); }

    H2UnsentResponse(Controller* c, int stream_id, bool is_grpc);
    ~H2UnsentResponse() {}
    H2UnsentResponse(const H2UnsentResponse&);
    void operator=(const H2UnsentResponse&);

private:
    uint32_t _size;
    uint32_t _stream_id;
    std::unique_ptr<HttpHeader> _http_response;
    butil::IOBuf _data;
    bool _is_grpc;
    GrpcStatus _grpc_status;
    std::string _grpc_message;
    HPacker::Header _list[0];
};

// Used in http_rpc_protocol.cpp
class H2StreamContext : public HttpContext {
public:
    H2StreamContext(bool read_body_progressively);
    ~H2StreamContext();
    void Init(H2Context* conn_ctx, int stream_id);

    // Decode headers in HPACK from *it and set into this->header(). The input
    // does not need to complete.
    // Returns 0 on success, -1 otherwise.
    int ConsumeHeaders(butil::IOBufBytesIterator& it);
    H2ParseResult OnEndStream();

    H2ParseResult OnData(butil::IOBufBytesIterator&, const H2FrameHead&,
                       uint32_t frag_size, uint8_t pad_length);
    H2ParseResult OnHeaders(butil::IOBufBytesIterator&, const H2FrameHead&,
                          uint32_t frag_size, uint8_t pad_length);
    H2ParseResult OnContinuation(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnResetStream(H2Error h2_error, const H2FrameHead&);
    
    uint64_t correlation_id() const { return _correlation_id; }
    void set_correlation_id(uint64_t cid) { _correlation_id = cid; }
    
    size_t parsed_length() const { return this->_parsed_length; }
    int stream_id() const { return _stream_id; }

    int64_t ReleaseDeferredWindowUpdate() {
        if (_deferred_window_update.load(butil::memory_order_relaxed) == 0) {
            return 0;
        }
        return _deferred_window_update.exchange(0, butil::memory_order_relaxed);
    }

    bool ConsumeWindowSize(int64_t size);

#if defined(BRPC_H2_STREAM_STATE)
    H2StreamState state() const { return _state; }
    void SetState(H2StreamState state);
#endif

friend class H2Context;
    H2Context* _conn_ctx;
#if defined(BRPC_H2_STREAM_STATE)
    H2StreamState _state;
#endif
    int _stream_id;
    bool _stream_ended;
    butil::atomic<int64_t> _remote_window_left;
    butil::atomic<int64_t> _deferred_window_update;
    uint64_t _correlation_id;
    butil::IOBuf _remaining_header_fragment;
};

StreamCreator* get_h2_global_stream_creator();

ParseResult ParseH2Message(butil::IOBuf *source, Socket *socket,
                             bool read_eof, const void *arg);
void PackH2Request(butil::IOBuf* buf,
                   SocketMessage** user_message_out,
                   uint64_t correlation_id,
                   const google::protobuf::MethodDescriptor* method,
                   Controller* controller,
                   const butil::IOBuf& request,
                   const Authenticator* auth);

class H2GlobalStreamCreator : public StreamCreator {
protected:
    StreamUserData* OnCreatingStream(SocketUniquePtr* inout, Controller* cntl) override;
    void DestroyStreamCreator(Controller* cntl) override;
};

enum H2ConnectionState {
    H2_CONNECTION_UNINITIALIZED,
    H2_CONNECTION_READY,
    H2_CONNECTION_GOAWAY,
};

void SerializeFrameHead(void* out_buf,
                        uint32_t payload_size, H2FrameType type,
                        uint8_t flags, uint32_t stream_id);

size_t SerializeH2Settings(const H2Settings& in, void* out);

const size_t FRAME_HEAD_SIZE = 9;

// Contexts of a http2 connection
class H2Context : public Destroyable, public Describable {
public:
    typedef H2ParseResult (H2Context::*FrameHandler)(
        butil::IOBufBytesIterator&, const H2FrameHead&);

    // main_socket: the socket owns this object as parsing_context
    // server: NULL means client-side
    H2Context(Socket* main_socket, const Server* server);
    ~H2Context();
    // Must be called before usage.
    int Init();

    H2ConnectionState state() const { return _conn_state; }
    ParseResult Consume(butil::IOBufBytesIterator& it, Socket*);

    void ClearAbandonedStreams();
    void AddAbandonedStream(uint32_t stream_id);

    //@Destroyable
    void Destroy() override { delete this; }

    int AllocateClientStreamId();
    bool RunOutStreams() const;
    // Try to map stream_id to ctx if stream_id does not exist before
    // Returns 0 on success, -1 on exist, 1 on goaway.
    int TryToInsertStream(int stream_id, H2StreamContext* ctx);
    size_t VolatilePendingStreamSize() const { return _pending_streams.size(); }

    HPacker& hpacker() { return _hpacker; }
    const H2Settings& remote_settings() const { return _remote_settings; }
    const H2Settings& local_settings() const { return _local_settings; }

    bool is_client_side() const { return _socket->CreatedByConnect(); }
    bool is_server_side() const { return !is_client_side(); }

    void Describe(std::ostream& os, const DescribeOptions&) const override;

    void DeferWindowUpdate(int64_t);
    int64_t ReleaseDeferredWindowUpdate();

private:
friend class H2StreamContext;
friend class H2UnsentRequest;
friend class H2UnsentResponse;
friend void InitFrameHandlers();

    ParseResult ConsumeFrameHead(butil::IOBufBytesIterator&, H2FrameHead*);

    H2ParseResult OnData(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnHeaders(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnPriority(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnResetStream(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnSettings(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnPushPromise(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnPing(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnGoAway(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnWindowUpdate(butil::IOBufBytesIterator&, const H2FrameHead&);
    H2ParseResult OnContinuation(butil::IOBufBytesIterator&, const H2FrameHead&);

    H2StreamContext* RemoveStreamAndDeferWU(int stream_id);
    void RemoveGoAwayStreams(int goaway_stream_id, std::vector<H2StreamContext*>* out_streams);

    H2StreamContext* FindStream(int stream_id);

    // True if the connection is established by client, otherwise it's
    // accepted by server.
    Socket* _socket;
    butil::atomic<int64_t> _remote_window_left;
    H2ConnectionState _conn_state;
    int _last_received_stream_id;
    uint32_t _last_sent_stream_id;
    int _goaway_stream_id;
    H2Settings _remote_settings;
    bool _remote_settings_received;
    H2Settings _local_settings;
    H2Settings _unack_local_settings;
    HPacker _hpacker;
    mutable butil::Mutex _abandoned_streams_mutex;
    std::vector<uint32_t> _abandoned_streams;
    typedef butil::FlatMap<int, H2StreamContext*> StreamMap;
    mutable butil::Mutex _stream_mutex;
    StreamMap _pending_streams;
    butil::atomic<int64_t> _deferred_window_update;
};

inline int H2Context::AllocateClientStreamId() {
    if (RunOutStreams()) {
        LOG(WARNING) << "Fail to allocate new client stream, _last_sent_stream_id="
            << _last_sent_stream_id;
        return -1;
    }
    const int id = _last_sent_stream_id;
    _last_sent_stream_id += 2;
    return id;
}

inline bool H2Context::RunOutStreams() const {
    return (_last_sent_stream_id > 0x7FFFFFFF);
}

inline std::ostream& operator<<(std::ostream& os, const H2UnsentRequest& req) {
    req.Print(os);
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const H2UnsentResponse& res) {
    res.Print(os);
    return os;
}

} // namespace policy
} // namespace brpc

#endif // BAIDU_RPC_POLICY_HTTP2_RPC_PROTOCOL_H
