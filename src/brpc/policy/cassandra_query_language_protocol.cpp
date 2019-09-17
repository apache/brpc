// Copyright (c) 2019 Iqiyi, Inc.
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

// Authors: Daojin, Cai (caidaojin@qiyi.com)

#include "brpc/policy/cassandra_query_language_protocol.h"

#include <mutex>                                // std::once_flag

#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message

#include "brpc/cassandra.h"
#include "brpc/controller.h"
#include "brpc/describable.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/input_messenger.h"
#include "brpc/log.h"
#include "brpc/policy/cassandra_authenticator.h"
#include "brpc/reloadable_flags.h"              // BRPC_VALIDATE_GFLAG
#include "brpc/socket.h"
#include "brpc/span.h"
#include "bthread/bthread.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "butil/string_printf.h"                // string_appendf()
#include "butil/time.h"

namespace brpc {

DECLARE_bool(enable_rpcz);

namespace policy {

static bool CheckMaxCqlStreams(const char* flagname, int value) {
    if (value >= 64 && value <= 32640) {
        return true;
    }
    LOG(ERROR) << flagname << " should be range of [64, 32640].";
    return false;
}

DEFINE_bool(enable_cql_prepare, false,
            "Enable cql prepare and excute for query atomatically.");
DEFINE_bool(cql_verbose, false,
            "[DEBUG] Print EVERY cql request/response");
BRPC_VALIDATE_GFLAG(cql_verbose, PassValidate);
DEFINE_int32(max_cql_streams_per_connection, 32640,
             "CQL can only be 32768 different simultaneous streams, it is up to "
             "the client to reuse stream id. We reserve 32640 ~ 32678 for internal "
             "use. Users' requests can use id from 0 to 32639.");
BRPC_VALIDATE_GFLAG(max_cql_streams_per_connection, CheckMaxCqlStreams);
DEFINE_bool(enable_cql_stream_allocation_monitor, true,
            "Enable cql stream allocation performance monitor.");
DEFINE_int32(pending_cql_streams_overload_threshold, 10000,
             "If pending streams on a cql connection meet this threshold,"
             "we should not allocate stream on this connention ever.");
BRPC_VALIDATE_GFLAG(pending_cql_streams_overload_threshold, PassValidate);
DEFINE_int32(cql_streams_allocation_cost_overload_threshold, 100,
             "If a cql stream allocation cost on a connection meet this threshold,"
             "we should not allocate stream on this connention ever.");
BRPC_VALIDATE_GFLAG(cql_streams_allocation_cost_overload_threshold, PassValidate);

namespace {

#if defined(_MSC_VER) && defined(_M_AMD64)
    using CqlStreamBucket = uint64_t;
#else
    using CqlStreamBucket = unsigned long;
#endif

constexpr int16_t kCqlDummyStreamId = -1;
constexpr int16_t kMaxCqlStreamsPerConnection = 32640;
constexpr int16_t kCqlStartupStreamId = kMaxCqlStreamsPerConnection;
constexpr int16_t kCqlAuthResponseStreamId = kCqlStartupStreamId + 1;
constexpr int16_t kCqlSetKeyspceStreamId = kCqlAuthResponseStreamId + 1;
constexpr int16_t kCqlOptionsStreamId = kCqlSetKeyspceStreamId + 1;

constexpr size_t kBitsOfCqlStreamBucket = sizeof(CqlStreamBucket) * 8;

// Round robin cql stream buckets by this step.
// 7 * 11 * 17 > 32640 / 32
const int g_stream_bucket_rr_strides[3] = {7, 11, 17};
 
inline size_t GetNumOfCqlStreamBucket() {
    return FLAGS_max_cql_streams_per_connection / kBitsOfCqlStreamBucket; 
}

inline int GetCqlStreamBucketRrStride(size_t bucket_size) {
    for (size_t i = 0;
         i != sizeof(g_stream_bucket_rr_strides) / sizeof(g_stream_bucket_rr_strides[0]); 
         ++i) {
        if (bucket_size % g_stream_bucket_rr_strides[i] != 0) {
            return g_stream_bucket_rr_strides[i];
        }
    }
    return -1;
}

constexpr uint8_t kDefaultCqlProtocolVersion = 0x03;

} // namespace

struct CqlProtocolVersion {
    CqlProtocolVersion() {
        Set(kDefaultCqlProtocolVersion);
    }

    CqlProtocolVersion(uint8_t version) {
        Set(version);
    }

    uint8_t version() const {
        return _version;
    }

    const std::string& version_str() const {
        return _version_str;
    }

