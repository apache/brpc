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
//          Jiashun Zhu(zhujiashun@baidu.com)

#include "brpc/policy/http2_rpc_protocol.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/server.h"
#include "butil/base64.h"

namespace brpc {

DECLARE_bool(http_verbose);
DECLARE_int32(http_verbose_max_body_length);

namespace policy {

DEFINE_int32(http2_client_header_table_size,
             H2Settings::DEFAULT_HEADER_TABLE_SIZE,
             "maximum size of compression tables for decoding headers");
DEFINE_int32(http2_client_initial_window_size,
             H2Settings::DEFAULT_INITIAL_WINDOW_SIZE,
             "Initial window size for flow control");
DEFINE_int32(http2_client_max_frame_size,
             H2Settings::DEFAULT_MAX_FRAME_SIZE,
             "Size of the largest frame payload that client is willing to receive");

DEFINE_bool(http2_hpack_encode_name, false,
            "Encode name in HTTP2 headers with huffman encoding");
DEFINE_bool(http2_hpack_encode_value, false,
            "Encode value in HTTP2 headers with huffman encoding");

const char* H2StreamState2Str(H2StreamState s) {
    switch (s) {
    case H2_STREAM_IDLE: return "idle";
    case H2_STREAM_RESERVED_LOCAL: return "reserved(local)";
    case H2_STREAM_RESERVED_REMOTE: return "reserved(remote)";
    case H2_STREAM_OPEN: return "open";
    case H2_STREAM_HALF_CLOSED_LOCAL: return "half-closed(local)";
    case H2_STREAM_HALF_CLOSED_REMOTE: return "half-closed(remote)";
    case H2_STREAM_CLOSED: return "closed";
    }
    return "unknown(H2StreamState)";
}

enum H2ConnectionState {
    H2_CONNECTION_UNINITIALIZED,
    H2_CONNECTION_READY,
    H2_CONNECTION_GOAWAY,
};
static const char* H2ConnectionState2Str(H2ConnectionState s) {
    switch (s) {
    case H2_CONNECTION_UNINITIALIZED: return "UNINITIALIZED";
    case H2_CONNECTION_READY: return "READY";
    case H2_CONNECTION_GOAWAY: return "GOAWAY";
    }
    return "UNKNOWN(H2ConnectionState)";
}

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

// A series of utilities to load numbers from http2 streams.
inline uint8_t LoadUint8(butil::IOBufBytesIterator& it) {
    uint8_t v = *it;
    ++it;
    return v;
}
inline uint16_t LoadUint16(butil::IOBufBytesIterator& it) {
    uint16_t v = *it; ++it;
    v = ((v << 8) | *it); ++it;
    return v;
}
inline uint32_t LoadUint32(butil::IOBufBytesIterator& it) {
    uint32_t v = *it; ++it;
    v = ((v << 8) | *it); ++it;
    v = ((v << 8) | *it); ++it;
    v = ((v << 8) | *it); ++it;
    return v;
}
inline void SaveUint16(void* out, uint16_t v) {
    uint8_t* p = (uint8_t*)out;
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}
inline void SaveUint32(void* out, uint32_t v) {
    uint8_t* p = (uint8_t*)out;
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

const uint8_t H2_FLAGS_END_STREAM = 0x1;
const uint8_t H2_FLAGS_ACK = 0x1;
const uint8_t H2_FLAGS_END_HEADERS = 0x4;
const uint8_t H2_FLAGS_PADDED = 0x8;
const uint8_t H2_FLAGS_PRIORITY = 0x20;

#define H2_CONNECTION_PREFACE_PREFIX "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
const size_t H2_CONNECTION_PREFACE_PREFIX_SIZE = 24;

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

static void InitFrameHandlers();

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
    void Destroy() { delete this; }

    int AllocateClientStreamId();
    bool RunOutStreams();
    // Try to map stream_id to ctx if stream_id does not exist before
    // Returns true on success, false otherwise.
    bool TryToInsertStream(int stream_id, H2StreamContext* ctx);
    uint32_t StreamSize();
    
    HPacker& hpacker() { return _hpacker; }
    const H2Settings& remote_settings() const { return _remote_settings; }
    const H2Settings& local_settings() const { return _local_settings; }

    bool is_client_side() const { return _socket->CreatedByConnect(); }
    bool is_server_side() const { return !is_client_side(); }

    void Describe(std::ostream& os, const DescribeOptions&) const;

    void ReclaimWindowSize(int64_t);

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

    H2StreamContext* RemoveStream(int stream_id);
    H2StreamContext* FindStream(int stream_id, bool* closed);
    H2StreamContext* FindStream(int stream_id) {
        return FindStream(stream_id, NULL);
    }

    void ClearAbandonedStreamsImpl();
    
    // True if the connection is established by client, otherwise it's
    // accepted by server.
    Socket* _socket;
    butil::atomic<int64_t> _remote_conn_window_size;
    H2ConnectionState _conn_state;
    int _last_server_stream_id;
    uint32_t _last_client_stream_id;
    H2Settings _remote_settings;
    H2Settings _local_settings;
    H2Settings _unack_local_settings;
    HPacker _hpacker;
    butil::Mutex _abandoned_streams_mutex;
    std::vector<uint32_t> _abandoned_streams;
    typedef butil::FlatMap<int, H2StreamContext*> StreamMap;
    butil::Mutex _stream_mutex;
    StreamMap _pending_streams;
    butil::atomic<int64_t> _pending_conn_window_size;
};

inline bool add_window_size(butil::atomic<int64_t>* window_size, int64_t diff) {
    // A sender MUST NOT allow a flow-control window to exceed 2^31 - 1.
    // If a sender receives a WINDOW_UPDATE that causes a flow-control window 
    // to exceed this maximum, it MUST terminate either the stream or the connection,
    // as appropriate.
    int64_t before_add = window_size->fetch_add(diff, butil::memory_order_relaxed);
    if ((((before_add | diff) >> 31) & 1) == 0) {
        // two positive int64_t, check positive overflow
        if ((before_add + diff) & (1 << 31)) {
            return false;
        }
    }
    if ((((before_add & diff) >> 31) & 1) == 1) {
        // two negative int64_t, check negaitive overflow
        if (((before_add + diff) & (1 << 31)) == 0) {
            return false;
        }
    }
    // window_size being negative is OK
    return true;
}

inline bool consume_window_size(butil::atomic<int64_t>* window_size, int64_t size) {
    if (window_size->load(butil::memory_order_relaxed) < size) {
        // false negative is OK.
        return false;
    }
    int64_t before_sub = window_size->fetch_sub(size, butil::memory_order_relaxed);
    if (before_sub < size) {
        window_size->fetch_add(size, butil::memory_order_relaxed);
        return false;
    }
    return true;
}

static H2Context::FrameHandler s_frame_handlers[H2_FRAME_TYPE_MAX + 1];
    
static pthread_once_t s_frame_handlers_init_once = PTHREAD_ONCE_INIT;
static void InitFrameHandlers() {
    s_frame_handlers[H2_FRAME_DATA] = &H2Context::OnData;
    s_frame_handlers[H2_FRAME_HEADERS] = &H2Context::OnHeaders;
    s_frame_handlers[H2_FRAME_PRIORITY] = &H2Context::OnPriority;
    s_frame_handlers[H2_FRAME_RST_STREAM] = &H2Context::OnResetStream;
    s_frame_handlers[H2_FRAME_SETTINGS] = &H2Context::OnSettings;
    s_frame_handlers[H2_FRAME_PUSH_PROMISE] = &H2Context::OnPushPromise;
    s_frame_handlers[H2_FRAME_PING] = &H2Context::OnPing;
    s_frame_handlers[H2_FRAME_GOAWAY] = &H2Context::OnGoAway;
    s_frame_handlers[H2_FRAME_WINDOW_UPDATE] = &H2Context::OnWindowUpdate;
    s_frame_handlers[H2_FRAME_CONTINUATION] = &H2Context::OnContinuation;
}
inline H2Context::FrameHandler FindFrameHandler(H2FrameType type) {
    pthread_once(&s_frame_handlers_init_once, InitFrameHandlers);
    if (type < 0 || type > H2_FRAME_TYPE_MAX) {
        return NULL;
    }
    return s_frame_handlers[type];
}

H2Context::H2Context(Socket* socket, const Server* server)
    : _socket(socket)
    , _remote_conn_window_size(H2Settings::DEFAULT_INITIAL_WINDOW_SIZE)
    , _conn_state(H2_CONNECTION_UNINITIALIZED)
    , _last_server_stream_id(-1)
    , _last_client_stream_id(1)
    , _pending_conn_window_size(0) {
    if (server) {
        _unack_local_settings = server->options().http2_settings;
    } else {
        _unack_local_settings.header_table_size = FLAGS_http2_client_header_table_size;
        _unack_local_settings.initial_window_size = FLAGS_http2_client_initial_window_size;
        _unack_local_settings.max_frame_size = FLAGS_http2_client_max_frame_size;
    }
#if defined(UNIT_TEST)
    // In ut, we hope _last_client_stream_id run out quickly to test the correctness
    // of creating new h2 socket. This value is 100,000 less than 0x7FFFFFFF.
    _last_client_stream_id = 0x7FFE795F;
#endif
}

H2Context::~H2Context() {
    for (StreamMap::iterator it = _pending_streams.begin();
         it != _pending_streams.end(); ++it) {
        delete it->second;
    }
    _pending_streams.clear();
}

H2StreamContext::H2StreamContext()
    : _conn_ctx(NULL)
#ifdef HAS_H2_STREAM_STATE
    , _state(H2_STREAM_IDLE)
#endif
    , _stream_ended(false)
    , _remote_window_size(0)
    , _local_window_size(0)
    , _correlation_id(INVALID_BTHREAD_ID.value) {
    header().set_version(2, 0);
}

H2StreamContext::~H2StreamContext() {
    if (_conn_ctx) {
        int64_t diff = _conn_ctx->local_settings().initial_window_size - _local_window_size;
        _conn_ctx->ReclaimWindowSize(diff);
    }
}

int H2Context::Init() {
    if (_pending_streams.init(64, 70) != 0) {
        LOG(ERROR) << "Fail to init _pending_streams";
        return -1;
    }
    if (_hpacker.Init(_unack_local_settings.header_table_size) != 0) {
        LOG(ERROR) << "Fail to init _hpacker";
        return -1;
    }
    return 0;
}

inline int H2Context::AllocateClientStreamId() {
    if (RunOutStreams()) {
        return -1;
    }
    const int id = _last_client_stream_id;
    _last_client_stream_id += 2;
    return id;
}

inline bool H2Context::RunOutStreams() {
    if (_last_client_stream_id > 0x7FFFFFFF) {
        // run out stream id
        return true;
    }
    return false;
}

H2StreamContext* H2Context::RemoveStream(int stream_id) {
    H2StreamContext* sctx = NULL;
    std::unique_lock<butil::Mutex> mu(_stream_mutex);
    if (_pending_streams.erase(stream_id, &sctx)) {
        return sctx;
    }
    return NULL;
}

H2StreamContext* H2Context::FindStream(int stream_id, bool* closed) {
    {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        H2StreamContext** psctx = _pending_streams.seek(stream_id);
        if (psctx) {
            return *psctx;
        }
    }
    if (closed) {
        const uint32_t limit = is_client_side() ? _last_client_stream_id
            : (uint32_t)_last_server_stream_id;
        *closed = ((uint32_t)stream_id < limit);
    }
    return NULL;
}

bool H2Context::TryToInsertStream(int stream_id, H2StreamContext* ctx) {
    std::unique_lock<butil::Mutex> mu(_stream_mutex);
    H2StreamContext*& sctx = _pending_streams[stream_id];
    if (sctx == NULL) {
        sctx = ctx;
        return true;
    }
    return false;
}

uint32_t H2Context::StreamSize() {
    std::unique_lock<butil::Mutex> mu(_stream_mutex);
    return _pending_streams.size();
}

const size_t FRAME_HEAD_SIZE = 9;

ParseResult H2Context::ConsumeFrameHead(
    butil::IOBufBytesIterator& it, H2FrameHead* frame_head) {
    uint8_t length_buf[3];
    size_t n = it.copy_and_forward(length_buf, sizeof(length_buf));
    if (n < 3) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const uint32_t length = ((uint32_t)length_buf[0] << 16)
        | ((uint32_t)length_buf[1] << 8) | length_buf[2];
    if (length > _local_settings.max_frame_size) {
        LOG(ERROR) << "Too large length=" << length << " max="
                   << _local_settings.max_frame_size;
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    if (it.bytes_left() < FRAME_HEAD_SIZE - 3 + length) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    frame_head->payload_size = length;
    frame_head->type = (H2FrameType)LoadUint8(it);
    frame_head->flags = LoadUint8(it);
    const uint32_t stream_id = LoadUint32(it);
    if (stream_id & 0x80000000) {
        LOG(ERROR) << "Invalid stream_id=" << stream_id;
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    frame_head->stream_id = static_cast<int>(stream_id);
    return MakeMessage(NULL);
}

static void SerializeFrameHead(void* out_buf, uint32_t payload_size,
                               H2FrameType type, uint8_t flags,
                               uint32_t stream_id) {
    uint8_t* p = (uint8_t*)out_buf;
    *p++ = (payload_size >> 16) & 0xFF;
    *p++ = (payload_size >> 8) & 0xFF;
    *p++ = payload_size & 0xFF;
    *p++ = (uint8_t)type;
    *p++ = flags;
    *p++ = (stream_id >> 24) & 0xFF;
    *p++ = (stream_id >> 16) & 0xFF;
    *p++ = (stream_id >> 8) & 0xFF;
    *p++ = stream_id & 0xFF;    
}

inline void SerializeFrameHead(void* out_buf, const H2FrameHead& h) {
    return SerializeFrameHead(out_buf, h.payload_size, h.type,
                              h.flags, h.stream_id);
}

ParseResult H2Context::Consume(
    butil::IOBufBytesIterator& it, Socket* socket) {
    if (_conn_state == H2_CONNECTION_UNINITIALIZED) {
        if (is_server_side()) {
            // Wait for the client connection preface prefix
            char preface[H2_CONNECTION_PREFACE_PREFIX_SIZE];
            const size_t n = it.copy_and_forward(preface, sizeof(preface));
            if (memcmp(preface, H2_CONNECTION_PREFACE_PREFIX, n) != 0) {
                return MakeParseError(PARSE_ERROR_TRY_OTHERS);
            }
            if (n < sizeof(preface)) {
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            _conn_state = H2_CONNECTION_READY;

            char settingbuf[36];
            _unack_local_settings.SerializeTo(settingbuf); 
            char headbuf[FRAME_HEAD_SIZE];
            SerializeFrameHead(headbuf, _unack_local_settings.ByteSize(),
                               H2_FRAME_SETTINGS, 0, 0);
            butil::IOBuf buf;
            buf.append(headbuf, FRAME_HEAD_SIZE);
            buf.append(settingbuf, _unack_local_settings.ByteSize());
            Socket::WriteOptions wopt;
            wopt.ignore_eovercrowded = true;
            if (socket->Write(&buf, &wopt) != 0) {
                LOG(WARNING) << "Fail to respond http2-client with settings";
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
        } else {
            _conn_state = H2_CONNECTION_READY;
        }
        return MakeMessage(NULL);
    } else if (_conn_state == H2_CONNECTION_READY) {
        H2FrameHead frame_head;
        ParseResult res = ConsumeFrameHead(it, &frame_head);
        if (!res.is_ok()) {
            return res;
        }
        H2Context::FrameHandler handler = FindFrameHandler(frame_head.type);
        if (handler == NULL) {
            LOG(ERROR) << "Invalid frame type=" << (int)frame_head.type;
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        H2ParseResult h2_res = (this->*handler)(it, frame_head);
        if (h2_res.is_ok()) {
            return MakeMessage(h2_res.message());
        }
        if (h2_res.stream_id()) { // send RST_STREAM
            char rstbuf[FRAME_HEAD_SIZE + 4];
            SerializeFrameHead(rstbuf, 4, H2_FRAME_RST_STREAM,
                               0, h2_res.stream_id());
            SaveUint32(rstbuf + FRAME_HEAD_SIZE, h2_res.error());
            butil::IOBuf sendbuf;
            sendbuf.append(rstbuf, sizeof(rstbuf));
            Socket::WriteOptions wopt;
            wopt.ignore_eovercrowded = true;
            if (_socket->Write(&sendbuf, &wopt) != 0) {
                LOG(WARNING) << "Fail to send RST_STREAM";
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            return MakeMessage(NULL);
        } else { // send GOAWAY
            char goawaybuf[FRAME_HEAD_SIZE + 4];
            SerializeFrameHead(goawaybuf, 8, H2_FRAME_GOAWAY, 0, 0);
            SaveUint32(goawaybuf + FRAME_HEAD_SIZE, 0/*last-stream-id*/);
            SaveUint32(goawaybuf + FRAME_HEAD_SIZE + 4, h2_res.error());
            butil::IOBuf sendbuf;
            sendbuf.append(goawaybuf, sizeof(goawaybuf));
            Socket::WriteOptions wopt;
            wopt.ignore_eovercrowded = true;
            if (_socket->Write(&sendbuf, &wopt) != 0) {
                LOG(WARNING) << "Fail to send GOAWAY";
            }
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
    } else {
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
}

H2ParseResult H2Context::OnHeaders(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    // HEADERS frames MUST be associated with a stream.  If a HEADERS frame
    // is received whose stream identifier field is 0x0, the recipient MUST
    // respond with a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
    if (frame_head.stream_id == 0) {
        LOG(ERROR) << "Invalid stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    const bool has_padding = (frame_head.flags & H2_FLAGS_PADDED);
    const bool has_priority = (frame_head.flags & H2_FLAGS_PRIORITY);
    if (frame_head.payload_size <
        (size_t)(has_priority ? 5 : 0) + (size_t)has_padding) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    uint32_t frag_size = frame_head.payload_size;
    uint8_t pad_length = 0;
    if (has_padding) {
        pad_length = LoadUint8(it);
        --frag_size;
    }
    if (has_priority) {
        const uint32_t ALLOW_UNUSED stream_dep = LoadUint32(it);
        const uint32_t ALLOW_UNUSED weight = LoadUint8(it);
        frag_size -= 5;
    }
    if (frag_size < pad_length) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    frag_size -= pad_length;
    H2StreamContext* sctx = NULL;
    if (is_server_side() &&
        frame_head.stream_id > _last_server_stream_id) { // new stream
        if ((frame_head.stream_id & 1) == 0) {
            LOG(ERROR) << "stream_id=" << frame_head.stream_id
                       << " created by client is not odd";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        if (((frame_head.stream_id - _last_server_stream_id) & 1) != 0) {
            LOG(ERROR) << "Invalid stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        _last_server_stream_id = frame_head.stream_id;
        sctx = new H2StreamContext(this, frame_head.stream_id);
        if (!TryToInsertStream(frame_head.stream_id, sctx)) {
            delete sctx;
            LOG(ERROR) << "Fail to insert stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
    } else {
        sctx = FindStream(frame_head.stream_id);
        if (sctx == NULL) {
            LOG(ERROR) << "stream_id=" << frame_head.stream_id
                       << " does not exist";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
    }
    return sctx->OnHeaders(it, frame_head, frag_size, pad_length);
}

H2ParseResult H2StreamContext::OnHeaders(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head,
    uint32_t frag_size, uint8_t pad_length) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
#ifdef HAS_H2_STREAM_STATE
    SetState(H2_STREAM_OPEN);
#endif
    butil::IOBufBytesIterator it2(it, frag_size);
    if (ConsumeHeaders(it2) < 0) {
        LOG(ERROR) << "Invalid header, frag_size=" << frag_size;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    const size_t nskip = frag_size - it2.bytes_left();
    CHECK_EQ(nskip, it.forward(nskip));
    if (it2.bytes_left()) {
        it.append_and_forward(&_remaining_header_fragment,
                              it2.bytes_left());
    }
    it.forward(pad_length);
    if (frame_head.flags & H2_FLAGS_END_HEADERS) {
        if (it2.bytes_left() != 0) {
            LOG(ERROR) << "Incomplete header";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        if (frame_head.flags & H2_FLAGS_END_STREAM) {
            return EndRemoteStream();
        }
        return MakeH2Message(NULL);
    }
    if (frame_head.flags & H2_FLAGS_END_STREAM) {
        // Delay calling EndRemoteStream() in OnContinuation()
        _stream_ended = true;
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnContinuation(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    H2StreamContext* sctx = FindStream(frame_head.stream_id);
    if (sctx == NULL) {
        LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    return sctx->OnContinuation(it, frame_head);
}

H2ParseResult H2StreamContext::OnContinuation(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
    it.append_and_forward(&_remaining_header_fragment, frame_head.payload_size);
    const size_t size = _remaining_header_fragment.size();
    butil::IOBufBytesIterator it2(_remaining_header_fragment);
    if (ConsumeHeaders(it2) < 0) {
        LOG(ERROR) << "Invalid header";
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    _remaining_header_fragment.pop_front(size - it2.bytes_left());
    if (frame_head.flags & H2_FLAGS_END_HEADERS) {
        if (it2.bytes_left() != 0) {
            LOG(ERROR) << "Incomplete header";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        if (_stream_ended) {
            return EndRemoteStream();
        }
        return MakeH2Message(NULL);
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnData(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    uint32_t frag_size = frame_head.payload_size;
    uint8_t pad_length = 0;
    if (frame_head.flags & H2_FLAGS_PADDED) {
        --frag_size;
        pad_length = LoadUint8(it);
    }
    if (frag_size < pad_length) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    frag_size -= pad_length;
    H2StreamContext* sctx = FindStream(frame_head.stream_id);
    if (sctx == NULL) {
        LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
        return MakeH2Message(NULL);
    }
    return sctx->OnData(it, frame_head, frag_size, pad_length);
}

H2ParseResult H2StreamContext::OnData(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head,
    uint32_t frag_size, uint8_t pad_length) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
    butil::IOBuf data;
    it.append_and_forward(&data, frag_size);
    it.forward(pad_length);
    for (size_t i = 0; i < data.backing_block_num(); ++i) {
        const butil::StringPiece blk = data.backing_block(i);
        if (OnBody(blk.data(), blk.size()) != 0) {
            LOG(ERROR) << "Fail to parse data";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
    }
    int64_t before_sub = _local_window_size.fetch_sub(frag_size, butil::memory_order_relaxed);
    // HTTP/2 defines only the format and semantics of the WINDOW_UPDATE frame (Section 6.9).
    // Spec does not stipulate how a receiver decides when to send this frame or the value
    // that it sends, nor does it specify how a sender chooses to send packets.
    // Implementations are able to select any algorithm that suits their needs.
    if (before_sub < _conn_ctx->local_settings().initial_window_size / 3) {
        int64_t old_value = _local_window_size.exchange(_conn_ctx->local_settings().initial_window_size,
                                                        butil::memory_order_relaxed);
        char swinbuf[FRAME_HEAD_SIZE + 4];
        SerializeFrameHead(swinbuf, 4, H2_FRAME_WINDOW_UPDATE, 0, stream_id());
        SaveUint32(swinbuf + FRAME_HEAD_SIZE, _local_window_size - old_value);
        char cwinbuf[FRAME_HEAD_SIZE + 4];
        SerializeFrameHead(cwinbuf, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
        SaveUint32(cwinbuf + FRAME_HEAD_SIZE, _local_window_size - old_value);
        butil::IOBuf sendbuf;
        sendbuf.append(swinbuf, sizeof(swinbuf));
        sendbuf.append(cwinbuf, sizeof(cwinbuf));
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        if (_conn_ctx->_socket->Write(&sendbuf, &wopt) != 0) {
            LOG(WARNING) << "Fail to send WINDOW_UPDATE";
            return MakeH2Error(H2_INTERNAL_ERROR);
        }
    }
    if (frame_head.flags & H2_FLAGS_END_STREAM) {
        return EndRemoteStream();
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnResetStream(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    if (frame_head.payload_size != 4) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    H2StreamContext* sctx = FindStream(frame_head.stream_id);
    if (sctx == NULL) {
        LOG(WARNING) << "Fail to find stream_id=" << frame_head.stream_id;
        return MakeH2Message(NULL);
    }
    const H2Error h2_error = static_cast<H2Error>(LoadUint32(it));
    return sctx->OnResetStream(h2_error, frame_head);
}

H2ParseResult H2StreamContext::OnResetStream(
    H2Error h2_error, const H2FrameHead& frame_head) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
#ifdef HAS_H2_STREAM_STATE
    if (state() == H2_STREAM_OPEN) {
        SetState(H2_STREAM_HALF_CLOSED_REMOTE);
    } else if (state() == H2_STREAM_HALF_CLOSED_LOCAL) {
        SetState(H2_STREAM_CLOSED);
    } else {
        LOG(ERROR) << "Invalid state=" << H2StreamState2Str(_state)
                   << " in stream_id=" << stream_id();
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
#endif
    H2StreamContext* sctx = _conn_ctx->RemoveStream(stream_id());
    if (sctx != NULL) {
        LOG(ERROR) << "Fail to find stream_id=" << stream_id();
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    if (_conn_ctx->is_client_side()) {
        sctx->header().set_status_code(HTTP_STATUS_INTERNAL_SERVER_ERROR);
        sctx->header()._h2_error = h2_error;
        return MakeH2Message(sctx);
    } else {
        // No need to process the request.
        delete sctx;
        return MakeH2Message(NULL);
    }
}

H2ParseResult H2StreamContext::EndRemoteStream() {
#ifdef HAS_H2_STREAM_STATE
    if (state() == H2_STREAM_OPEN) {
        SetState(H2_STREAM_HALF_CLOSED_REMOTE);
    } else if (state() == H2_STREAM_HALF_CLOSED_LOCAL) {
        SetState(H2_STREAM_CLOSED);
    } else {
        LOG(ERROR) << "Invalid state=" << H2StreamState2Str(_state)
                   << " in stream_id=" << stream_id();
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
#endif
    OnMessageComplete();
    H2StreamContext* sctx = _conn_ctx->RemoveStream(stream_id());
    if (sctx == NULL) {
        LOG(ERROR) << "Fail to find stream_id=" << stream_id();
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    return MakeH2Message(sctx);
}

H2ParseResult H2Context::OnSettings(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    // SETTINGS frames always apply to a connection, never a single stream.
    // The stream identifier for a SETTINGS frame MUST be zero (0x0).  If an
    // endpoint receives a SETTINGS frame whose stream identifier field is
    // anything other than 0x0, the endpoint MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.
    if (frame_head.stream_id != 0) {
        LOG(ERROR) << "Invalid stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    if (frame_head.flags & H2_FLAGS_ACK) {
        if (frame_head.payload_size != 0) {
            LOG(ERROR) << "Non-zero payload_size=" << frame_head.payload_size
                       << " for settings-ACK";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        _local_settings = _unack_local_settings;
        return MakeH2Message(NULL);
    }
    const int64_t old_initial_window_size = _remote_settings.initial_window_size;
    if (!_remote_settings.ParseFrom(it, frame_head.payload_size)) {
        LOG(ERROR) << "Fail to parse from SETTINGS";
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    const int64_t window_diff =
        static_cast<int64_t>(_remote_settings.initial_window_size)
        - old_initial_window_size;
    if (window_diff) {
        // Do not update the connection flow-control window here, which can only be
        // changed using WINDOW_UPDATE frames.
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        for (StreamMap::const_iterator it = _pending_streams.begin();
             it != _pending_streams.end(); ++it) {
            if (!add_window_size(&it->second->_remote_window_size, window_diff)) {
                return MakeH2Error(H2_FLOW_CONTROL_ERROR);
            }
        }
    }
    // Respond with ack
    char headbuf[FRAME_HEAD_SIZE];
    SerializeFrameHead(headbuf, 0, H2_FRAME_SETTINGS, H2_FLAGS_ACK, 0);
    butil::IOBuf sendbuf;
    sendbuf.append(headbuf, FRAME_HEAD_SIZE);
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (_socket->Write(&sendbuf, &wopt) != 0) {
        LOG(WARNING) << "Fail to respond settings with ack";
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnPriority(
    butil::IOBufBytesIterator&, const H2FrameHead&) {
    LOG(ERROR) << "Not support PRIORITY frame yet";
    return MakeH2Error(H2_PROTOCOL_ERROR);
}

H2ParseResult H2Context::OnPushPromise(
    butil::IOBufBytesIterator&, const H2FrameHead&) {
    LOG(ERROR) << "Not support PUSH_PROMISE frame yet";
    return MakeH2Error(H2_PROTOCOL_ERROR);
}

H2ParseResult H2Context::OnPing(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    if (frame_head.payload_size != 8) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    if (frame_head.stream_id != 0) {
        LOG(ERROR) << "Invalid stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    if (frame_head.flags & H2_FLAGS_ACK) {
        return MakeH2Message(NULL);
    }
    
    char pongbuf[FRAME_HEAD_SIZE + 8];
    SerializeFrameHead(pongbuf, 8, H2_FRAME_PING, H2_FLAGS_ACK, 0);
    it.copy_and_forward(pongbuf + FRAME_HEAD_SIZE, 8);
    butil::IOBuf sendbuf;
    sendbuf.append(pongbuf, sizeof(pongbuf));
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (_socket->Write(&sendbuf, &wopt) != 0) {
        LOG(WARNING) << "Fail to send ack of PING";
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnGoAway(
    butil::IOBufBytesIterator&, const H2FrameHead&) {
    _socket->SetFailed(ELOGOFF, "Received GOAWAY from %s",
                       butil::endpoint2str(_socket->remote_side()).c_str());
    return MakeH2Error(H2_PROTOCOL_ERROR);
}
                          
H2ParseResult H2Context::OnWindowUpdate(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    if (frame_head.payload_size != 4) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    const uint32_t inc = LoadUint32(it);
    if ((inc & 0x80000000) || (inc == 0)) {
        LOG(ERROR) << "Invalid window_size_increment=" << inc;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }

    if (frame_head.stream_id == 0) {
        if (!add_window_size(&_remote_conn_window_size, inc)) {
            LOG(ERROR) << "Invalid window_size_increment=" << inc;
            return MakeH2Error(H2_FLOW_CONTROL_ERROR);
        }
    } else {
        H2StreamContext* sctx = FindStream(frame_head.stream_id);
        if (sctx == NULL) {
            LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
            return MakeH2Message(NULL);
        }
        if (!add_window_size(&sctx->_remote_window_size, inc)) {
            LOG(ERROR) << "Invalid window_size_increment=" << inc;
            return MakeH2Error(H2_FLOW_CONTROL_ERROR);
        }
    }
    return MakeH2Message(NULL);
}

void H2Context::Describe(std::ostream& os, const DescribeOptions& opt) const {
    const char sep = (opt.verbose ? '\n' : ' ');
    os << (opt.verbose ? sep : '{')
       << "remote_conn_window_size=" << _remote_conn_window_size
       << sep << "state=" << H2ConnectionState2Str(_conn_state);
    if (is_server_side()) {
        os << sep << "last_server_stream_id=" << _last_server_stream_id;
    } else {
        os << sep << "last_client_stream_id=" << _last_client_stream_id;
    }
    os << sep << "remote_settings=" << _remote_settings
       << sep << "local_settings=" << _local_settings
       << sep << "hpacker=";
    IndentingOStream os2(os, 2);
    _hpacker.Describe(os2, opt);
    if (!opt.verbose) {
        os << '}';
    }
}

void H2Context::ReclaimWindowSize(int64_t size) {
    if (size <= 0) {
        return;
    }
    // HTTP/2 defines only the format and semantics of the WINDOW_UPDATE frame (Section 6.9).
    // Spec does not stipulate how a receiver decides when to send this frame or the value
    // that it sends, nor does it specify how a sender chooses to send packets.
    // Implementations are able to select any algorithm that suits their needs.

    // TODO(zhujiashun): optimize the number of WINDOW_UPDATE frame
    //int64_t before_add = _pending_conn_window_size.fetch_add(
    //                        size, butil::memory_order_relaxed);
    //if (before_add > local_settings().initial_window_size / 3) {
    //    int64_t old_value = _pending_conn_window_size.exchange(0, butil::memory_order_relaxed);
    //    if (old_value) {
    //    }
    //}

    char cwinbuf[FRAME_HEAD_SIZE + 4];
    SerializeFrameHead(cwinbuf, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
    SaveUint32(cwinbuf + FRAME_HEAD_SIZE, size);
    butil::IOBuf sendbuf;
    sendbuf.append(cwinbuf, sizeof(cwinbuf));
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (_socket->Write(&sendbuf, &wopt) != 0) {
        LOG(WARNING) << "Fail to send WINDOW_UPDATE";
    }
}

/*
bvar::Adder<int64_t> g_parse_time;
bvar::PerSecond<bvar::Adder<int64_t> > g_parse_time_per_second(
    "h2_parse_second", &g_parse_time);
    */

ParseResult ParseH2Message(butil::IOBuf *source, Socket *socket,
                           bool read_eof, const void *arg) {
    //bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_parse_time);
    H2Context* ctx = static_cast<H2Context*>(socket->parsing_context());
    if (ctx == NULL) {
        if (read_eof || source->empty()) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        const Server* server = static_cast<const Server*>(arg);
        ctx = new H2Context(socket, server);
        if (ctx->Init() != 0) {
            delete ctx;
            LOG(ERROR) << "Fail to init H2Context";
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        socket->initialize_parsing_context(&ctx);
    }
    butil::IOBufBytesIterator it(*source);
    size_t last_bytes_left = it.bytes_left();
    CHECK_EQ(last_bytes_left, source->size());
    while (true) {
        ParseResult res = ctx->Consume(it, socket);
        if (res.is_ok()) {
            last_bytes_left = it.bytes_left();
            if (res.message() == NULL) {
                // no message to process, continue parsing.
                continue;
            }
        }
        source->pop_front(source->size() - last_bytes_left);
        ctx->ClearAbandonedStreams();
        return res;
    }
}

void H2Context::AddAbandonedStream(uint32_t stream_id) {
    std::unique_lock<butil::Mutex> mu(_abandoned_streams_mutex);
    _abandoned_streams.push_back(stream_id);
}

inline void H2Context::ClearAbandonedStreams() {
    if (!_abandoned_streams.empty()) {
        ClearAbandonedStreamsImpl();
    }
}

void H2Context::ClearAbandonedStreamsImpl() {
    std::unique_lock<butil::Mutex> mu(_abandoned_streams_mutex);
    while (!_abandoned_streams.empty()) {
        const uint32_t stream_id = _abandoned_streams.back();
        _abandoned_streams.pop_back();
        H2StreamContext* sctx = RemoveStream(stream_id);
        if (sctx != NULL) {
            delete sctx;
        }
    }
}

void H2StreamContext::Init(H2Context* conn_ctx, int stream_id) {
    _conn_ctx = conn_ctx;
    _remote_window_size.store(conn_ctx->remote_settings().initial_window_size,
                              butil::memory_order_relaxed);
    _local_window_size.store(conn_ctx->local_settings().initial_window_size,
                             butil::memory_order_relaxed);
    header()._h2_stream_id = stream_id;
}

H2StreamContext::H2StreamContext(H2Context* conn_ctx, int stream_id)
    : _conn_ctx(conn_ctx)
#ifdef HAS_H2_STREAM_STATE
    , _state(H2_STREAM_IDLE)
#endif
    , _stream_ended(false)
    , _remote_window_size(conn_ctx->remote_settings().initial_window_size)
    , _local_window_size(conn_ctx->local_settings().initial_window_size)
    , _correlation_id(INVALID_BTHREAD_ID.value) {
    header().set_version(2, 0);
    header()._h2_stream_id = stream_id;
}

#ifdef HAS_H2_STREAM_STATE
void H2StreamContext::SetState(H2StreamState state) {
    const H2StreamState old_state = _state;
    _state = state;
    RPC_VLOG << "stream_id=" << stream_id() << " changed from "
             << H2StreamState2Str(old_state) << " to "
             << H2StreamState2Str(state);
}
#endif

int H2StreamContext::ConsumeHeaders(butil::IOBufBytesIterator& it) {
    HPacker& hpacker = _conn_ctx->hpacker();
    HttpHeader& h = header();
    while (it) {
        HPacker::Header pair;
        const int rc = hpacker.Decode(it, &pair);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }
        const char* const name = pair.name.c_str();
        bool matched = false;
        if (name[0] == ':') { // reserved names
            switch (name[1]) {
            case 'a':
                if (strcmp(name + 2, /*a*/"uthority") == 0) {
                    matched = true;
                    h.uri().SetHostAndPort(pair.value);
                }
                break;
            case 'm':
                if (strcmp(name + 2, /*m*/"ethod") == 0) {
                    matched = true;
                    HttpMethod method;
                    if (!Str2HttpMethod(pair.value.c_str(), &method)) {
                        LOG(ERROR) << "Invalid method=" << pair.value;
                        return -1;
                    }
                    h.set_method(method);
                }
                break;
            case 'p':
                if (strcmp(name + 2, /*p*/"ath") == 0) {
                    matched = true;
                    // Including path/query/fragment
                    h.uri().SetH2Path(pair.value);
                }
                break;
            case 's':
                if (strcmp(name + 2, /*:s*/"cheme") == 0) {
                    matched = true;
                    h.uri().set_schema(pair.value);
                } else if (strcmp(name + 2, /*:s*/"tatus") == 0) {
                    matched = true;
                    char* endptr = NULL;
                    const int sc = strtol(pair.value.c_str(), &endptr, 10);
                    if (*endptr != '\0') {
                        LOG(ERROR) << "Invalid status=" << pair.value;
                        return -1;
                    }
                    h.set_status_code(sc);
                }
                break;
            default:
                break;
            }
            if (!matched) {
                LOG(ERROR) << "Unknown name=`" << name << '\'';
                return -1;
            }
        } else if (name[0] == 'c' &&
                   strcmp(name + 1, /*c*/"ontent-type") == 0) {
            h.set_content_type(pair.value);
        } else {
            // TODO: AppendHeader?
            h.SetHeader(pair.name, pair.value);
        }

        if (FLAGS_http_verbose) {
            butil::IOBufBuilder* vs = this->_vmsgbuilder;
            if (vs == NULL) {
                vs = new butil::IOBufBuilder;
                this->_vmsgbuilder = vs;
                if (_conn_ctx->is_server_side()) {
                    *vs << "[ H2 REQUEST @" << butil::my_ip() << " ]";
                } else {
                    *vs << "[ H2 RESPONSE @" << butil::my_ip() << " ]";
                }
            }
            // print \n first to be consistent with code in http_message.cpp
            *vs << "\n< " << pair.name << " = " << pair.value;
        }
    }
    return 0;
}

const CommonStrings* get_common_strings();

static void PackH2Message(butil::IOBuf* out,
                          butil::IOBuf& headers,
                          const butil::IOBuf& data,
                          int stream_id,
                          const H2Settings& remote_settings) {
    char headbuf[FRAME_HEAD_SIZE];
    H2FrameHead headers_head = {
        (uint32_t)headers.size(), H2_FRAME_HEADERS, 0, stream_id};
    if (data.empty()) {
        headers_head.flags |= H2_FLAGS_END_STREAM;
    }
    if (headers_head.payload_size <= remote_settings.max_frame_size) {
        headers_head.flags |= H2_FLAGS_END_HEADERS;
        SerializeFrameHead(headbuf, headers_head);
        out->append(headbuf, sizeof(headbuf));
        out->append(butil::IOBuf::Movable(headers));
    } else {
        headers_head.payload_size = remote_settings.max_frame_size;
        SerializeFrameHead(headbuf, headers_head);
        out->append(headbuf, sizeof(headbuf));
        headers.cutn(out, headers_head.payload_size);

        H2FrameHead cont_head = {0, H2_FRAME_CONTINUATION, 0, stream_id};
        while (!headers.empty()) {
            if (headers.size() <= remote_settings.max_frame_size) {
                cont_head.flags |= H2_FLAGS_END_HEADERS;
                cont_head.payload_size = headers.size();
            } else {
                cont_head.payload_size = remote_settings.max_frame_size;
            }
            SerializeFrameHead(headbuf, cont_head);
            out->append(headbuf, FRAME_HEAD_SIZE);
            headers.cutn(out, cont_head.payload_size);
        }
    }
    if (!data.empty()) {
        H2FrameHead data_head = {0, H2_FRAME_DATA, 0, stream_id};
        butil::IOBufBytesIterator it(data);
        while (it.bytes_left()) {
            if (it.bytes_left() <= remote_settings.max_frame_size) {
                data_head.flags |= H2_FLAGS_END_STREAM;
                data_head.payload_size = it.bytes_left();
            } else {
                data_head.payload_size = remote_settings.max_frame_size;
            }
            SerializeFrameHead(headbuf, data_head);
            out->append(headbuf, FRAME_HEAD_SIZE);
            it.append_and_forward(out, data_head.payload_size);
        }
    }
}

H2UnsentRequest* H2UnsentRequest::New(Controller* c, uint64_t correlation_id) {
    const HttpHeader& h = c->http_request();
    const CommonStrings* const common = get_common_strings();
    const bool need_content_length = (h.method() != HTTP_METHOD_GET);
    const bool need_content_type = !h.content_type().empty();
    const bool need_accept = !h.GetHeader(common->ACCEPT);
    const bool need_user_agent = !h.GetHeader(common->USER_AGENT);
    const std::string& user_info = h.uri().user_info();
    const bool need_authorization =
        (!user_info.empty() && !h.GetHeader("Authorization"));
    const size_t maxsize = h.HeaderCount() + 4
        + (size_t)need_content_length
        + (size_t)need_content_type
        + (size_t)need_accept
        + (size_t)need_user_agent
        + (size_t)need_authorization;
    const size_t memsize = offsetof(H2UnsentRequest, _list) +
        sizeof(HPacker::Header) * maxsize;
    H2UnsentRequest* msg = new (malloc(memsize)) H2UnsentRequest(c);
    // :method
    if (h.method() == HTTP_METHOD_GET) {
        msg->push(common->H2_METHOD, common->METHOD_GET);
    } else if (h.method() == HTTP_METHOD_POST) {
        msg->push(common->H2_METHOD, common->METHOD_POST);
    } else {
        msg->push(common->H2_METHOD) = HttpMethod2Str(h.method());
    }
    // :scheme
    const std::string* scheme = &h.uri().schema();
    if (scheme->empty()) {
        scheme = (c->is_ssl() ? &common->H2_SCHEME_HTTPS :
                  &common->H2_SCHEME_HTTP);
    }
    msg->push(common->H2_SCHEME, *scheme);
    // :path
    h.uri().GenerateH2Path(&msg->push(common->H2_PATH));
    // :authority
    const std::string* phost = h.GetHeader("host");
    if (phost) {
        msg->push(common->H2_AUTHORITY) = *phost;
    } else {
        const URI& uri = h.uri();
        std::string* val = &msg->push(common->H2_AUTHORITY);
        if (!uri.host().empty()) {
            if (uri.port() < 0) {
                *val = uri.host();
            } else {
                butil::string_printf(val, "%s:%d", uri.host().c_str(), uri.port());
            }
        } else if (c->remote_side().port != 0) {
            *val = butil::endpoint2str(c->remote_side()).c_str();
        }
    }
    if (need_content_length) {
        butil::string_printf(&msg->push(common->CONTENT_LENGTH),
                            "%" PRIu64, c->request_attachment().size());
    }
    if (need_content_type) {
        msg->push(common->CONTENT_TYPE, h.content_type());
    }
    if (need_accept) {
        msg->push(common->ACCEPT, common->DEFAULT_ACCEPT);
    }
    if (need_user_agent) {
        msg->push(common->USER_AGENT, common->DEFAULT_USER_AGENT);
    }
    if (need_authorization) {
        // NOTE: just assume user_info is well formatted, namely
        // "<user_name>:<password>". Users are very unlikely to add extra
        // characters in this part and even if users did, most of them are
        // invalid and rejected by http_parser_parse_url().
        std::string encoded_user_info;
        butil::Base64Encode(user_info, &encoded_user_info);
        std::string* val = &msg->push(common->AUTHORIZATION);
        val->reserve(6 + encoded_user_info.size());
        val->append("Basic ");
        val->append(encoded_user_info);
    }
    msg->_sctx.reset(new H2StreamContext);
    msg->_sctx->set_correlation_id(correlation_id);
    return msg;
}

void H2UnsentRequest::Destroy() {
    for (size_t i = 0; i < _size; ++i) {
        _list[i].~Header();
    }
    this->~H2UnsentRequest();
    free(this);
}

void H2UnsentRequest::ReplaceSocketForStream(SocketUniquePtr*, Controller*) {
}

void H2UnsentRequest::OnStreamCreationDone(
    SocketUniquePtr& sending_sock, Controller* cntl) {
    if (sending_sock != NULL && cntl->ErrorCode() != 0) {
        CHECK_EQ(_cntl, cntl);
        _mutex.lock();
        _cntl = NULL;
        if (_stream_id != 0) {
            H2Context* ctx =
                static_cast<H2Context*>(sending_sock->parsing_context());
            ctx->AddAbandonedStream(_stream_id);
        }
        _mutex.unlock();
    }
    RemoveRefManually();
}

void H2UnsentRequest::CleanupSocketForStream(
    Socket* prev_sock, Controller* cntl, int error_code) {
}

struct RemoveRefOnQuit {
    RemoveRefOnQuit(H2UnsentRequest* msg) : _msg(msg) {}
    ~RemoveRefOnQuit() { _msg->RemoveRefManually(); }
private:
    DISALLOW_COPY_AND_ASSIGN(RemoveRefOnQuit);
    H2UnsentRequest* _msg;
};

// bvar::Adder<int64_t> g_append_request_time;
// bvar::PerSecond<bvar::Adder<int64_t> > g_append_request_time_per_second(
//     "h2_append_request_second",     &g_append_request_time);

butil::Status
H2UnsentRequest::AppendAndDestroySelf(butil::IOBuf* out, Socket* socket) {
    //bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_append_request_time);
    RemoveRefOnQuit deref_self(this);
    if (socket == NULL) {
        return butil::Status::OK();
    }
    H2Context* ctx = static_cast<H2Context*>(socket->parsing_context());

    // Create a http2 stream and store correlation_id in.
    if (ctx == NULL) {
        CHECK(socket->CreatedByConnect());
        ctx = new H2Context(socket, NULL);
        if (ctx->Init() != 0) {
            delete ctx;
            socket->SetFailed(EFAILEDSOCKET, "Fail to init H2Context");
            return butil::Status(EFAILEDSOCKET, "Fail to init H2Context");
        }
        socket->initialize_parsing_context(&ctx);
        
        // Append client connection preface
        out->append(H2_CONNECTION_PREFACE_PREFIX,
                    H2_CONNECTION_PREFACE_PREFIX_SIZE);
        char headbuf[FRAME_HEAD_SIZE];
        SerializeFrameHead(headbuf, 0, H2_FRAME_SETTINGS, 0, 0);
        out->append(headbuf, FRAME_HEAD_SIZE);
    }

    std::unique_lock<butil::Mutex> mu(_mutex);
    if (_cntl == NULL) {
        return butil::Status(ECANCELED, "The RPC was already failed");
    }

    // TODO(zhujiashun): also check this in server push
    if (ctx->StreamSize() > ctx->remote_settings().max_concurrent_streams) {
        return butil::Status(EAGAIN, "Pending Stream count exceeds max concurrent stream");
    }

    // Although the critical section looks huge, it should rarely be contended
    // since timeout of RPC is much larger than the delay of sending.
    const int id = ctx->AllocateClientStreamId();
    if (id < 0) {
        // OK to fail in http2, choose a retryable errno
        socket->SetFailed(EFAILEDSOCKET, "Fail to create http2 stream");
        return butil::Status(EFAILEDSOCKET, "Fail to create http2 stream");
    }
    H2StreamContext* sctx = _sctx.release();
    sctx->Init(ctx, id);
    if (!ctx->TryToInsertStream(id, sctx)) {
        delete sctx;
        return butil::Status(ECANCELED, "stream_id already exists");
    }
    _stream_id = sctx->stream_id();

    HPacker& hpacker = ctx->hpacker();
    butil::IOBufAppender appender;
    HPackOptions options;
    options.encode_name = FLAGS_http2_hpack_encode_name;
    options.encode_value = FLAGS_http2_hpack_encode_value;
    for (size_t i = 0; i < _size; ++i) {
        hpacker.Encode(&appender, _list[i], options);
    }
    if (_cntl->has_http_request()) {
        const HttpHeader& h = _cntl->http_request();
        for (HttpHeader::HeaderIterator it = h.HeaderBegin();
             it != h.HeaderEnd(); ++it) {
            HPacker::Header header(it->first, it->second);
            hpacker.Encode(&appender, header, options);
        }
    }
    butil::IOBuf frag;
    appender.move_to(frag);

    // flow control
    int64_t s_win = sctx->_remote_window_size.load(butil::memory_order_relaxed);
    int64_t c_win = ctx->_remote_conn_window_size.load(butil::memory_order_relaxed);
    const int64_t sz = _cntl->request_attachment().size();
    if (sz > s_win || sz > c_win) {
        return butil::Status(EAGAIN, "Remote window size is not enough(flow control)");
    }
    if (!consume_window_size(&sctx->_remote_window_size, sz) ||
        !consume_window_size(&ctx->_remote_conn_window_size, sz)) {
        return butil::Status(EAGAIN, "Remote window size is not enough(flow control)");
    }

    PackH2Message(out, frag, _cntl->request_attachment(),
                  _stream_id, ctx->remote_settings());
    return butil::Status::OK();
}

size_t H2UnsentRequest::EstimatedByteSize() {
    size_t sz = 0;
    for (size_t i = 0; i < _size; ++i) {
        sz += _list[i].name.size() + _list[i].value.size() + 1;
    }
    std::unique_lock<butil::Mutex> mu(_mutex);
    if (_cntl == NULL) {
        return 0;
    }
    if (_cntl->has_http_request()) {
        const HttpHeader& h = _cntl->http_request();
        for (HttpHeader::HeaderIterator it = h.HeaderBegin();
             it != h.HeaderEnd(); ++it) {
            sz += it->first.size() + it->second.size() + 1;
        }
    }
    sz += _cntl->request_attachment().size();
    return sz;
}

void H2UnsentRequest::Describe(butil::IOBuf* desc) const {
    butil::IOBufBuilder os;
    os << "[ H2 REQUEST @" << butil::my_ip() << " ]\n";
    for (size_t i = 0; i < _size; ++i) {
        os << "> " << _list[i].name << " = " << _list[i].value << '\n';
    }
    std::unique_lock<butil::Mutex> mu(_mutex);
    if (_cntl == NULL) {
        return;
    }
    if (_cntl->has_http_request()) {
        const HttpHeader& h = _cntl->http_request();
        for (HttpHeader::HeaderIterator it = h.HeaderBegin();
             it != h.HeaderEnd(); ++it) {
            os << "> " << it->first << " = " << it->second << '\n';
        }
    }
    const butil::IOBuf* body = &_cntl->request_attachment();
    if (!body->empty()) {
        os << "> \n";
    }
    os.move_to(*desc);
    if (body->size() > (size_t)FLAGS_http_verbose_max_body_length) {
        size_t nskipped = body->size() - (size_t)FLAGS_http_verbose_max_body_length;
        body->append_to(desc, FLAGS_http_verbose_max_body_length);
        if (nskipped) {
            char str[32];
            snprintf(str, sizeof(str), "\n<skipped %" PRIu64 " bytes>", nskipped);
            desc->append(str);
        }
    } else {
        desc->append(*body);
    }
}

H2UnsentResponse* H2UnsentResponse::New(Controller* c) {
    const HttpHeader* const h = &c->http_response();
    const CommonStrings* const common = get_common_strings();
    const bool need_content_length =
        (c->Failed() || !c->has_progressive_writer());
    const bool need_content_type = !h->content_type().empty();
    const size_t maxsize = 1
        + (size_t)need_content_length
        + (size_t)need_content_type;
    const size_t memsize = offsetof(H2UnsentResponse, _list) +
        sizeof(HPacker::Header) * maxsize;
    H2UnsentResponse* msg = new (malloc(memsize)) H2UnsentResponse(c);
    // :status
    if (h->status_code() == 200) {
        msg->push(common->H2_STATUS, common->STATUS_200);
    } else {
        butil::string_printf(&msg->push(common->H2_STATUS),
                            "%d", h->status_code());
    }
    if (need_content_length) {
        butil::string_printf(&msg->push(common->CONTENT_LENGTH),
                            "%" PRIu64, msg->_data.size());
    }
    if (need_content_type) {
        msg->push(common->CONTENT_TYPE, h->content_type());
    }
    return msg;
}

void H2UnsentResponse::Destroy() {
    for (size_t i = 0; i < _size; ++i) {
        _list[i].~Header();
    }
    this->~H2UnsentResponse();
    free(this);
}

// bvar::Adder<int64_t> g_append_response_time;
// bvar::PerSecond<bvar::Adder<int64_t> > g_append_response_time_per_second(
//     "h2_append_response_second",     &g_append_response_time);

butil::Status
H2UnsentResponse::AppendAndDestroySelf(butil::IOBuf* out, Socket* socket) {
    //bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_append_response_time);
    DestroyingPtr<H2UnsentResponse> destroy_self(this);
    if (socket == NULL) {
        return butil::Status::OK();
    }
    H2Context* ctx = static_cast<H2Context*>(socket->parsing_context());
    
    HPacker& hpacker = ctx->hpacker();
    butil::IOBufAppender appender;
    HPackOptions options;
    options.encode_name = FLAGS_http2_hpack_encode_name;
    options.encode_value = FLAGS_http2_hpack_encode_value;

    for (size_t i = 0; i < _size; ++i) {
        hpacker.Encode(&appender, _list[i], options);
    }
    if (_http_response) {
        for (HttpHeader::HeaderIterator it = _http_response->HeaderBegin();
             it != _http_response->HeaderEnd(); ++it) {
            HPacker::Header header(it->first, it->second);
            hpacker.Encode(&appender, header, options);
        }
    }
    butil::IOBuf frag;
    appender.move_to(frag);

    // flow control
    int64_t c_win = ctx->_remote_conn_window_size.load(butil::memory_order_relaxed);
    const int64_t sz = _data.size();
    if ((sz > c_win) || !consume_window_size(&ctx->_remote_conn_window_size, sz)) {
        return butil::Status(EAGAIN, "Remote window size is not enough(flow control)");
    }
    
    PackH2Message(out, frag, _data, _stream_id, ctx->remote_settings());
    return butil::Status::OK();
}

size_t H2UnsentResponse::EstimatedByteSize() {
    size_t sz = 0;
    for (size_t i = 0; i < _size; ++i) {
        sz += _list[i].name.size() + _list[i].value.size() + 1;
    }
    if (_http_response) {
        for (HttpHeader::HeaderIterator it = _http_response->HeaderBegin();
             it != _http_response->HeaderEnd(); ++it) {
            sz += it->first.size() + it->second.size() + 1;
        }
    }
    sz += _data.size();
    return sz;
}

void H2UnsentResponse::Describe(butil::IOBuf* desc) const {
    butil::IOBufBuilder os;
    os << "[ H2 RESPONSE @" << butil::my_ip() << " ]\n";
    for (size_t i = 0; i < _size; ++i) {
        os << "> " << _list[i].name << " = " << _list[i].value << '\n';
    }
    if (_http_response) {
        for (HttpHeader::HeaderIterator it = _http_response->HeaderBegin();
             it != _http_response->HeaderEnd(); ++it) {
            os << "> " << it->first << " = " << it->second << '\n';
        }
    }
    if (!_data.empty()) {
        os << "> \n";
    }
    os.move_to(*desc);
    if (_data.size() > (size_t)FLAGS_http_verbose_max_body_length) {
        size_t nskipped = _data.size() - (size_t)FLAGS_http_verbose_max_body_length;
        _data.append_to(desc, FLAGS_http_verbose_max_body_length);
        if (nskipped) {
            char str[32];
            snprintf(str, sizeof(str), "\n<skipped %" PRIu64 " bytes>", nskipped);
            desc->append(str);
        }
    } else {
        desc->append(_data);
    }
}

void PackH2Request(butil::IOBuf*,
                      SocketMessage** user_message,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor*,
                      Controller* cntl,
                      const butil::IOBuf&,
                      const Authenticator* auth) {
    ControllerPrivateAccessor accessor(cntl);
    
    HttpHeader* header = &cntl->http_request();
    if (auth != NULL && header->GetHeader("Authorization") == NULL) {
        std::string auth_data;
        if (auth->GenerateCredential(&auth_data) != 0) {
            return cntl->SetFailed(EREQUEST, "Fail to GenerateCredential");
        }
        header->SetHeader("Authorization", auth_data);
    }

    // Serialize http2 request
    H2UnsentRequest* h2_req = H2UnsentRequest::New(cntl, correlation_id);
    if (cntl->stream_creator() &&
        cntl->stream_creator() != get_h2_global_stream_creator()) {
        static_cast<H2UnsentRequest*>(cntl->stream_creator())->RemoveRefManually();
    }
    cntl->set_stream_creator(h2_req);
    h2_req->AddRefManually();
    *user_message = h2_req;
    
    if (FLAGS_http_verbose) {
        butil::IOBuf desc;
        h2_req->Describe(&desc);
        std::cerr << desc << std::endl;
    }
}

void H2GlobalStreamCreator::ReplaceSocketForStream(
    SocketUniquePtr* inout, Controller* cntl) {
    // Although the critical section looks huge, it should rarely be contended
    // since timeout of RPC is much larger than the delay of sending.
    std::unique_lock<butil::Mutex> mu(_mutex);
    do {
        if (!(*inout)->_agent_socket) {
            break;
        }
        H2Context* ctx = static_cast<H2Context*>((*inout)->_agent_socket->parsing_context());
        // According to https://httpwg.org/specs/rfc7540.html#StreamIdentifiers:
        // A client that is unable to establish a new stream identifier can establish
        // a new connection for new streams. 
        if (ctx && ctx->RunOutStreams()) {
            break;
        }
        (*inout)->_agent_socket->ReAddress(inout);
        return;
    } while (0);

    SocketId sid;
    SocketOptions opt = (*inout)->_options;
    // Only main socket can be the owner of ssl_ctx
    opt.owns_ssl_ctx = false;
    opt.health_check_interval_s = -1;
    // TODO(zhujiashun): Predictively create socket to improve performance
    if (get_client_side_messenger()->Create(opt, &sid) != 0) {
        cntl->SetFailed(EINVAL, "Fail to create H2 socket");
        return;
    }
    SocketUniquePtr tmp_ptr;
    if (Socket::Address(sid, &tmp_ptr) != 0) {
        cntl->SetFailed(EFAILEDSOCKET, "Fail to address H2 socketId=%" PRIu64, sid);
        return;
    }
    (*inout)->_agent_socket.swap(tmp_ptr);
    mu.unlock();
    (*inout)->_agent_socket->ReAddress(inout);
    if (tmp_ptr) {
        tmp_ptr->ReleaseAdditionalReference();
    }
    return;
}

void H2GlobalStreamCreator::OnStreamCreationDone(
    SocketUniquePtr& sending_sock, Controller* cntl) {
    // If any error happens during the time of sending rpc, this function
    // would be called. Currently just do nothing.
}

void H2GlobalStreamCreator::CleanupSocketForStream(
    Socket* prev_sock, Controller* cntl, int error_code) {
}

StreamCreator* get_h2_global_stream_creator() {
    return butil::get_leaky_singleton<H2GlobalStreamCreator>();
}

}  // namespace policy

} // namespace brpc
