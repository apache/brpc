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


#include <netinet/in.h>
#include <gflags/gflags.h>
#include <leveldb/db.h>
#include <leveldb/comparator.h>
#include "bthread/bthread.h"
#include "butil/scoped_lock.h"
#include "butil/thread_local.h"
#include "butil/string_printf.h"
#include "butil/time.h"
#include "butil/logging.h"
#include "butil/object_pool.h"
#include "butil/fast_rand.h"
#include "butil/file_util.h"
#include "brpc/shared_object.h"
#include "brpc/reloadable_flags.h"
#include "brpc/span.h"

#define BRPC_SPAN_INFO_SEP "\1"


namespace brpc {

const int64_t SPAN_DELETE_INTERVAL_US = 10000000L/*10s*/;

DEFINE_string(rpcz_database_dir, "./rpc_data/rpcz",
              "For storing requests/contexts collected by rpcz.");

// TODO: collected per second is customizable.
// const int32_t MAX_RPCZ_MAX_SPAN_PER_SECOND = 10000;
// DEFINE_int32(rpcz_max_span_per_second, MAX_RPCZ_MAX_SPAN_PER_SECOND,
//              "Index so many spans per second at most");
// static bool validate_rpcz_max_span_per_second(const char*, int32_t val) {
//     return (val >= 1 && val <= MAX_RPCZ_MAX_SPAN_PER_SECOND);
// }
// BRPC_VALIDATE_GFLAG(rpcz_max_span_per_second,
//                          validate_rpcz_max_span_per_second);

DEFINE_int32(rpcz_keep_span_seconds, 3600,
             "Keep spans for at most so many seconds");
BRPC_VALIDATE_GFLAG(rpcz_keep_span_seconds, PositiveInteger);

DEFINE_bool(rpcz_keep_span_db, false, "Don't remove DB of rpcz at program's exit");

struct IdGen {
    bool init;
    uint16_t seq;
    uint64_t current_random;
    butil::FastRandSeed seed;
};

static __thread IdGen tls_trace_id_gen = { false, 0, 0, { { 0, 0 } } };
static __thread IdGen tls_span_id_gen = { false, 0, 0, { { 0, 0 } } };

inline uint64_t UpdateTLSRandom64(IdGen* g) {
    if (!g->init) {
        g->init = true;
        init_fast_rand_seed(&g->seed);
    }
    const uint64_t val = fast_rand(&g->seed);
    g->current_random = val;
    return val;
}

inline uint64_t GenerateSpanId() {
    // 0 is an invalid Id
    IdGen* g = &tls_trace_id_gen;
    if (g->seq == 0) {
        UpdateTLSRandom64(g);
        g->seq = 1;
    }
    return (g->current_random & 0xFFFFFFFFFFFF0000ULL) | g->seq++;
}

inline uint64_t GenerateTraceId() {
    // 0 is an invalid Id
    IdGen* g = &tls_span_id_gen;
    if (g->seq == 0) {
        UpdateTLSRandom64(g);
        g->seq = 1;
    }
    return (g->current_random & 0xFFFFFFFFFFFF0000ULL) | g->seq++;
}

Span* Span::CreateClientSpan(const std::string& full_method_name,
                             int64_t base_real_us) {
    Span* span = butil::get_object<Span>(Forbidden());
    if (__builtin_expect(span == NULL, 0)) {
        return NULL;
    }
    span->_log_id = 0;
    span->_base_cid = INVALID_BTHREAD_ID;
    span->_ending_cid = INVALID_BTHREAD_ID;
    span->_type = SPAN_TYPE_CLIENT;
    span->_async = false;
    span->_protocol = PROTOCOL_UNKNOWN;
    span->_error_code = 0;
    span->_request_size = 0;
    span->_response_size = 0;
    span->_base_real_us = base_real_us;
    span->_received_real_us = 0;
    span->_start_parse_real_us = 0;
    span->_start_callback_real_us = 0;
    span->_start_send_real_us = 0;
    span->_sent_real_us = 0;
    span->_next_client = NULL;
    span->_tls_next = NULL;
    span->_full_method_name = full_method_name;
    span->_info.clear();
    Span* parent = (Span*)bthread::tls_bls.rpcz_parent_span;
    if (parent) {
        span->_trace_id = parent->trace_id();
        span->_parent_span_id = parent->span_id();
        span->_local_parent = parent;
        span->_next_client = parent->_next_client;
        parent->_next_client = span;
    } else {
        span->_trace_id = GenerateTraceId();
        span->_parent_span_id = 0;
        span->_local_parent = NULL;
    }
    span->_span_id = GenerateSpanId();
    return span;
}

inline const std::string& unknown_span_name() {
    // thread-safe in gcc.
    static std::string s_unknown_method_name = "unknown_method";
    return s_unknown_method_name;
}

Span* Span::CreateServerSpan(
    const std::string& full_method_name,
    uint64_t trace_id, uint64_t span_id, uint64_t parent_span_id,
    int64_t base_real_us) {
    Span* span = butil::get_object<Span>(Forbidden());
    if (__builtin_expect(span == NULL, 0)) {
        return NULL;
    }
    span->_trace_id = (trace_id ? trace_id : GenerateTraceId());
    span->_span_id = (span_id ? span_id : GenerateSpanId());
    span->_parent_span_id = parent_span_id;
    span->_log_id = 0;
    span->_base_cid = INVALID_BTHREAD_ID;
    span->_ending_cid = INVALID_BTHREAD_ID;
    span->_type = SPAN_TYPE_SERVER;
    span->_async = false;
    span->_protocol = PROTOCOL_UNKNOWN;
    span->_error_code = 0;
    span->_request_size = 0;
    span->_response_size = 0;
    span->_base_real_us = base_real_us;
    span->_received_real_us = 0;
    span->_start_parse_real_us = 0;
    span->_start_callback_real_us = 0;
    span->_start_send_real_us = 0;
    span->_sent_real_us = 0;
    span->_next_client = NULL;
    span->_tls_next = NULL;
    span->_full_method_name = (!full_method_name.empty() ?
                               full_method_name : unknown_span_name());
    span->_info.clear();
    span->_local_parent = NULL;
    return span;
}

Span* Span::CreateServerSpan(
    uint64_t trace_id, uint64_t span_id, uint64_t parent_span_id,
    int64_t base_real_us) {
    return CreateServerSpan(unknown_span_name(), trace_id, span_id,
                            parent_span_id, base_real_us);
}

void Span::ResetServerSpanName(const std::string& full_method_name) {
    _full_method_name = (!full_method_name.empty() ?
                         full_method_name : unknown_span_name());
}

void Span::destroy() {
    EndAsParent();
    Span* p = _next_client;
    while (p) {
        Span* p_next = p->_next_client;
        p->_info.clear();
        butil::return_object(p);
        p = p_next;
    }
    _info.clear();
    butil::return_object(this);
}

void Span::Annotate(const char* fmt, ...) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    butil::string_appendf(&_info, BRPC_SPAN_INFO_SEP "%lld ",
                         (long long)anno_time);
    va_list ap;
    va_start(ap, fmt);
    butil::string_vappendf(&_info, fmt, ap);
    va_end(ap);
}