    void Set(uint8_t version) {
        _version = version;
        _version_str.clear();
        butil::string_appendf(&_version_str, "%d.0.0", _version);
    }

private:
    uint8_t _version = kDefaultCqlProtocolVersion;
    std::string _version_str;
};

// Status for cql connection.
enum CqlStatus {
    // Cql connection is set up.
    CQL_CONNECTED              = 0x00,
    // Cql startup message is sent. 
    CQL_STARTUP                = 0x01,
    // Cql authenticate is received and auth response is sent.
    CQL_AUTHENTICATE           = 0x02,
    // Cql auth success is received.
    CQL_AUTH_SUCCESS           = 0x03,
    // Cql set keyspace is sent.
    CQL_SET_KEYSPACE           = 0x04,
    // Cql connection is ready. But there are pending 
    // requests accumulated during connection initializtion to send. 
    CQL_READY_WITH_PENDING_REQ = 0x05,
    // Cql connection is ready.
    CQL_READY                  = 0x06
};

const char* cql_status_str(CqlStatus st) {
    switch(st) {
    case CQL_CONNECTED: 
        return "CQL_CONNECTED";
    case CQL_STARTUP: 
        return "CQL_STARTUP";
    case CQL_AUTHENTICATE: 
        return "CQL_AUTHENTICATE";
    case CQL_AUTH_SUCCESS:
        return "CQL_AUTH_SUCCESS";
    case CQL_SET_KEYSPACE:
        return "CQL_SET_KEYSPACE";
    case CQL_READY_WITH_PENDING_REQ:
        return "CQL_READY_WITH_PENDING_REQ";
    case CQL_READY:
        return "CQL_READY";  
    default:
        break;
    }
    return "Unknow";
}

class GlobalCqlStreamMonitor;

namespace {

GlobalCqlStreamMonitor* g_global_monitor = nullptr;
std::once_flag g_global_monitor_once_flag;

int cast_int(void* arg) { return *reinterpret_cast<int*>(arg); }

} // namespace

class GlobalCqlStreamMonitor {
public:
    ~GlobalCqlStreamMonitor() {
        if (FLAGS_enable_cql_stream_allocation_monitor) {
            _parsing_ctx_objects_bvar.hide();
            _inflight_streams_window.hide();
            _max_inflight_streams_window.hide();
            _max_stream_cost_window.hide();
        } 
    }

    static void RecordStreamAllocation(int cost, int inflight_streams) {
        if (FLAGS_enable_cql_stream_allocation_monitor) {
            GlobalCqlStreamMonitor* monitor = Instance();
            monitor->_max_stream_cost << cost;
            monitor->_inflight_streams << inflight_streams;
            monitor->_max_inflight_streams << inflight_streams;
        }
    }

    static void AddParsingContextCount() {
        if (FLAGS_enable_cql_stream_allocation_monitor) {
            Instance()->_parsing_ctx_objects.fetch_add(
                1, butil::memory_order_relaxed);
        }
    }

    static void SubParsingContextCount() {
        if (FLAGS_enable_cql_stream_allocation_monitor) {
            Instance()->_parsing_ctx_objects.fetch_sub(
                1, butil::memory_order_relaxed);
        }
    }

    static GlobalCqlStreamMonitor* Instance() {
        std::call_once(
            g_global_monitor_once_flag,
            [] () { g_global_monitor = new (std::nothrow) GlobalCqlStreamMonitor; });
        return g_global_monitor;
    }

private:
    GlobalCqlStreamMonitor()
        : _parsing_ctx_objects(0) 
        , _parsing_ctx_objects_bvar(cast_int, &_parsing_ctx_objects)
        , _inflight_streams_window(&_inflight_streams, -1)
        , _max_inflight_streams_window(&_max_inflight_streams, -1)
        , _max_stream_cost_window(&_max_stream_cost, -1) {
        if (FLAGS_enable_cql_stream_allocation_monitor) {
            _parsing_ctx_objects_bvar.expose("cql_parsing_context_count");
            _inflight_streams_window.expose("cql_inflight_average_streams_per_connection");
            _max_inflight_streams_window.expose("cql_inflight_max_streams_on_one_connection");
            _max_stream_cost_window.expose("cql_stream_allocation_max_cost");
        } 
    }
    using MaxerWindow = bvar::Window<bvar::Maxer<unsigned>, bvar::SERIES_IN_SECOND>;
    using RecorderWindow = bvar::Window<bvar::IntRecorder, bvar::SERIES_IN_SECOND>;

    butil::atomic<int> _parsing_ctx_objects;
    bvar::PassiveStatus<int> _parsing_ctx_objects_bvar;
    bvar::IntRecorder _inflight_streams;
    RecorderWindow _inflight_streams_window;
    bvar::Maxer<unsigned> _max_inflight_streams;
    MaxerWindow _max_inflight_streams_window;
    bvar::Maxer<unsigned> _max_stream_cost;
    MaxerWindow _max_stream_cost_window;
};

class CqlParsingContext;
class CqlStreamCreator;

struct CqlStream {
    CqlStream() = default;
    CqlStream(uint64_t cid) : correlation_id(cid) {}

    bool IsValid() const {
        return correlation_id != INVALID_BTHREAD_ID.value;
    }

    void Reset() {
        correlation_id = INVALID_BTHREAD_ID.value;
    }

    uint64_t correlation_id = INVALID_BTHREAD_ID.value;
};

class CqlInputResponse : public InputMessageBase {
friend class CqlParsingContext;
friend void ProcessCqlResponse(InputMessageBase* msg_base);
public:
    void set_correlation_id(const uint64_t id) {
        _correlation_id = id;
    }

