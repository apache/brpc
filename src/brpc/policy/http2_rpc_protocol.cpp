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


#include "brpc/policy/http2_rpc_protocol.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/server.h"
#include "butil/base64.h"
#include "brpc/log.h"

namespace brpc {

DECLARE_bool(http_verbose);
DECLARE_int32(http_verbose_max_body_length);
DECLARE_int32(health_check_interval);
DECLARE_bool(usercode_in_pthread);

namespace policy {

DEFINE_int32(h2_client_header_table_size,
             H2Settings::DEFAULT_HEADER_TABLE_SIZE,
             "maximum size of compression tables for decoding headers");
DEFINE_int32(h2_client_stream_window_size, 256 * 1024,
             "Initial window size for stream-level flow control");
DEFINE_int32(h2_client_connection_window_size, 1024 * 1024,
             "Initial window size for connection-level flow control");
DEFINE_int32(h2_client_max_frame_size,
             H2Settings::DEFAULT_MAX_FRAME_SIZE,
             "Size of the largest frame payload that client is willing to receive");

DEFINE_bool(h2_hpack_encode_name, false,
            "Encode name in HTTP2 headers with huffman encoding");
DEFINE_bool(h2_hpack_encode_value, false,
            "Encode value in HTTP2 headers with huffman encoding");

static bool CheckStreamWindowSize(const char*, int32_t val) {
    return val >= 0;
}
BRPC_VALIDATE_GFLAG(h2_client_stream_window_size, CheckStreamWindowSize);

static bool CheckConnWindowSize(const char*, int32_t val) {
    return val >= (int32_t)H2Settings::DEFAULT_INITIAL_WINDOW_SIZE;
}
BRPC_VALIDATE_GFLAG(h2_client_connection_window_size, CheckConnWindowSize);

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

static const char* H2ConnectionState2Str(H2ConnectionState s) {
    switch (s) {
    case H2_CONNECTION_UNINITIALIZED: return "UNINITIALIZED";
    case H2_CONNECTION_READY: return "READY";
    case H2_CONNECTION_GOAWAY: return "GOAWAY";
    }
    return "UNKNOWN(H2ConnectionState)";
}

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

void SerializeFrameHead(void* out_buf,
                        uint32_t payload_size, H2FrameType type,
                        uint8_t flags, uint32_t stream_id) {
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

static int WriteAck(Socket* s, const void* data, size_t n) {
    butil::IOBuf sendbuf;
    sendbuf.append(data, n);
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    return s->Write(&sendbuf, &wopt);
}

// [ https://tools.ietf.org/html/rfc7540#section-6.5.1 ]

enum H2SettingsIdentifier {
    H2_SETTINGS_HEADER_TABLE_SIZE      = 0x1,
    H2_SETTINGS_ENABLE_PUSH            = 0x2,
    H2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    H2_SETTINGS_STREAM_WINDOW_SIZE     = 0x4,
    H2_SETTINGS_MAX_FRAME_SIZE         = 0x5,
    H2_SETTINGS_MAX_HEADER_LIST_SIZE   = 0x6
};

// Parse from n bytes from the iterator.
// Returns true on success.
bool ParseH2Settings(H2Settings* out, butil::IOBufBytesIterator& it, size_t n) {
    const uint32_t npairs = n / 6;
    if (npairs * 6 != n) {
        LOG(ERROR) << "Invalid payload_size=" << n;
        return false;
    }
    for (uint32_t i = 0; i < npairs; ++i) {
        uint16_t id = LoadUint16(it);
        uint32_t value = LoadUint32(it);
        switch (static_cast<H2SettingsIdentifier>(id)) {
        case H2_SETTINGS_HEADER_TABLE_SIZE:
            out->header_table_size = value;
            break;
        case H2_SETTINGS_ENABLE_PUSH:
            if (value > 1) {
                LOG(ERROR) << "Invalid value=" << value << " for ENABLE_PUSH";
                return false;
            }
            out->enable_push = value;
            break;
        case H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            out->max_concurrent_streams = value;
            break;
        case H2_SETTINGS_STREAM_WINDOW_SIZE:
            if (value > H2Settings::MAX_WINDOW_SIZE) {
                LOG(ERROR) << "Invalid stream_window_size=" << value;
                return false;
            }
            out->stream_window_size = value;
            break;
        case H2_SETTINGS_MAX_FRAME_SIZE:
            if (value > H2Settings::MAX_OF_MAX_FRAME_SIZE ||
                value < H2Settings::DEFAULT_MAX_FRAME_SIZE) {
                LOG(ERROR) << "Invalid max_frame_size=" << value;
                return false;
            }
            out->max_frame_size = value;
            break;
        case H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            out->max_header_list_size = value;
            break;
        default:
            // An endpoint that receives a SETTINGS frame with any unknown or
            // unsupported identifier MUST ignore that setting (section 6.5.2)
            LOG(WARNING) << "Unknown setting, id=" << id << " value=" << value;
            break;
        }
    }
    return true;
}

// Maximum value that may be returned by SerializeH2Settings
static const size_t H2_SETTINGS_MAX_BYTE_SIZE = 36;
    
// Serialize to `out' which is at least ByteSize() bytes long.
// Returns bytes written.
size_t SerializeH2Settings(const H2Settings& in, void* out) {
    uint8_t* p = (uint8_t*)out;
    if (in.header_table_size != H2Settings::DEFAULT_HEADER_TABLE_SIZE) {
        SaveUint16(p, H2_SETTINGS_HEADER_TABLE_SIZE);
        SaveUint32(p + 2, in.header_table_size);
        p += 6;
    }
    if (in.enable_push != H2Settings::DEFAULT_ENABLE_PUSH) {
        SaveUint16(p, H2_SETTINGS_ENABLE_PUSH);
        SaveUint32(p + 2, in.enable_push);
        p += 6;
    }
    if (in.max_concurrent_streams != std::numeric_limits<uint32_t>::max()) {
        SaveUint16(p, H2_SETTINGS_MAX_CONCURRENT_STREAMS);
        SaveUint32(p + 2, in.max_concurrent_streams);
        p += 6;
    }
    if (in.stream_window_size != H2Settings::DEFAULT_INITIAL_WINDOW_SIZE) {
        SaveUint16(p, H2_SETTINGS_STREAM_WINDOW_SIZE);
        SaveUint32(p + 2, in.stream_window_size);
        p += 6;
    }
    if (in.max_frame_size != H2Settings::DEFAULT_MAX_FRAME_SIZE) {
        SaveUint16(p, H2_SETTINGS_MAX_FRAME_SIZE);
        SaveUint32(p + 2, in.max_frame_size);
        p += 6;
    }
    if (in.max_header_list_size != std::numeric_limits<uint32_t>::max()) {
        SaveUint16(p, H2_SETTINGS_MAX_HEADER_LIST_SIZE);
        SaveUint32(p + 2, in.max_header_list_size);
        p += 6;
    }
    return static_cast<size_t>(p - (uint8_t*)out);
}

static size_t SerializeH2SettingsFrameAndWU(const H2Settings& in, void* out) {
    uint8_t* p = (uint8_t*)out;
    size_t nb = SerializeH2Settings(in, p + FRAME_HEAD_SIZE);
    SerializeFrameHead(p, nb, H2_FRAME_SETTINGS, 0, 0);
    p += FRAME_HEAD_SIZE + nb;
    if (in.connection_window_size > H2Settings::DEFAULT_INITIAL_WINDOW_SIZE) {
        SerializeFrameHead(p, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
        SaveUint32(p + FRAME_HEAD_SIZE,
                   in.connection_window_size - H2Settings::DEFAULT_INITIAL_WINDOW_SIZE);
        p += FRAME_HEAD_SIZE + 4;
    }
    return static_cast<size_t>(p - (uint8_t*)out);
}

inline bool AddWindowSize(butil::atomic<int64_t>* window_size, int64_t diff) {
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

inline bool MinusWindowSize(butil::atomic<int64_t>* window_size, int64_t size) {
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
void InitFrameHandlers() {
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
    // Maximize the window size to make sending big request possible before
    // receving the remote settings.
    , _remote_window_left(H2Settings::MAX_WINDOW_SIZE)
    , _conn_state(H2_CONNECTION_UNINITIALIZED)
    , _last_received_stream_id(-1)
    , _last_sent_stream_id(1)
    , _goaway_stream_id(-1)
    , _remote_settings_received(false)
    , _deferred_window_update(0) {
    // Stop printing the field which is useless for remote settings.
    _remote_settings.connection_window_size = 0;
    // Maximize the window size to make sending big request possible before
    // receving the remote settings.
    _remote_settings.stream_window_size = H2Settings::MAX_WINDOW_SIZE;
    if (server) {
        _unack_local_settings = server->options().h2_settings;
    } else {
        _unack_local_settings.header_table_size = FLAGS_h2_client_header_table_size;
        _unack_local_settings.stream_window_size = FLAGS_h2_client_stream_window_size;
        _unack_local_settings.max_frame_size = FLAGS_h2_client_max_frame_size;
        _unack_local_settings.connection_window_size = FLAGS_h2_client_connection_window_size;
    }
#if defined(UNIT_TEST)
    // In ut, we hope _last_sent_stream_id run out quickly to test the correctness
    // of creating new h2 socket. This value is 10,000 less than 0x7FFFFFFF.
    _last_sent_stream_id = 0x7fffd8ef;
#endif
}

H2Context::~H2Context() {
    for (StreamMap::iterator it = _pending_streams.begin();
         it != _pending_streams.end(); ++it) {
        delete it->second;
    }
    _pending_streams.clear();
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

H2StreamContext* H2Context::RemoveStream(int stream_id) {
    H2StreamContext* sctx = NULL;
    {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        if (!_pending_streams.erase(stream_id, &sctx)) {
            return NULL;
        }
    }
    // The remote stream will not send any more data, sending back the
    // stream-level WINDOW_UPDATE is pointless, just move the value into
    // the connection.
    DeferWindowUpdate(sctx->ReleaseDeferredWindowUpdate());
    return sctx;
}

void H2Context::RemoveGoAwayStreams(
    int goaway_stream_id, std::vector<H2StreamContext*>* out_streams) {
    out_streams->clear();
    if (goaway_stream_id == 0) {  // quick path
        StreamMap tmp;
        {
            std::unique_lock<butil::Mutex> mu(_stream_mutex);
            _goaway_stream_id = goaway_stream_id;
            _pending_streams.swap(tmp);
        }
        for (StreamMap::const_iterator it = tmp.begin(); it != tmp.end(); ++it) {
            out_streams->push_back(it->second);
        }
    } else {
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        _goaway_stream_id = goaway_stream_id;
        for (StreamMap::const_iterator it = _pending_streams.begin();
             it != _pending_streams.end(); ++it) {
            if (it->first > goaway_stream_id) {
                out_streams->push_back(it->second);
            }
        }
        for (size_t i = 0; i < out_streams->size(); ++i) {
            _pending_streams.erase((*out_streams)[i]->stream_id());
        }
    }
}

H2StreamContext* H2Context::FindStream(int stream_id) {
    std::unique_lock<butil::Mutex> mu(_stream_mutex);
    H2StreamContext** psctx = _pending_streams.seek(stream_id);
    if (psctx) {
        return *psctx;
    }
    return NULL;
}

int H2Context::TryToInsertStream(int stream_id, H2StreamContext* ctx) {
    std::unique_lock<butil::Mutex> mu(_stream_mutex);
    if (_goaway_stream_id >= 0 && stream_id > _goaway_stream_id) {
        return 1;
    }
    H2StreamContext*& sctx = _pending_streams[stream_id];
    if (sctx == NULL) {
        sctx = ctx;
        return 0;
    }
    return -1;
}

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
        LOG(ERROR) << "Too large frame length=" << length << " max="
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

            char settingsbuf[FRAME_HEAD_SIZE + H2_SETTINGS_MAX_BYTE_SIZE +
                             FRAME_HEAD_SIZE + 4/*for WU*/];
            const size_t nb = SerializeH2SettingsFrameAndWU(_unack_local_settings, settingsbuf);
            if (WriteAck(socket, settingsbuf, nb) != 0) {
                LOG(WARNING) << "Fail to respond http2-client with settings to " << *socket;
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
            if (WriteAck(_socket, rstbuf, sizeof(rstbuf)) != 0) {
                LOG(WARNING) << "Fail to send RST_STREAM to " << *_socket;
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            H2StreamContext* sctx = RemoveStream(h2_res.stream_id());
            if (sctx) {
                if (is_server_side()) {
                    delete sctx;
                    return MakeMessage(NULL);
                } else {
                    sctx->header().set_status_code(
                            H2ErrorToStatusCode(h2_res.error()));
                    return MakeMessage(sctx);
                }
            }
            return MakeMessage(NULL);
        } else { // send GOAWAY
            char goawaybuf[FRAME_HEAD_SIZE + 8];
            SerializeFrameHead(goawaybuf, 8, H2_FRAME_GOAWAY, 0, 0);
            SaveUint32(goawaybuf + FRAME_HEAD_SIZE, _last_received_stream_id);
            SaveUint32(goawaybuf + FRAME_HEAD_SIZE + 4, h2_res.error());
            if (WriteAck(_socket, goawaybuf, sizeof(goawaybuf)) != 0) {
                LOG(WARNING) << "Fail to send GOAWAY to " << *_socket;
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            return MakeMessage(NULL);
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
        frame_head.stream_id > _last_received_stream_id) { // new stream
        if ((frame_head.stream_id & 1) == 0) {
            LOG(ERROR) << "stream_id=" << frame_head.stream_id
                       << " created by client is not odd";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        _last_received_stream_id = frame_head.stream_id;
        sctx = new H2StreamContext(_socket->is_read_progressive());
        sctx->Init(this, frame_head.stream_id);
        const int rc = TryToInsertStream(frame_head.stream_id, sctx);
        if (rc < 0) {
            delete sctx;
            LOG(ERROR) << "Fail to insert existing stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        } else if (rc > 0) {
            delete sctx;
            return MakeH2Error(H2_REFUSED_STREAM);
        }
    } else {
        sctx = FindStream(frame_head.stream_id);
        if (sctx == NULL) {
            if (is_client_side()) {
                RPC_VLOG << "Fail to find stream_id=" << frame_head.stream_id;
                // Ignore the message without closing the socket.
                H2StreamContext tmp_sctx(false);
                tmp_sctx.Init(this, frame_head.stream_id);
                tmp_sctx.OnHeaders(it, frame_head, frag_size, pad_length);
                return MakeH2Message(NULL);
            } else {
                LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
                return MakeH2Error(H2_PROTOCOL_ERROR);
            }
        }
    }
    return sctx->OnHeaders(it, frame_head, frag_size, pad_length);
}

H2ParseResult H2StreamContext::OnHeaders(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head,
    uint32_t frag_size, uint8_t pad_length) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
#if defined(BRPC_H2_STREAM_STATE)
    SetState(H2_STREAM_OPEN);
#endif
    butil::IOBufBytesIterator it2(it, frag_size);
    if (ConsumeHeaders(it2) < 0) {
        LOG(ERROR) << "Invalid header, frag_size=" << frag_size
            << ", stream_id=" << frame_head.stream_id;
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
            LOG(ERROR) << "Incomplete header: payload_size=" << frame_head.payload_size
                << ", stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        if (frame_head.flags & H2_FLAGS_END_STREAM) {
            return OnEndStream();
        }
        return MakeH2Message(NULL);
    } else {
        if (frame_head.flags & H2_FLAGS_END_STREAM) {
            // Delay calling OnEndStream() in OnContinuation()
            _stream_ended = true;
        }
        return MakeH2Message(NULL);
    }
}

H2ParseResult H2Context::OnContinuation(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    H2StreamContext* sctx = FindStream(frame_head.stream_id);
    if (sctx == NULL) {
        if (is_client_side()) {
            RPC_VLOG << "Fail to find stream_id=" << frame_head.stream_id;
            // Ignore the message without closing the socket.
            H2StreamContext tmp_sctx(false);
            tmp_sctx.Init(this, frame_head.stream_id);
            tmp_sctx.OnContinuation(it, frame_head);
            return MakeH2Message(NULL);
        } else {
            LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
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
        LOG(ERROR) << "Invalid header: payload_size=" << frame_head.payload_size
            << ", stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    _remaining_header_fragment.pop_front(size - it2.bytes_left());
    if (frame_head.flags & H2_FLAGS_END_HEADERS) {
        if (it2.bytes_left() != 0) {
            LOG(ERROR) << "Incomplete header: payload_size=" << frame_head.payload_size
                << ", stream_id=" << frame_head.stream_id;
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        if (_stream_ended) {
            return OnEndStream();
        }
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
        // If a DATA frame is received whose stream is not in "open" or "half-closed (local)" state,
        // the recipient MUST respond with a stream error (Section 5.4.2) of type STREAM_CLOSED.
        // Ignore the message without closing the socket.
        H2StreamContext tmp_sctx(false);
        tmp_sctx.Init(this, frame_head.stream_id);
        tmp_sctx.OnData(it, frame_head, frag_size, pad_length);
        DeferWindowUpdate(tmp_sctx.ReleaseDeferredWindowUpdate());

        LOG(ERROR) << "Fail to find stream_id=" << frame_head.stream_id;
        return MakeH2Error(H2_STREAM_CLOSED_ERROR, frame_head.stream_id);
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

    const int64_t acc = _deferred_window_update.fetch_add(frag_size, butil::memory_order_relaxed) + frag_size;
    if (acc >= _conn_ctx->local_settings().stream_window_size / 2) {
        if (acc > _conn_ctx->local_settings().stream_window_size) {
            LOG(ERROR) << "Fail to satisfy the stream-level flow control policy";
            return MakeH2Error(H2_FLOW_CONTROL_ERROR, frame_head.stream_id);
        }
        // Rarely happen for small messages.
        const int64_t stream_wu =
            _deferred_window_update.exchange(0, butil::memory_order_relaxed);
        
        if (stream_wu > 0) {
            char winbuf[(FRAME_HEAD_SIZE + 4) * 2];
            char* p = winbuf;

            SerializeFrameHead(p, 4, H2_FRAME_WINDOW_UPDATE, 0, stream_id());
            SaveUint32(p + FRAME_HEAD_SIZE, stream_wu);
            p += FRAME_HEAD_SIZE + 4;

            const int64_t conn_wu = stream_wu + _conn_ctx->ReleaseDeferredWindowUpdate();
            SerializeFrameHead(p, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
            SaveUint32(p + FRAME_HEAD_SIZE, conn_wu);
            if (WriteAck(_conn_ctx->_socket, winbuf, sizeof(winbuf)) != 0) {
                LOG(WARNING) << "Fail to send WINDOW_UPDATE to " << *_conn_ctx->_socket;
                return MakeH2Error(H2_INTERNAL_ERROR);
            }
        }
    }
    if (frame_head.flags & H2_FLAGS_END_STREAM) {
        return OnEndStream();
    }
    return MakeH2Message(NULL);
}

H2ParseResult H2Context::OnResetStream(
    butil::IOBufBytesIterator& it, const H2FrameHead& frame_head) {
    if (frame_head.payload_size != 4) {
        LOG(ERROR) << "Invalid payload_size=" << frame_head.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    const H2Error h2_error = static_cast<H2Error>(LoadUint32(it));
    H2StreamContext* sctx = FindStream(frame_head.stream_id);
    if (sctx == NULL) {
        RPC_VLOG << "Fail to find stream_id=" << frame_head.stream_id;
        return MakeH2Message(NULL);
    }
    return sctx->OnResetStream(h2_error, frame_head);
}

H2ParseResult H2StreamContext::OnResetStream(
    H2Error h2_error, const H2FrameHead& frame_head) {
    _parsed_length += FRAME_HEAD_SIZE + frame_head.payload_size;
#if defined(BRPC_H2_STREAM_STATE)
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
    if (sctx == NULL) {
        LOG(ERROR) << "Fail to find stream_id=" << stream_id();
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    if (_conn_ctx->is_client_side()) {
        sctx->header().set_status_code(H2ErrorToStatusCode(h2_error));
        return MakeH2Message(sctx);
    } else {
        // No need to process the request.
        delete sctx;
        return MakeH2Message(NULL);
    }
}

H2ParseResult H2StreamContext::OnEndStream() {
#if defined(BRPC_H2_STREAM_STATE)
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
    if (sctx == NULL) {
        RPC_VLOG << "Fail to find stream_id=" << stream_id();
        return MakeH2Message(NULL);
    }
    CHECK_EQ(sctx, this);

    OnMessageComplete();
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
    const int64_t old_stream_window_size = _remote_settings.stream_window_size;
    if (!_remote_settings_received) {
        // To solve the problem that sender can't send large request before receving
        // remote setting, the initial window size of stream/connection is set to
        // MAX_WINDOW_SIZE(see constructor of H2Context).
        // As a result, in the view of remote side, window size is 65535 by default so
        // it may not send its stream size to sender, making stream size still be
        // MAX_WINDOW_SIZE. In this case we need to revert this value to default.
        H2Settings tmp_settings;
        if (!ParseH2Settings(&tmp_settings, it, frame_head.payload_size)) {
            LOG(ERROR) << "Fail to parse from SETTINGS";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
        _remote_settings = tmp_settings;
        _remote_window_left.fetch_sub(
                H2Settings::MAX_WINDOW_SIZE - H2Settings::DEFAULT_INITIAL_WINDOW_SIZE,
                butil::memory_order_relaxed);
        _remote_settings_received = true;
    } else {
        if (!ParseH2Settings(&_remote_settings, it, frame_head.payload_size)) {
            LOG(ERROR) << "Fail to parse from SETTINGS";
            return MakeH2Error(H2_PROTOCOL_ERROR);
        }
    }
    const int64_t window_diff =
        static_cast<int64_t>(_remote_settings.stream_window_size)
        - old_stream_window_size;
    if (window_diff) {
        // Do not update the connection flow-control window here, which can only
        // be changed using WINDOW_UPDATE frames.
        // https://tools.ietf.org/html/rfc7540#section-6.9.2
        // TODO(gejun): Has race conditions with AppendAndDestroySelf
        std::unique_lock<butil::Mutex> mu(_stream_mutex);
        for (StreamMap::const_iterator it = _pending_streams.begin();
             it != _pending_streams.end(); ++it) {
            if (!AddWindowSize(&it->second->_remote_window_left, window_diff)) {
                return MakeH2Error(H2_FLOW_CONTROL_ERROR);
            }
        }
    }
    // Respond with ack
    char headbuf[FRAME_HEAD_SIZE];
    SerializeFrameHead(headbuf, 0, H2_FRAME_SETTINGS, H2_FLAGS_ACK, 0);
    if (WriteAck(_socket, headbuf, sizeof(headbuf)) != 0) {
        LOG(WARNING) << "Fail to respond settings with ack to " << *_socket;
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
    if (WriteAck(_socket, pongbuf, sizeof(pongbuf)) != 0) {
        LOG(WARNING) << "Fail to send ack of PING to " << *_socket;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    return MakeH2Message(NULL);
}

static void* ProcessHttpResponseWrapper(void* void_arg) {
    ProcessHttpResponse(static_cast<InputMessageBase*>(void_arg));
    return NULL;
}

H2ParseResult H2Context::OnGoAway(
    butil::IOBufBytesIterator& it, const H2FrameHead& h) {
    if (h.payload_size < 8) {
        LOG(ERROR) << "Invalid payload_size=" << h.payload_size;
        return MakeH2Error(H2_FRAME_SIZE_ERROR);
    }
    if (h.stream_id != 0) {
        LOG(ERROR) << "Invalid stream_id=" << h.stream_id;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    if (h.flags) {
        LOG(ERROR) << "Invalid flags=" << h.flags;
        return MakeH2Error(H2_PROTOCOL_ERROR);
    }
    // Skip Additional Debug Data
    it.forward(h.payload_size - 8);
    const int last_stream_id = static_cast<int>(LoadUint32(it));
    const H2Error ALLOW_UNUSED h2_error = static_cast<H2Error>(LoadUint32(it));
    // TODO(zhujiashun): client and server should unify the code.
    // Server Push is not supported so it works fine now.
    if (is_client_side()) {
        // The socket will not be selected for further requests.
        _socket->SetLogOff();

        std::vector<H2StreamContext*> goaway_streams;
        RemoveGoAwayStreams(last_stream_id, &goaway_streams);
        if (goaway_streams.empty()) {
            return MakeH2Message(NULL);
        }
        for (size_t i = 0; i < goaway_streams.size(); ++i) {
            H2StreamContext* sctx = goaway_streams[i];
            sctx->header().set_status_code(HTTP_STATUS_SERVICE_UNAVAILABLE);
        }
        for (size_t i = 1; i < goaway_streams.size(); ++i) {
            bthread_t th;
            bthread_attr_t tmp = (FLAGS_usercode_in_pthread ?
                                  BTHREAD_ATTR_PTHREAD :
                                  BTHREAD_ATTR_NORMAL);
            tmp.keytable_pool = _socket->keytable_pool();
            CHECK_EQ(0, bthread_start_background(&th, &tmp, ProcessHttpResponseWrapper,
                         static_cast<InputMessageBase*>(goaway_streams[i])));
        }
        return MakeH2Message(goaway_streams[0]);
    } else {
        // server serves requests on-demand, ignoring GOAWAY is OK.
        return MakeH2Message(NULL);
    }
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
        if (!AddWindowSize(&_remote_window_left, inc)) {
            LOG(ERROR) << "Invalid connection-level window_size_increment=" << inc;
            return MakeH2Error(H2_FLOW_CONTROL_ERROR);
        }
        return MakeH2Message(NULL);
    } else {
        H2StreamContext* sctx = FindStream(frame_head.stream_id);
        if (sctx == NULL) {
            RPC_VLOG << "Fail to find stream_id=" << frame_head.stream_id;
            return MakeH2Message(NULL);
        }
        if (!AddWindowSize(&sctx->_remote_window_left, inc)) {
            LOG(ERROR) << "Invalid stream-level window_size_increment=" << inc
                << " to remote_window_left=" << sctx->_remote_window_left.load(butil::memory_order_relaxed);
            return MakeH2Error(H2_FLOW_CONTROL_ERROR);
        }
        return MakeH2Message(NULL);
    }
}

void H2Context::Describe(std::ostream& os, const DescribeOptions& opt) const {
    if (opt.verbose) {
        os << '\n';
    }
    const char sep = (opt.verbose ? '\n' : ' ');
    os << "conn_state=" << H2ConnectionState2Str(_conn_state);
    os << sep << "last_received_stream_id=" << _last_received_stream_id
       << sep << "last_sent_stream_id=" << _last_sent_stream_id;
    os << sep << "deferred_window_update="
       << _deferred_window_update.load(butil::memory_order_relaxed)
       << sep << "remote_conn_window_left="
       << _remote_window_left.load(butil::memory_order_relaxed)
       << sep << "remote_settings=" << _remote_settings
       << sep << "remote_settings_received=" << _remote_settings_received
       << sep << "local_settings=" << _local_settings
       << sep << "hpacker={";
    IndentingOStream os2(os, 2);
    _hpacker.Describe(os2, opt);
    os << '}';
    size_t abandoned_size = 0;
    {
        BAIDU_SCOPED_LOCK(_abandoned_streams_mutex);
        abandoned_size = _abandoned_streams.size();
    }
    os << sep << "abandoned_streams=" << abandoned_size
       << sep << "pending_streams=" << VolatilePendingStreamSize();
    if (opt.verbose) {
        os << '\n';
    }
}

inline int64_t H2Context::ReleaseDeferredWindowUpdate() {
    if (_deferred_window_update.load(butil::memory_order_relaxed) == 0) {
        return 0;
    }
    return _deferred_window_update.exchange(0, butil::memory_order_relaxed);
}

void H2Context::DeferWindowUpdate(int64_t size) {
    if (size <= 0) {
        return;
    }
    const int64_t acc = _deferred_window_update.fetch_add(size, butil::memory_order_relaxed) + size;
    if (acc >= local_settings().stream_window_size / 2) {
        // Rarely happen for small messages.
        const int64_t conn_wu = _deferred_window_update.exchange(0, butil::memory_order_relaxed);
        if (conn_wu > 0) {
            char winbuf[FRAME_HEAD_SIZE + 4];
            SerializeFrameHead(winbuf, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
            SaveUint32(winbuf + FRAME_HEAD_SIZE, conn_wu);
            if (WriteAck(_socket, winbuf, sizeof(winbuf)) != 0) {
                LOG(WARNING) << "Fail to send WINDOW_UPDATE";
            }
        }
    }
}

#if defined(BRPC_PROFILE_H2)
bvar::Adder<int64_t> g_parse_time;
bvar::PerSecond<bvar::Adder<int64_t> > g_parse_time_per_second(
    "h2_parse_second", &g_parse_time);
#endif

ParseResult ParseH2Message(butil::IOBuf *source, Socket *socket,
                           bool read_eof, const void *arg) {
#if defined(BRPC_PROFILE_H2)
    bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_parse_time);
#endif
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

H2StreamContext::H2StreamContext(bool read_body_progressively)
    : HttpContext(read_body_progressively)
    , _conn_ctx(NULL)
#if defined(BRPC_H2_STREAM_STATE)
    , _state(H2_STREAM_IDLE)
#endif
    , _stream_id(0)
    , _stream_ended(false)
    , _remote_window_left(0)
    , _deferred_window_update(0)
    , _correlation_id(INVALID_BTHREAD_ID.value) {
    header().set_version(2, 0);
#ifndef NDEBUG
    get_h2_bvars()->h2_stream_context_count << 1;
#endif
}

void H2StreamContext::Init(H2Context* conn_ctx, int stream_id) {
    _conn_ctx = conn_ctx;
    _stream_id = stream_id;
    _remote_window_left.store(conn_ctx->remote_settings().stream_window_size,
                              butil::memory_order_relaxed);
}

H2StreamContext::~H2StreamContext() {
#ifndef NDEBUG
    get_h2_bvars()->h2_stream_context_count << -1;
#endif
}

#if defined(BRPC_H2_STREAM_STATE)
void H2StreamContext::SetState(H2StreamState state) {
    const H2StreamState old_state = _state;
    _state = state;
    RPC_VLOG << "stream_id=" << stream_id() << " changed from "
             << H2StreamState2Str(old_state) << " to "
             << H2StreamState2Str(state);
}
#endif

bool H2StreamContext::ConsumeWindowSize(int64_t size) {
    // This method is guaranteed to be called in AppendAndDestroySelf() which
    // is run sequentially. As a result, _remote_window_left of this stream
    // context will not be decremented (may be incremented) because following
    // AppendAndDestroySelf() are not run yet.
    // This fact is important to make window_size changes to stream and
    // connection contexts transactionally.
    if (_remote_window_left.load(butil::memory_order_relaxed) < size) {
        return false;
    }
    if (!MinusWindowSize(&_conn_ctx->_remote_window_left, size)) {
        return false;
    }
    int64_t after_sub = _remote_window_left.fetch_sub(size, butil::memory_order_relaxed) - size;
    if (after_sub < 0) {
        LOG(FATAL) << "Impossible, the http2 impl is buggy";
        _remote_window_left.fetch_add(size, butil::memory_order_relaxed);
        return false;
    }
    return true;
}

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
                    h.uri().set_scheme(pair.value);
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
                          butil::IOBuf& trailer_headers,
                          const butil::IOBuf& data,
                          int stream_id,
                          H2Context* conn_ctx) {
    const H2Settings& remote_settings = conn_ctx->remote_settings();
    char headbuf[FRAME_HEAD_SIZE];
    H2FrameHead headers_head = {
        (uint32_t)headers.size(), H2_FRAME_HEADERS, 0, stream_id};
    if (data.empty() && trailer_headers.empty()) {
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
                data_head.payload_size = it.bytes_left();
                if (trailer_headers.empty()) {
                    data_head.flags |= H2_FLAGS_END_STREAM;
                }
            } else {
                data_head.payload_size = remote_settings.max_frame_size;
            }
            SerializeFrameHead(headbuf, data_head);
            out->append(headbuf, FRAME_HEAD_SIZE);
            it.append_and_forward(out, data_head.payload_size);
        }
    }
    if (!trailer_headers.empty()) {
        H2FrameHead headers_head = {
            (uint32_t)trailer_headers.size(), H2_FRAME_HEADERS, 0, stream_id};
        headers_head.flags |= H2_FLAGS_END_STREAM;
        headers_head.flags |= H2_FLAGS_END_HEADERS;
        SerializeFrameHead(headbuf, headers_head);
        out->append(headbuf, sizeof(headbuf));
        out->append(butil::IOBuf::Movable(trailer_headers));
    }
    const int64_t conn_wu = conn_ctx->ReleaseDeferredWindowUpdate();
    if (conn_wu > 0) {
        char winbuf[FRAME_HEAD_SIZE + 4];
        SerializeFrameHead(winbuf, 4, H2_FRAME_WINDOW_UPDATE, 0, 0);
        SaveUint32(winbuf + FRAME_HEAD_SIZE, conn_wu);
        out->append(winbuf, sizeof(winbuf));
    }
}

H2UnsentRequest* H2UnsentRequest::New(Controller* c) {
    const HttpHeader& h = c->http_request();
    const CommonStrings* const common = get_common_strings();
    const bool need_content_type = !h.content_type().empty();
    const bool need_accept = !h.GetHeader(common->ACCEPT);
    const bool need_user_agent = !h.GetHeader(common->USER_AGENT);
    const std::string& user_info = h.uri().user_info();
    const bool need_authorization =
        (!user_info.empty() && !h.GetHeader("Authorization"));
    const size_t maxsize = h.HeaderCount() + 4
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
    const std::string* scheme = &h.uri().scheme();
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
    msg->_sctx.reset(new H2StreamContext(c->is_response_read_progressively()));
    return msg;
}

void H2UnsentRequest::Destroy() {
    for (size_t i = 0; i < _size; ++i) {
        _list[i].~Header();
    }
    this->~H2UnsentRequest();
    free(this);
}

struct RemoveRefOnQuit {
    RemoveRefOnQuit(H2UnsentRequest* msg) : _msg(msg) {}
    ~RemoveRefOnQuit() { _msg->RemoveRefManually(); }
private:
    DISALLOW_COPY_AND_ASSIGN(RemoveRefOnQuit);
    H2UnsentRequest* _msg;
};

void H2UnsentRequest::DestroyStreamUserData(SocketUniquePtr& sending_sock,
                                            Controller* cntl,
                                            int error_code,
                                            bool /*end_of_rpc*/) {
    RemoveRefOnQuit deref_self(this);
    if (sending_sock != NULL && error_code != 0) {
        CHECK_EQ(cntl, _cntl);
        std::unique_lock<butil::Mutex> mu(_mutex);
        _cntl = NULL;
        if (_stream_id != 0) {
            H2Context* ctx = static_cast<H2Context*>(sending_sock->parsing_context());
            ctx->AddAbandonedStream(_stream_id);
        }
    }
}

#if defined(BRPC_PROFILE_H2)
bvar::Adder<int64_t> g_append_request_time;
bvar::PerSecond<bvar::Adder<int64_t> > g_append_request_time_per_second(
    "h2_append_request_second",     &g_append_request_time);
#endif

butil::Status
H2UnsentRequest::AppendAndDestroySelf(butil::IOBuf* out, Socket* socket) {
#if defined(BRPC_PROFILE_H2)
    bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_append_request_time);
#endif
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
            return butil::Status(EINTERNAL, "Fail to init H2Context");
        }
        socket->initialize_parsing_context(&ctx);
        
        // Append client connection preface
        out->append(H2_CONNECTION_PREFACE_PREFIX,
                    H2_CONNECTION_PREFACE_PREFIX_SIZE);
        
        char settingsbuf[FRAME_HEAD_SIZE + H2_SETTINGS_MAX_BYTE_SIZE +
                         FRAME_HEAD_SIZE + 4/*for WU*/];
        const size_t nb = SerializeH2SettingsFrameAndWU(
            ctx->_unack_local_settings, settingsbuf);
        out->append(settingsbuf, nb);
    }

    // TODO(zhujiashun): also check this in server push
    if (ctx->VolatilePendingStreamSize() > ctx->remote_settings().max_concurrent_streams) {
        return butil::Status(ELIMIT, "Pending Stream count exceeds max concurrent stream");
    }

    // Although the critical section looks huge, it should rarely be contended
    // since timeout of RPC is much larger than the delay of sending.
    std::unique_lock<butil::Mutex> mu(_mutex);
    if (_cntl == NULL) {
        return butil::Status(ECANCELED, "The RPC was already failed");
    }

    const int id = ctx->AllocateClientStreamId();
    if (id < 0) {
        // The RPC should be failed and retried.
        // Note that the socket should not be SetFailed() which may affect
        // other RPC successfully sent requests and waiting for responses.
        RPC_VLOG << "Fail to allocate stream_id on " << *socket
                 << " h2req=" << (StreamUserData*)this;
        return butil::Status(EH2RUNOUTSTREAMS, "Fail to allocate stream_id");
    }

    _sctx->Init(ctx, id);
    // check flow control restriction
    if (!_cntl->request_attachment().empty()) {
        const int64_t data_size = _cntl->request_attachment().size();
        if (!_sctx->ConsumeWindowSize(data_size)) {
            return butil::Status(ELIMIT, "remote_window_left is not enough, data_size=%" PRId64, data_size);
        }
    }

    const int rc = ctx->TryToInsertStream(id, _sctx.get());
    if (rc < 0) {
        return butil::Status(EINTERNAL, "Fail to insert existing stream_id");
    } else if (rc > 0) {
        return butil::Status(ELOGOFF, "the connection just issued GOAWAY");
    }
    _stream_id = _sctx->stream_id();
    // After calling TryToInsertStream, the ownership of _sctx is transferred to ctx
    _sctx.release();

    HPacker& hpacker = ctx->hpacker();
    butil::IOBufAppender appender;
    HPackOptions options;
    options.encode_name = FLAGS_h2_hpack_encode_name;
    options.encode_value = FLAGS_h2_hpack_encode_value;
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
    butil::IOBuf dummy_buf;
    PackH2Message(out, frag, dummy_buf, _cntl->request_attachment(), _stream_id, ctx);
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

void H2UnsentRequest::Print(std::ostream& os) const {
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
    os << butil::ToPrintable(*body, FLAGS_http_verbose_max_body_length);

}

H2UnsentResponse::H2UnsentResponse(Controller* c, int stream_id, bool is_grpc)
    : _size(0)
    , _stream_id(stream_id)
    , _http_response(c->release_http_response())
    , _is_grpc(is_grpc) {
    _data.swap(c->response_attachment());
    if (is_grpc) {
        _grpc_status = ErrorCodeToGrpcStatus(c->ErrorCode());
        PercentEncode(c->ErrorText(), &_grpc_message);
    }
}

H2UnsentResponse* H2UnsentResponse::New(Controller* c, int stream_id, bool is_grpc) {
    const HttpHeader* const h = &c->http_response();
    const CommonStrings* const common = get_common_strings();
    const bool need_content_type = !h->content_type().empty();
    const size_t maxsize = 1
        + (size_t)need_content_type;
    const size_t memsize = offsetof(H2UnsentResponse, _list) +
        sizeof(HPacker::Header) * maxsize;
    H2UnsentResponse* msg = new (malloc(memsize)) H2UnsentResponse(c, stream_id, is_grpc);
    // :status
    if (h->status_code() == 200) {
        msg->push(common->H2_STATUS, common->STATUS_200);
    } else {
        butil::string_printf(&msg->push(common->H2_STATUS),
                            "%d", h->status_code());
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

#if defined(BRPC_PROFILE_H2)
bvar::Adder<int64_t> g_append_response_time;
bvar::PerSecond<bvar::Adder<int64_t> > g_append_response_time_per_second(
    "h2_append_response_second",     &g_append_response_time);
#endif

butil::Status
H2UnsentResponse::AppendAndDestroySelf(butil::IOBuf* out, Socket* socket) {
#if defined(BRPC_PROFILE_H2)
    bvar::ScopedTimer<bvar::Adder<int64_t> > tm(g_append_response_time);
#endif
    DestroyingPtr<H2UnsentResponse> destroy_self(this);
    if (socket == NULL) {
        return butil::Status::OK();
    }
    H2Context* ctx = static_cast<H2Context*>(socket->parsing_context());

    // flow control
    // NOTE: Currently the stream context is definitely removed and updating
    // window size is useless, however it's not true when progressive request
    // is supported.
    // TODO(zhujiashun): Instead of just returning error to client, a better
    // solution to handle not enough window size is to wait until WINDOW_UPDATE
    // is received, and then retry those failed response again.
    if (!MinusWindowSize(&ctx->_remote_window_left, _data.size())) {
        char rstbuf[FRAME_HEAD_SIZE + 4];
        SerializeFrameHead(rstbuf, 4, H2_FRAME_RST_STREAM, 0, _stream_id);
        SaveUint32(rstbuf + FRAME_HEAD_SIZE, H2_FLOW_CONTROL_ERROR);
        out->append(rstbuf, sizeof(rstbuf));
        return butil::Status::OK();
    }

    HPacker& hpacker = ctx->hpacker();
    butil::IOBufAppender appender;
    HPackOptions options;
    options.encode_name = FLAGS_h2_hpack_encode_name;
    options.encode_value = FLAGS_h2_hpack_encode_value;

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

    butil::IOBuf trailer_frag;
    if (_is_grpc) {
        HPacker::Header status_header("grpc-status",
                                      butil::string_printf("%d", _grpc_status));
        hpacker.Encode(&appender, status_header, options);
        if (!_grpc_message.empty()) {
            HPacker::Header msg_header("grpc-message", _grpc_message);
            hpacker.Encode(&appender, msg_header, options);
        }
        appender.move_to(trailer_frag);
    }

    PackH2Message(out, frag, trailer_frag, _data, _stream_id, ctx);
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

void H2UnsentResponse::Print(std::ostream& os) const {
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
    os << butil::ToPrintable(_data, FLAGS_http_verbose_max_body_length);
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

    H2UnsentRequest* h2_req = dynamic_cast<H2UnsentRequest*>(accessor.get_stream_user_data());
    CHECK(h2_req);
    h2_req->AddRefManually();   // add ref for AppendAndDestroySelf
    h2_req->_sctx->set_correlation_id(correlation_id);
    *user_message = h2_req;
    
    if (FLAGS_http_verbose) {
        LOG(INFO) << '\n' << *h2_req;
    }
}

static bool IsH2SocketValid(Socket* s) {
    H2Context* c = static_cast<H2Context*>(s->parsing_context());
    return (c == NULL || !c->RunOutStreams());
}

StreamUserData* H2GlobalStreamCreator::OnCreatingStream(
        SocketUniquePtr* inout, Controller* cntl) {
    if ((*inout)->GetAgentSocket(inout, IsH2SocketValid) != 0) {
        cntl->SetFailed(EINTERNAL, "Fail to create agent socket");
        return NULL;
    }

    H2UnsentRequest* h2_req = H2UnsentRequest::New(cntl);
    if (!h2_req) {
        cntl->SetFailed(ENOMEM, "Fail to create H2UnsentRequest");
        return NULL;
    }
    return h2_req;
}

void H2GlobalStreamCreator::DestroyStreamCreator(Controller* cntl) {
    // H2GlobalStreamCreator is a global singleton value.
    // Don't delete it in this function.
}

StreamCreator* get_h2_global_stream_creator() {
    return butil::get_leaky_singleton<H2GlobalStreamCreator>();
}

}  // namespace policy

} // namespace brpc