void Span::Annotate(const char* fmt, va_list args) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    butil::string_appendf(&_info, BRPC_SPAN_INFO_SEP "%lld ",
                         (long long)anno_time);
    butil::string_vappendf(&_info, fmt, args);
}

void Span::Annotate(const std::string& info) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    butil::string_appendf(&_info, BRPC_SPAN_INFO_SEP "%lld ",
                         (long long)anno_time);
    _info.append(info);
}

void Span::AnnotateCStr(const char* info, size_t length) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    butil::string_appendf(&_info, BRPC_SPAN_INFO_SEP "%lld ",
                         (long long)anno_time);
    if (length <= 0) {
        _info.append(info);
    } else {
        _info.append(info, length);
    }
}

size_t Span::CountClientSpans() const {
    size_t n = 0;
    for (Span* p = _next_client; p; p = p->_next_client, ++n);
    return n;
}

int64_t Span::GetStartRealTimeUs() const {
    return _type == SPAN_TYPE_SERVER ? _received_real_us : _start_send_real_us;
}

int64_t Span::GetEndRealTimeUs() const {
    int64_t result = 0;
    result = std::max(result, _received_real_us);
    result = std::max(result, _start_parse_real_us);
    result = std::max(result, _start_callback_real_us);
    result = std::max(result, _start_send_real_us);
    result = std::max(result, _sent_real_us);
    return result;
}