    uint64_t correlation_id() const {
        return _correlation_id;
    }

    int16_t GetStreamId() const {
        return _response.head.stream_id;
    }

    CqlResponse& response() {
        return _response;
    }

    static CqlInputResponse* New() {
        return new (std::nothrow) CqlInputResponse();
    }

protected:
    // MUST create an object by New().
    CqlInputResponse() = default;

    // @InputMessageBase
    void DestroyImpl() {
        delete this;
    }

private:
    uint64_t _correlation_id = INVALID_BTHREAD_ID.value;
    CqlResponse _response;
};

class CqlParsingContext : public Destroyable, public Describable {
public:
    virtual ~CqlParsingContext() = default;

    int16_t AddStream(CqlStream stream);
    bool RemoveStream(int16_t stream_id);
    CqlStream FindStream(int16_t stream_id) const;

    ParseError Consume(butil::IOBuf* buf);
    ParseError ConsumeHead(butil::IOBuf* buf);
    ParseError ConsumeBody(butil::IOBuf* buf);

    ParseResult HandleAndDestroyInternalMessages(CqlInputResponse* stream);
    ParseResult OnSupported(const CqlResponse& response);
    ParseResult OnInternalError(const CqlResponse& response);
    ParseResult OnReady(const CqlResponse& response);
    ParseResult OnAuthenticator(const CqlResponse& response);
    ParseResult OnAuthChallenge(const CqlResponse& response);
    ParseResult OnAuthSuccess(const CqlResponse& response);
    ParseResult OnInternalUseKeyspace(const CqlResponse& response);

    void PackCqlOptions(butil::IOBuf* buf) const;		
		void PackCqlStartup(butil::IOBuf* buf) const;
    void PackPlainTextAuthenticator(const std::string& user_name,
                                    const std::string& password,
                                    butil::IOBuf* buf);
    bool ReplyAuthResponse();
    int UseKeyspaceIfNeeded();

    bool overload() const {
        return _overload;
    }

    void DecideOverloadStatus(int cost, int16_t stream_id) {
        if (stream_id < 0
            || cost > FLAGS_cql_streams_allocation_cost_overload_threshold
            || _num_inflight_streams.load(butil::memory_order_relaxed)
                > FLAGS_pending_cql_streams_overload_threshold) {
            _overload = true;
        }
    }

    uint8_t cql_protocol_version() const {
        return _cql_protocol_version.version();
    }
    
    bool IsExpectedVersion(uint8_t version) const {
        return version == cql_protocol_version();
    }

    void set_cql_protocol_version(uint8_t version) {
        _cql_protocol_version.Set(version);
    }

    void set_auth(const CassandraAuthenticator* auth) {
        _auth = auth;
    }

    const CassandraAuthenticator* auth() const {
        return _auth;
    }

    CqlStatus status() const {
        return _status;
    }
 
    void set_status(CqlStatus status) {
        _status = status;
    }

    bool set_startup_status_once() {
        CqlStatus status_connected = CQL_CONNECTED;
        return _status.compare_exchange_strong(
            status_connected, CQL_STARTUP, butil::memory_order_relaxed);
    }

    bool IsOptions(int16_t stream_id) const {
        return stream_id == kCqlOptionsStreamId;
    }

    bool AppendToPendingBuf(const CqlFrameHead& header,
                            const butil::IOBuf& body);

    bool IsReady() const {
        return status() >= CQL_READY_WITH_PENDING_REQ;
    }

    const char* parsing_error() const {
        return _parsing_error;
    }

    // @Destroyable
    void Destroy() override {
        GlobalCqlStreamMonitor::SubParsingContextCount();
        delete this;
    }

    // @Describable
    virtual void Describe(std::ostream& os, const DescribeOptions& options) const;

    static CqlParsingContext* New(Socket* sock) {
        GlobalCqlStreamMonitor::AddParsingContextCount();
        return new (std::nothrow) CqlParsingContext(sock);
    }

    static bool IsInternalStream(int16_t stream_id) {
        return stream_id >= kCqlStartupStreamId;
    }

private:
friend ParseResult ParseCqlMessage(butil::IOBuf* source, Socket* socket,
                                   bool read_eof, const void* arg);

    CqlParsingContext(Socket* sock);

    int16_t AllocateStreamId(int* cost);
    int FindAndSetFirstAvailableStream(size_t index, int* cost);
    int FindFirstBitOneFromRight(CqlStreamBucket bucket) const;
    bool IsStreamExists(int16_t stream_id) const;

    int WriteInternalMessageToSocket(butil::IOBuf* buf) {
        // TODO(caidaojin): we use default write options here. 
        // Maybe we need a bthread_id_t to handle write failure case.
        return _socket->Write(buf);
    }

    bool FlushPendingMessages();

    volatile bool _overload = false;

    CqlProtocolVersion _cql_protocol_version;
    std::atomic<CqlStatus> _status;
    const CassandraAuthenticator* _auth = nullptr;