SpanInfoExtractor::SpanInfoExtractor(const char* info)
    : _sp(info, *BRPC_SPAN_INFO_SEP) {
}

bool SpanInfoExtractor::PopAnnotation(
    int64_t before_this_time, int64_t* time, std::string* annotation) {
    for (; _sp != NULL; ++_sp) {
        butil::StringSplitter sp_time(_sp.field(), _sp.field() + _sp.length(), ' ');
        if (sp_time) {
            char* endptr;
            const int64_t anno_time = strtoll(sp_time.field(), &endptr, 10);
            if (*endptr == ' ') {
                if (before_this_time <= anno_time) {
                    return false;
                }
                *time = anno_time;
                ++sp_time;
                annotation->assign(
                    sp_time.field(),
                    _sp.field() + _sp.length() - sp_time.field());
                ++_sp;
                return true;
            }
        }
        LOG(ERROR) << "Unknown annotation: "
                    << std::string(_sp.field(), _sp.length());
    }
    return false;
}

bool CanAnnotateSpan() {
    return bthread::tls_bls.rpcz_parent_span;
}
    
void AnnotateSpan(const char* fmt, ...) {
    Span* span = (Span*)bthread::tls_bls.rpcz_parent_span;
    va_list ap;
    va_start(ap, fmt);
    span->Annotate(fmt, ap);
    va_end(ap);
}

class SpanDB : public SharedObject {
public:
    leveldb::DB* id_db;
    leveldb::DB* time_db;
    std::string id_db_name;
    std::string time_db_name;

    SpanDB() : id_db(NULL), time_db(NULL) { }
    static SpanDB* Open();
    leveldb::Status Index(const Span* span, std::string* value_buf);
    leveldb::Status RemoveSpansBefore(int64_t tm);

private:
    static void Swap(SpanDB& db1, SpanDB& db2) {
        std::swap(db1.id_db, db2.id_db);
        std::swap(db1.id_db_name, db2.id_db_name);
        std::swap(db1.time_db, db2.time_db);
        std::swap(db1.time_db_name, db2.time_db_name);
    }
    
    ~SpanDB() {
        if (id_db == NULL && time_db == NULL) {
            return;
        }
        delete id_db;
        delete time_db;
        if (!FLAGS_rpcz_keep_span_db) {
            std::string cmd = butil::string_printf("rm -rf %s %s",
                                                  id_db_name.c_str(),
                                                  time_db_name.c_str());
            butil::ignore_result(system(cmd.c_str()));
        }
    }
};

static bool started_span_indexing = false;
static pthread_once_t start_span_indexing_once = PTHREAD_ONCE_INIT;
static int64_t g_last_time_key = 0;
static int64_t g_last_delete_tm = 0;

// Following variables are monitored by builtin services, thus non-static.
static pthread_mutex_t g_span_db_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_span_ending = false;  // don't open span again if this var is true.
// Can't use intrusive_ptr which has ctor/dtor issues.
static SpanDB* g_span_db = NULL;
bool has_span_db() { return !!g_span_db; }
bvar::CollectorSpeedLimit g_span_sl = BVAR_COLLECTOR_SPEED_LIMIT_INITIALIZER;
static bvar::DisplaySamplingRatio s_display_sampling_ratio(
    "rpcz_sampling_ratio", &g_span_sl);

struct SpanEarlier {
    bool operator()(bvar::Collected* c1, bvar::Collected* c2) const {
        return ((Span*)c1)->GetStartRealTimeUs() < ((Span*)c2)->GetStartRealTimeUs();
    }
};
class SpanPreprocessor : public bvar::CollectorPreprocessor {
public:
    void process(std::vector<bvar::Collected*> & list) {
        // Sort spans by their starting time so that the code on making
        // time monotonic in Span::Index works better.
        std::sort(list.begin(), list.end(), SpanEarlier()); 
    }
};
static SpanPreprocessor* g_span_prep = NULL;

bvar::CollectorSpeedLimit* Span::speed_limit() {
    return &g_span_sl;
}

bvar::CollectorPreprocessor* Span::preprocessor() {
    return g_span_prep;
}

static void ResetSpanDB(SpanDB* db) {
    SpanDB* old_db = NULL;
    {
        BAIDU_SCOPED_LOCK(g_span_db_mutex);
        old_db = g_span_db;
        g_span_db = db;
        if (g_span_db) {
            g_span_db->AddRefManually();
        }
    }
    if (old_db) {
        old_db->RemoveRefManually();
    }
}

static void RemoveSpanDB() {
    g_span_ending = true;
    ResetSpanDB(NULL);
}

static void StartSpanIndexing() {
    atexit(RemoveSpanDB);
    g_span_prep = new SpanPreprocessor;
    started_span_indexing = true;
}

static int StartIndexingIfNeeded() {
    if (pthread_once(&start_span_indexing_once, StartSpanIndexing) != 0) {
        return -1;
    }
    return started_span_indexing ? 0 : -1;
}

inline int GetSpanDB(butil::intrusive_ptr<SpanDB>* db) {
    BAIDU_SCOPED_LOCK(g_span_db_mutex);
    if (g_span_db != NULL) {
        *db = g_span_db;
        return 0;
    }
    return -1;
}

void Span::Submit(Span* span, int64_t cpuwide_time_us) {
    if (span->local_parent() == NULL) {
        span->submit(cpuwide_time_us);
    }
}

static void Span2Proto(const Span* span, RpczSpan* out) {
    out->set_trace_id(span->trace_id());
    out->set_span_id(span->span_id());
    out->set_parent_span_id(span->parent_span_id());
    out->set_log_id(span->log_id());
    out->set_base_cid(span->base_cid().value);
    out->set_ending_cid(span->ending_cid().value);
    out->set_remote_ip(butil::ip2int(span->remote_side().ip));
    out->set_remote_port(span->remote_side().port);
    out->set_type(span->type());
    out->set_async(span->async());
    out->set_protocol(span->protocol());
    out->set_request_size(span->request_size());
    out->set_response_size(span->response_size());
    out->set_received_real_us(span->received_real_us());
    out->set_start_parse_real_us(span->start_parse_real_us());
    out->set_start_callback_real_us(span->start_callback_real_us());
    out->set_start_send_real_us(span->start_send_real_us());
    out->set_sent_real_us(span->sent_real_us());
    out->set_full_method_name(span->full_method_name());
    out->set_info(span->info());
    out->set_error_code(span->error_code());
}

inline void ToBigEndian(uint64_t n, uint32_t* buf) {
    buf[0] = htonl(n >> 32);
    buf[1] = htonl(n & 0xFFFFFFFFUL);
}

inline uint64_t ToLittleEndian(const uint32_t* buf) {
    return (((uint64_t)ntohl(buf[0])) << 32) | ntohl(buf[1]);
}