    const char* _parsing_error = nullptr;
    Socket* _socket = nullptr;
    DestroyingPtr<CqlInputResponse> _parsing_response;

    std::string _authenticator_expected;

    butil::Mutex _pending_mu;
    butil::IOBuf _pending_buf;

    int _last_stream_allocation_cost = 0;
    butil::atomic<int> _num_inflight_streams;		
    butil::atomic<uint64_t> _curr_stream_bucket_index;
    std::vector<butil::atomic<CqlStreamBucket>> _stream_buckets;
    const int _stream_bucket_rr_stride;
    std::vector<CqlStream> _inflight_streams;

    DISALLOW_COPY_AND_ASSIGN(CqlParsingContext);
};

CqlParsingContext* GetParsingContextOf(Socket* sock) {
    CHECK(sock != nullptr);
    CqlParsingContext* ctx = static_cast<CqlParsingContext*>(sock->parsing_context());
    if (ctx == nullptr) {
        ctx = CqlParsingContext::New(sock);
        sock->initialize_parsing_context(&ctx);
    }

    return ctx;
}

CqlParsingContext::CqlParsingContext(Socket* sock)
    : _status(CQL_CONNECTED)
    , _socket(sock)
    , _num_inflight_streams(0)
    , _curr_stream_bucket_index(0)
    , _stream_buckets(GetNumOfCqlStreamBucket())
    , _stream_bucket_rr_stride(GetCqlStreamBucketRrStride(_stream_buckets.size()))
    , _inflight_streams(_stream_buckets.size() * kBitsOfCqlStreamBucket) {
    CHECK(_stream_bucket_rr_stride != -1) << "Fail to find cql bucket rr stride."; 
    for (size_t i = 0; i != _stream_buckets.size(); ++i) {
        _stream_buckets[i] = (CqlStreamBucket)-1;
    }
}

inline int CqlParsingContext::FindFirstBitOneFromRight(
    CqlStreamBucket bucket) const {
#if defined(__GNUC__)
    return __builtin_ffsl(bucket);
#elif defined(_MSC_VER)
    unsigned long ret;
#  if defined(_M_AMD64)
    _BitScanForward64(&ret, bucket);
#  else
    _BitScanForward(&ret, bucket);
#  endif
    return static_cast<int>(ret);
#else
#endif
}

inline bool CqlParsingContext::IsStreamExists(int16_t stream_id) const {
    if (stream_id < 0) {
        return false;
    }
    const size_t bucket = stream_id / kBitsOfCqlStreamBucket;
    const size_t offset = stream_id % kBitsOfCqlStreamBucket;
    if (bucket < _stream_buckets.size() && 
        (~_stream_buckets[bucket].load(butil::memory_order_relaxed)
            & (CqlStreamBucket(1) << offset))) {
        return true;
    }
    return false;
}

int16_t CqlParsingContext::AddStream(CqlStream stream) {
    int cost = 0;
    const int16_t stream_id = AllocateStreamId(&cost);
    DecideOverloadStatus(cost, stream_id);
    _last_stream_allocation_cost = cost;
    if (stream_id >= 0) {
        CHECK(!_inflight_streams.at(stream_id).IsValid())
            << "Allocate a cql stream id=" << stream_id << " whose stream is not empty.";
        _inflight_streams[stream_id] = stream;
        _num_inflight_streams.fetch_add(1, butil::memory_order_relaxed);
    }
    GlobalCqlStreamMonitor::RecordStreamAllocation(
        cost, _num_inflight_streams.load(butil::memory_order_relaxed));
    return stream_id;
}

int CqlParsingContext::FindAndSetFirstAvailableStream(size_t index, int* cost) {
    CqlStreamBucket bucket = 0;
    CqlStreamBucket new_bucket = 0;
    int offset = 0;
    do {
        ++(*cost);
        bucket = _stream_buckets[index].load(butil::memory_order_acquire);
        offset = FindFirstBitOneFromRight(bucket);
        if (offset <= 0) {
            return -1;
        }
        new_bucket = bucket & ~(CqlStreamBucket(1) << (offset - 1));
    } while (!_stream_buckets[index].compare_exchange_strong(
        bucket, new_bucket, butil::memory_order_acquire));
    return offset - 1;
}

int16_t CqlParsingContext::AllocateStreamId(int* cost) {
    const size_t begin = _curr_stream_bucket_index.fetch_add(
        _stream_bucket_rr_stride, butil::memory_order_relaxed)
        % _stream_buckets.size();
    for(size_t i = begin; ;) {
        int offset = FindAndSetFirstAvailableStream(i, cost);
        if (offset >= 0) {
            return offset + kBitsOfCqlStreamBucket * i;
        }
        i = (i + 1) % _stream_buckets.size();
        if (i == begin) {
            return -1;
        }
    }
    return -1;
}

bool CqlParsingContext::RemoveStream(int16_t stream_id) {
    const size_t bucket = stream_id / kBitsOfCqlStreamBucket;
    if (stream_id < 0 || bucket >= _stream_buckets.size()) {
        return false;
    }
    const CqlStreamBucket bit_mask = CqlStreamBucket(1) << (stream_id % kBitsOfCqlStreamBucket);
    butil::atomic<CqlStreamBucket>& stream_bucket = _stream_buckets[bucket];
    if (~stream_bucket & bit_mask) {
        _inflight_streams[stream_id].Reset();
        CqlStreamBucket c = stream_bucket.load(butil::memory_order_acquire); 
        CqlStreamBucket n = c | bit_mask;
        while (!stream_bucket.compare_exchange_strong(
            c, n, butil::memory_order_release)) {
            c = stream_bucket.load(butil::memory_order_relaxed);
            n = c | bit_mask;
            CHECK(~c & bit_mask) << "Bugs due to remove an un-allocated cql stream id.";
        }
        _num_inflight_streams.fetch_sub(1, butil::memory_order_relaxed);
        return true;
    }
    return false;
}

CqlStream CqlParsingContext::FindStream(int16_t stream_id) const {
    return IsStreamExists(stream_id)
           ? _inflight_streams[stream_id] : CqlStream();
}

bool CqlParsingContext::AppendToPendingBuf(
    const CqlFrameHead& header, const butil::IOBuf& body) {
    BAIDU_SCOPED_LOCK(_pending_mu);
    if (!IsReady()) {
        CqlEncodeHead(header, &_pending_buf);
        _pending_buf.append(body);
        return true; 
    }
    return false;
}

ParseError CqlParsingContext::Consume(butil::IOBuf* buf) {
    if (!_parsing_response) {
        if (buf->size() >= CqlFrameHead::SerializedSize()) {
            ParseError ret = ConsumeHead(buf);
            if (ret != PARSE_OK) {
                return ret;
            }
        } else {
            return PARSE_ERROR_NOT_ENOUGH_DATA;
        }
    }
    return ConsumeBody(buf);
}

ParseError CqlParsingContext::ConsumeHead(butil::IOBuf* buf) {
    CqlFrameHead head;
    butil::IOBufBytesIterator iter(*buf);
    CHECK(CqlDecodeHead(iter, &head));
    buf->pop_front(head.SerializedSize());
    do {
        if (head.version != (cql_protocol_version() | 0x80)) {
            _parsing_error = "Unsupported protocol version of cql response.";
            break;
        }
        if (!IsResponseOpcode(head.opcode)) {
           _parsing_error = "Invalid opcode of cql response.";
           break;
        }
        _parsing_response.reset(CqlInputResponse::New());
        if (!_parsing_response) {
            _parsing_error = "Fail to create cql input response.";
            break;
        }
        _parsing_response->_response.head = head;
        if (!IsInternalStream(head.stream_id)) {
            CqlStream stream = FindStream(head.stream_id);
            if (stream.IsValid()) {
                _parsing_response->set_correlation_id(stream.correlation_id);
                return PARSE_OK;
            }
            _parsing_error = "Null stream found for cql response.";
        } else {
            return PARSE_OK;
        }
    } while (false);
    LOG(ERROR) << "Fail to parse cql head: " << head;
    return PARSE_ERROR_ABSOLUTELY_WRONG;
}

ParseError CqlParsingContext::ConsumeBody(butil::IOBuf* buf) {
    CHECK(_parsing_response);
    const size_t body_len = _parsing_response->_response.head.length;
    if (body_len <= buf->size()) {
        CHECK_EQ(_parsing_response->_response.body.size(), 0);
        buf->cutn(&_parsing_response->_response.body, body_len);
        return PARSE_OK;
    }
    return PARSE_ERROR_NOT_ENOUGH_DATA;
}

void CqlParsingContext::PackPlainTextAuthenticator(
    const std::string& user_name, const std::string& password,
    butil::IOBuf* buf) {
    CqlFrameHead head;
    head.version = cql_protocol_version(),
    head.opcode = CQL_OPCODE_AUTH_RESPONSE;
    head.stream_id = kCqlAuthResponseStreamId;
    head.length = user_name.size() + password.size() + 2;
    CqlEncodeHead(head, buf);
    CqlEncodePlainTextAuthenticate(user_name, password, buf);
}

bool CqlParsingContext::ReplyAuthResponse() {
    // TODO(caidaojin): Support other auth policy.
    // Now we only support authenticate with user name and password.
    const CassandraAuthenticator* cass_auth = auth();
    if (cass_auth == nullptr || cass_auth->user_name().empty() 
        || cass_auth->password().empty()) {
        return false;
    }
  
    butil::IOBuf buf;
    PackPlainTextAuthenticator(cass_auth->user_name(), cass_auth->password(), &buf);
    return 0 == WriteInternalMessageToSocket(&buf);   
}

void CqlParsingContext::PackCqlOptions(butil::IOBuf* buf) const {
    CqlFrameHead head(cql_protocol_version(),
                      CQL_OPCODE_OPTIONS,
                      kCqlOptionsStreamId,
                      0);
    CqlEncodeHead(head, buf);
}

void CqlParsingContext::PackCqlStartup(butil::IOBuf* buf) const {
    CqlFrameHead head(cql_protocol_version(),
                      CQL_OPCODE_STARTUP,
                      kCqlStartupStreamId,
                      0);
    // Now only support cql version 3.0.0. 
    // Do not support COMPRESSION.
    std::map<std::string, std::string> body =
        {{"CQL_VERSION", _cql_protocol_version.version_str()}};
    CqlEncodeStartup(head, body, buf);
}

int CqlParsingContext::UseKeyspaceIfNeeded() {
    const CassandraAuthenticator* cass_auth = auth();
    if (cass_auth == nullptr || cass_auth->key_space().empty()) {
        return 1;
    }
    std::string set_keyspace = "USE " + cass_auth->key_space();
    CqlFrameHead head;
    head.version = cql_protocol_version();
    head.opcode = CQL_OPCODE_QUERY;
    head.stream_id = kCqlSetKeyspceStreamId;
    head.length = set_keyspace.size() + 4/*string size*/ + 3/*consistency + flags*/;

    butil::IOBuf buf;
    CqlEncodeHead(head, &buf);
    CqlEncodeUseKeyspace(set_keyspace, &buf);
    if (0 == WriteInternalMessageToSocket(&buf)) {
        return 0;
    }

    return -1;
}

bool CqlParsingContext::FlushPendingMessages() {
    BAIDU_SCOPED_LOCK(_pending_mu);
    if (!_pending_buf.empty()) {
        if (0 == WriteInternalMessageToSocket(&_pending_buf)) {
             _pending_buf.clear();
             return true;
        }
        return false;
    }
    return true;
}

ParseResult CqlParsingContext::OnSupported(const CqlResponse& response) {
    // Get options if needed.
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}

ParseResult CqlParsingContext::OnReady(const CqlResponse& response) {
    CqlReadyDecoder ready(response.body);
    if (ready.DecodeResponse()) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    return MakeParseError(
        PARSE_ERROR_ABSOLUTELY_WRONG,
        "Fail to decode ready reponse.");
}

ParseResult CqlParsingContext::OnAuthenticator(const CqlResponse& response) {
    CqlAuthenticateDecoder authecticator(response.body);
    if (authecticator.DecodeResponse()) {
        _authenticator_expected = authecticator.authenticator();
        if (status() == CQL_STARTUP) {
            if (!ReplyAuthResponse()) {
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                                      "Fail to send auth response.");
            }						
            set_status(CQL_AUTHENTICATE);
        } else {
            LOG(ERROR) << "Received cql authenticate at unexpected status=" << status();
        }
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    return MakeParseError(
        PARSE_ERROR_ABSOLUTELY_WRONG,
        "Fail to decode authenticate reponse.");
}

ParseResult CqlParsingContext::OnAuthChallenge(const CqlResponse& response) {
    // TODO(caidaojin): Now not support auth challenge.
    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                          "Now do not support auth challenge.");
}

ParseResult CqlParsingContext::OnAuthSuccess(const CqlResponse& response) {
    CqlAuthSuccessDecoder auth_success(response.body);
    if (auth_success.DecodeResponse()) {
        std::string token;
        if (!auth_success.DecodeAuthToken(&token) || !token.empty()) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                                  "Invalid auth response or not a empty body.");            
        }
        set_status(CQL_AUTH_SUCCESS);
        int ret = UseKeyspaceIfNeeded();
        if (ret < 0) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                                  "Fail to set keyspace for cql.");
        }
        if (ret == 0) {
            set_status(CQL_SET_KEYSPACE);
        } else {
            LOG(ERROR) << "No need set keyspace for this cql connection. Are you sure?";
            set_status(CQL_READY_WITH_PENDING_REQ);
        }			
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    return MakeParseError(
        PARSE_ERROR_ABSOLUTELY_WRONG,
        "Fail to decode auth success reponse.");    
}