SpanDB* SpanDB::Open() {
    SpanDB local;
    leveldb::Status st;
    char prefix[64];
    time_t rawtime;
    time(&rawtime);
    struct tm lt_buf;
    struct tm* timeinfo = localtime_r(&rawtime, &lt_buf);
    const size_t nw = strftime(prefix, sizeof(prefix),
                               "/%Y%m%d.%H%M%S", timeinfo);
    const int nw2 = snprintf(prefix + nw, sizeof(prefix) - nw, ".%d",
                             getpid());
    leveldb::Options options;
    options.create_if_missing = true;
    options.error_if_exists = true;

    local.id_db_name.append(FLAGS_rpcz_database_dir);
    local.id_db_name.append(prefix, nw + nw2);
    // Create the dir first otherwise leveldb fails.
    butil::File::Error error;
    const butil::FilePath dir(local.id_db_name);
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        LOG(ERROR) << "Fail to create directory=`" << dir.value() << ", "
                   << error;
        return NULL;
    }

    local.id_db_name.append("/id.db");
    st = leveldb::DB::Open(options, local.id_db_name.c_str(), &local.id_db);
    if (!st.ok()) {
        LOG(ERROR) << "Fail to open id_db: " << st.ToString();
        return NULL;
    }

    local.time_db_name.append(FLAGS_rpcz_database_dir);
    local.time_db_name.append(prefix, nw + nw2);
    local.time_db_name.append("/time.db");
    st = leveldb::DB::Open(options, local.time_db_name.c_str(), &local.time_db);
    if (!st.ok()) {
        LOG(ERROR) << "Fail to open time_db: " << st.ToString();
        return NULL;
    }
    SpanDB* db = new (std::nothrow) SpanDB;
    if (NULL == db) {
        return NULL;
    }
    LOG(INFO) << "Opened " << local.id_db_name << " and "
               << local.time_db_name;
    Swap(local, *db);
    return db;
}

leveldb::Status SpanDB::Index(const Span* span, std::string* value_buf) {
    leveldb::WriteOptions options;
    options.sync = false;

    leveldb::Status st;
    
    // NOTE: Writing into time_db before id_db so that if the second write
    // fails, the entry in time_db will be finally removed when it's out
    // of time window.

    const int64_t start_time = span->GetStartRealTimeUs();
    BriefSpan brief;
    brief.set_trace_id(span->trace_id());
    brief.set_span_id(span->span_id());
    brief.set_log_id(span->log_id());
    brief.set_type(span->type());
    brief.set_error_code(span->error_code());
    brief.set_request_size(span->request_size());
    brief.set_response_size(span->response_size());
    brief.set_start_real_us(start_time);
    brief.set_latency_us(span->GetEndRealTimeUs() - start_time);
    brief.set_full_method_name(span->full_method_name());
    if (!brief.SerializeToString(value_buf)) {
        return leveldb::Status::InvalidArgument(
            leveldb::Slice("Fail to serialize BriefSpan"));
    }
    // We need to make the time monotonic otherwise if older entries are
    // overwritten by newer ones, entries in id_db associated with the older
    // entries are not evicted. Surely we can call DB::Get() before Put(), but
    // that would be too slow due to the storage model of leveldb. One feasible
    // method is to maintain recent window of keys to time_db, when there's a
    // conflict before Put(), try key+1us until an unused time is found. The
    // window could be 5~10s. However this method needs a std::map(slow) or
    // hashmap+queue(more memory: remember that we're just a framework), and
    // this method can't guarantee no duplication when real time goes back
    // significantly.
    // Since the time to this method is ALMOST in ascending order, we use a
    // very simple strategy: if the time is not greater than last-time, set
    // it to be last-time + 1us. This works when time goes back because the
    // real time is at least 1000000 / FLAGS_rpcz_max_span_per_second times faster
    // and it will finally catch up with our time key. (provided the flag
    // is less than 1000000).
    int64_t time_key = start_time;
    if (time_key <= g_last_time_key) {
        time_key = g_last_time_key + 1;
    }
    g_last_time_key = time_key;
    uint32_t time_data[2];
    ToBigEndian(time_key, time_data);
    st = time_db->Put(options,
                      leveldb::Slice((char*)time_data, sizeof(time_data)),
                      leveldb::Slice(value_buf->data(), value_buf->size()));
    if (!st.ok()) {
        return st;
    }
    
    uint32_t key_data[4];
    ToBigEndian(span->trace_id(), key_data);
    ToBigEndian(span->span_id(), key_data + 2);
    leveldb::Slice key((char*)key_data, sizeof(key_data));
    RpczSpan value_proto;
    Span2Proto(span, &value_proto);
    // client spans should be reversed.
    size_t client_span_count = span->CountClientSpans();
    for (size_t i = 0; i < client_span_count; ++i) {
        value_proto.add_client_spans();
    }
    size_t i = 0;
    for (const Span* p = span->_next_client; p; p = p->_next_client, ++i) {
        Span2Proto(p, value_proto.mutable_client_spans(client_span_count - i - 1));
    }
    if (!value_proto.SerializeToString(value_buf)) {
        return leveldb::Status::InvalidArgument(
            leveldb::Slice("Fail to serialize RpczSpan"));
    }
    leveldb::Slice value(value_buf->data(), value_buf->size());
    st = id_db->Put(options, key, value);
    return st;
}

// NOTE: may take more than 100ms
leveldb::Status SpanDB::RemoveSpansBefore(int64_t tm) {
    if (id_db == NULL || time_db == NULL) {
        return leveldb::Status::InvalidArgument(leveldb::Slice("NULL param"));
    }
    leveldb::Status rc;
    leveldb::WriteOptions options;
    options.sync = false;
    leveldb::Iterator* it = time_db->NewIterator(leveldb::ReadOptions());
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if (it->key().size() != 8) {
            LOG(ERROR) << "Invalid key size: " << it->key().size();
            continue;
        }
        const int64_t realtime = 
            ToLittleEndian((const uint32_t*)it->key().data());
        if (realtime >= tm) {  // removal is done.
            break;
        }
        BriefSpan brief;
        if (brief.ParseFromArray(it->value().data(), it->value().size())) {
            uint32_t key_data[4];
            ToBigEndian(brief.trace_id(), key_data);
            ToBigEndian(brief.span_id(), key_data + 2);
            leveldb::Slice key((char*)key_data, sizeof(key_data));
            rc = id_db->Delete(options, key);
            if (!rc.ok()) {
                LOG(ERROR) << "Fail to delete from id_db";
                break;
            }
        } else {
            LOG(ERROR) << "Fail to parse from value";
        }
        rc = time_db->Delete(options, it->key());
        if (!rc.ok()) {
            LOG(ERROR) << "Fail to delete from time_db";
            break;
        }
    }
    delete it;
    return rc;
}

// Write span into leveldb.
void Span::dump_and_destroy(size_t /*round*/) {
    StartIndexingIfNeeded();
    
    std::string value_buf;

    butil::intrusive_ptr<SpanDB> db;
    if (GetSpanDB(&db) != 0) {
        if (g_span_ending) {
            destroy();
            return;
        }
        SpanDB* db2 = SpanDB::Open();
        if (db2 == NULL) {
            LOG(WARNING) << "Fail to open SpanDB";
            destroy();
            return;
        }
        ResetSpanDB(db2);
        db.reset(db2);
    }

    leveldb::Status st = db->Index(this, &value_buf);
    destroy();
    if (!st.ok()) {
        LOG(WARNING) << st.ToString();
        if (st.IsNotFound() || st.IsIOError() || st.IsCorruption()) {
            ResetSpanDB(NULL);
            return;
        }
    }

    // Remove old spans
    const int64_t now = butil::gettimeofday_us();
    if (now > g_last_delete_tm + SPAN_DELETE_INTERVAL_US) {
        g_last_delete_tm = now;
        leveldb::Status st = db->RemoveSpansBefore(
            now - FLAGS_rpcz_keep_span_seconds * 1000000L);
        if (!st.ok()) {
            LOG(ERROR) << st.ToString();
            if (st.IsNotFound() || st.IsIOError() || st.IsCorruption()) {
                ResetSpanDB(NULL);
                return;
            }
        }
    }
}