ParseResult CqlParsingContext::OnInternalUseKeyspace(const CqlResponse& response) {
    CqlSetKeyspaceResultDecoder decoder(response.body);
    if (!decoder.DecodeResponse()) {
        return MakeParseError(
            PARSE_ERROR_ABSOLUTELY_WRONG,
            "Fail to decode internal use keyspace reponse.");
    }		
    if (auth() != nullptr && auth()->key_space() == decoder.keyspace()) {
        set_status(CQL_READY_WITH_PENDING_REQ);
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    return MakeParseError(
                PARSE_ERROR_ABSOLUTELY_WRONG,
                "Invalid keyspace reponse.");
}

ParseResult CqlParsingContext::OnInternalError(const CqlResponse& response) {
    if (!IsOptions(response.head.stream_id)) {
        return MakeParseError(
            PARSE_ERROR_ABSOLUTELY_WRONG,
            "Received error response during cql connection initialization.");
    }
    CqlErrorDecoder decoder(response.body);
    if (!decoder.DecodeResponse()) {
        return MakeParseError(
             PARSE_ERROR_ABSOLUTELY_WRONG,
             "Fail to decode internal error reponse.");
    }
    RPC_VLOG << "Received a cql internal error response at status: " << status() 
        << ". Brief error: " << decoder.GetErrorBrief()
        << ". Details error: " << decoder.GetErrorDetails();
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}

ParseResult CqlParsingContext::HandleAndDestroyInternalMessages(CqlInputResponse* input_response) {
    DestroyingPtr<CqlInputResponse> msg_destroyer(input_response);
    CqlResponse& response = input_response->response();
    if (FLAGS_cql_verbose) {
        LOG(ERROR) << "[CQL INTERNAL RESPONSE]: " 
            << CqlMessageOsWrapper(response, true);
    }
    const CqlFrameHead& head = response.head;
    ParseResult result = MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    switch (head.opcode) {
    case CQL_OPCODE_ERROR:
        result = OnInternalError(response);
        break;
    case CQL_OPCODE_AUTHENTICATE:
        result = OnAuthenticator(response);
        break;				
    case CQL_OPCODE_AUTH_CHALLENGE:
        result = OnAuthChallenge(response);
        break;
    case CQL_OPCODE_READY:
        result = OnReady(response);
        break;
    case CQL_OPCODE_AUTH_SUCCESS: 
        result = OnAuthSuccess(response);
        break;
    case CQL_OPCODE_SUPPORTED:
        result = OnSupported(response);
        break;
    default:
        break;
    }
    if (input_response->GetStreamId() == kCqlSetKeyspceStreamId) {
        result = OnInternalUseKeyspace(response);
    }
    if (status() == CQL_READY_WITH_PENDING_REQ) {
        if (FlushPendingMessages()) {
            set_status(CQL_READY);
        } else {
            return MakeParseError(
                PARSE_ERROR_ABSOLUTELY_WRONG,
                "Fail to send cql pending requests.");
        }
    }
    return result;
}

void CqlParsingContext::Describe(std::ostream& os,
                                 const DescribeOptions& options) const {
    os << "connection_state=" << cql_status_str(status()) 
       << " num_inflight_streams=" << _num_inflight_streams.load(butil::memory_order_relaxed)
       << " last_stream_allocation_cost=" << _last_stream_allocation_cost;
    // TODO(caidaojin): print stream bucket bit map if verbose is enabled.
}

namespace {

bool IsCqlSocketLightLoad(Socket* socket) {
    CqlParsingContext* parsing_ctx	= GetParsingContextOf(socket);
    return !parsing_ctx->overload();
}

}  // namespace

class CqlStreamCreator : public StreamCreator {
public:
    CassOpCode opcode() const {
        return _opcode;
    }

    static CqlStreamCreator* New(const CassandraRequest& cr) {
        CqlStreamCreator* sc = new (std::nothrow) CqlStreamCreator();
        if (sc != nullptr) {
            sc->_opcode = cr.opcode();
        }
        return sc;
    }

    bool CheckIfSupportPrepare() {
        return false;   
    }

protected:
    // MUST create an object by New(const CassandraRequest& cr).
    CqlStreamCreator() {}
    // @StreamCreator
    StreamUserData* OnCreatingStream(SocketUniquePtr* inout, Controller* cntl);

    // @StreamCreator
    void DestroyStreamCreator(Controller* cntl) {
        delete this;
    }

private:
    CassOpCode _opcode;
};

StreamUserData* CqlStreamCreator::OnCreatingStream(SocketUniquePtr* inout, Controller* cntl) {
    if (cntl->connection_type() == CONNECTION_TYPE_SINGLE) {
        if ((*inout)->GetAgentSocket(inout, IsCqlSocketLightLoad) != 0) {
            cntl->SetFailed(EINTERNAL, "Fail to create cql agent socket");
        }
    }
    return nullptr;
}

class CqlDummyMessage : public SocketMessage {
public:
    CqlDummyMessage(size_t size) : _bytes(size) {}
    virtual butil::Status AppendAndDestroySelf(butil::IOBuf* out, Socket* sock) {
        delete this;
        return butil::Status::OK();
    }

    virtual size_t EstimatedByteSize() { return _bytes; }
private:
    const size_t _bytes;    
};

// "Message" = "Response" as we only implement the client for CQL.
ParseResult ParseCqlMessage(butil::IOBuf* source, Socket* socket,
                            bool read_eof, const void* /*arg*/) {
    if (read_eof || source->empty()) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    CqlParsingContext* parsing_ctx = GetParsingContextOf(socket);
    do {
        ParseError err = parsing_ctx->Consume(source);
        if (err != PARSE_OK) {
            return MakeParseError(err, parsing_ctx->parsing_error());
        }
        CHECK(parsing_ctx->_parsing_response);
        const int16_t id = parsing_ctx->_parsing_response->GetStreamId();
        if (!parsing_ctx->IsInternalStream(id)) {
            if (!parsing_ctx->RemoveStream(id)) {
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                                      "Fail to remove cql stream.");     
            }
            return MakeMessage(parsing_ctx->_parsing_response.release());
        } else {
            return parsing_ctx->HandleAndDestroyInternalMessages(
                parsing_ctx->_parsing_response.release());
        }
    } while(true);
    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
}

void ProcessCqlResponse(InputMessageBase* msg_base) {
    DestroyingPtr<CqlInputResponse> msg(static_cast<CqlInputResponse*>(msg_base));

    const int64_t start_parse_us = butil::cpuwide_time_us();
    const bthread_id_t cid = { msg->correlation_id() };
    Controller* cntl = nullptr;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        return;
    }

    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(msg->base_real_us());
        span->set_received_us(msg->received_us());
        span->set_response_size(msg->_response.ByteSize());
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    if (cntl->response() != nullptr) {
        if (cntl->response()->GetDescriptor() != CassandraResponse::descriptor()) {
            cntl->SetFailed(ERESPONSE, "Must be CassandraResponse");
        } else {
            CassandraResponse& response = *(static_cast<CassandraResponse*>(cntl->response()));
            response.MoveFrom(msg->_response);
            // TODO(caidaojin): Move response decode() to ParseCqlMessage may be a better way.
            // Usually, decode failure indicates that protocol has bugs. So we should 
            // close this connection immediately.
            if (!response.Decode()) {
                RPC_VLOG << "Fail to decode cql response: " << response.LastError();
            }
            if (FLAGS_cql_verbose) {
                LOG(ERROR) << "[CQL RESPONSE]: " << CqlMessageOsWrapper(response.cql_response(), true);
            }
        }
    }

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    accessor.OnResponse(cid, saved_error);
}