int FindSpan(uint64_t trace_id, uint64_t span_id, RpczSpan* response) {
    butil::intrusive_ptr<SpanDB> db;
    if (GetSpanDB(&db) != 0) {
        return -1;
    }
    uint32_t key_data[4];
    ToBigEndian(trace_id, key_data);
    ToBigEndian(span_id, key_data + 2);
    leveldb::Slice key((char*)key_data, sizeof(key_data));
    std::string value;
    leveldb::Status st = db->id_db->Get(leveldb::ReadOptions(), key, &value);
    if (!st.ok()) {
        return -1;
    }
    if (!response->ParseFromString(value)) {
        LOG(ERROR) << "Fail to parse from the value";
        return -1;
    }
    return 0;
}

void FindSpans(uint64_t trace_id, std::deque<RpczSpan>* out) {
    out->clear();
    butil::intrusive_ptr<SpanDB> db;
    if (GetSpanDB(&db) != 0) {
        return;
    }
    leveldb::Iterator* it = db->id_db->NewIterator(leveldb::ReadOptions());
    uint32_t key_data[4];
    ToBigEndian(trace_id, key_data);
    ToBigEndian(0, key_data + 2);
    leveldb::Slice key((char*)key_data, sizeof(key_data));
    for (it->Seek(key); it->Valid(); it->Next()) {
        if (it->key().size() != sizeof(key_data)) {
            LOG(ERROR) << "Invalid key size: " << it->key().size();
            break;
        }
        const uint64_t stored_trace_id =
            ToLittleEndian((const uint32_t*)it->key().data());
        if (trace_id != stored_trace_id) {
            break;
        }
        RpczSpan span;
        if (span.ParseFromArray(it->value().data(), it->value().size())) {
            out->push_back(span);
        } else {
            LOG(ERROR) << "Fail to parse from value";
        }
    }
    delete it;
}

void ListSpans(int64_t starting_realtime, size_t max_scan,
               std::deque<BriefSpan>* out, SpanFilter* filter) {
    out->clear();
    butil::intrusive_ptr<SpanDB> db;
    if (GetSpanDB(&db) != 0) {
        return;
    }
    leveldb::Iterator* it = db->time_db->NewIterator(leveldb::ReadOptions());
    uint32_t time_data[2];
    ToBigEndian(starting_realtime, time_data);
    leveldb::Slice key((char*)time_data, sizeof(time_data));
    it->Seek(key);
    if (!it->Valid()) {
        it->SeekToLast();
    }
    BriefSpan brief;
    size_t nscan = 0;
    for (size_t i = 0; nscan < max_scan && it->Valid(); ++i, it->Prev()) {
        const int64_t key_tm = ToLittleEndian((const uint32_t*)it->key().data());
        // May have some bigger time at the beginning, because leveldb returns
        // keys >= starting_realtime.
        if (key_tm > starting_realtime) {
            continue;
        }
        brief.Clear();
        if (brief.ParseFromArray(it->value().data(), it->value().size())) {
            if (NULL == filter || filter->Keep(brief)) {
                out->push_back(brief);
            }
            // We increase the count no matter filter passed or not to avoid
            // scaning too many entries.
            ++nscan;
        } else {
            LOG(ERROR) << "Fail to parse from value";
        }
    }
    delete it;
}

void DescribeSpanDB(std::ostream& os) {
    butil::intrusive_ptr<SpanDB> db;
    if (GetSpanDB(&db) != 0) {
        return;
    }
    
    if (db->id_db != NULL) {
        std::string val;
        if (db->id_db->GetProperty(leveldb::Slice("leveldb.stats"), &val)) {
            os << "[ " << db->id_db_name << " ]\n" << val;
        }
        if (db->id_db->GetProperty(leveldb::Slice("leveldb.sstables"), &val)) {
            os << '\n' << val;
        }
    }
    os << '\n';
    if (db->time_db != NULL) {
        std::string val;
        if (db->time_db->GetProperty(leveldb::Slice("leveldb.stats"), &val)) {
            os << "[ " << db->time_db_name << " ]\n" << val;
        }
        if (db->time_db->GetProperty(leveldb::Slice("leveldb.sstables"), &val)) {
            os << '\n' << val;
        }
    }
}

} // namespace brpc