void SerializeCqlRequest(butil::IOBuf* buf,
                         Controller* cntl,
                         const google::protobuf::Message* request) {
    if (request == nullptr) {
        return cntl->SetFailed(EREQUEST, "Cassandra request is NULL");
    }
    CassandraRequest& cass_req = 
        *(const_cast<CassandraRequest*>(static_cast<const CassandraRequest*>(request)));
    if (request->GetDescriptor() != CassandraRequest::descriptor()) {
        return cntl->SetFailed(EREQUEST, "The request is not a CassandraRequest");
    }

    CqlStreamCreator* sc = CqlStreamCreator::New(cass_req);
    if (sc == nullptr) {
        return cntl->SetFailed("Fail to create cql stream creator");
    }
    cntl->set_stream_creator(sc);
    if (!FLAGS_enable_cql_prepare) {
        if (cass_req.IsValidOpcode() && !cass_req._query.empty()) {
            return cass_req.Encode(buf);
        } else {
            return cntl->SetFailed(EREQUEST, "Invalid cassandra request.");
        }
    }
    cntl->SetFailed(EREQUEST, "Fail to serialize cassandra request.");
}

void PackCqlRequest(butil::IOBuf* packet_buf,
                    SocketMessage** sock_msg,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method_des,
                    Controller* cntl,
                    const butil::IOBuf& request,
                    const Authenticator* auth) {
    ControllerPrivateAccessor accessor(cntl);
    CqlStreamCreator* stream_ctx = 
        dynamic_cast<CqlStreamCreator*>(accessor.stream_creator());
    if (stream_ctx == nullptr) {
        return cntl->SetFailed("None cql stream creator");
    }	
    Socket* sock = accessor.sending_sock();
    CqlParsingContext* parsing_ctx  = GetParsingContextOf(sock);
    if (parsing_ctx == nullptr) {
        return cntl->SetFailed("Fail to get cql parsing context");
    }
    if (auth != nullptr) {
        const CassandraAuthenticator* cass_auth = 
            dynamic_cast<const CassandraAuthenticator*>(auth);
        CHECK(cass_auth != nullptr);
        parsing_ctx->set_auth(cass_auth);
        if (cass_auth->has_set_cql_protocol_version()) {
            parsing_ctx->set_cql_protocol_version(cass_auth->cql_protocol_version());
        }
    }
    const int16_t stream_id = parsing_ctx->AddStream(correlation_id);
    if (stream_id < 0) {
        return cntl->SetFailed("Fail to allocate cql stream");
    }
    CqlFrameHead header(parsing_ctx->cql_protocol_version(),
                        stream_ctx->opcode(),
                        stream_id,
                        request.size());
    if (parsing_ctx->IsReady()) {
        CqlEncodeHead(header, packet_buf);
        packet_buf->append(request);
    } else {
        if (parsing_ctx->AppendToPendingBuf(header, request)) {
            if (parsing_ctx->set_startup_status_once()) {
                // Cql Startup request should be the first message on a connection.
                parsing_ctx->PackCqlStartup(packet_buf);
            } else {
                // During cql connection initialization, we just register correlation id
                // to socket, but write empty data. The actual request data will be written
                // into socket after the conneciton is ready.
                *sock_msg = new CqlDummyMessage(request.size() + header.SerializedSize());
            }
        } else {
            CqlEncodeHead(header, packet_buf);
            packet_buf->append(request);
        }
    }
    if (FLAGS_cql_verbose) {
        LOG(ERROR) << "[CQL REQUEST]: " 
            << CqlMessageOsWrapper(header, request, true);
    }
}

const std::string& GetCqlMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*) {
    const static std::string CQL_SERVER_STR = "cql-server";
    return CQL_SERVER_STR;
}

} // namespace policy
} // namespace brpc
