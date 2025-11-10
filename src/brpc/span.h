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


// NOTE: RPC users are not supposed to include this file.

#ifndef BRPC_SPAN_H
#define BRPC_SPAN_H

#include <stdint.h>
#include <string>
#include <list>
#include <deque>
#include <ostream>
#include <memory>
#include <pthread.h>
#include "butil/macros.h"
#include "butil/endpoint.h"
#include "butil/string_splitter.h"
#include "bvar/collector.h"
#include "bthread/task_meta.h"
#include "brpc/options.pb.h"                 // ProtocolType
#include "brpc/span.pb.h"

namespace bthread {
extern __thread bthread::LocalStorage tls_bls;
}

namespace brpc {

class Span;

void SetTlsParentSpan(std::shared_ptr<Span> span);
std::shared_ptr<Span> GetTlsParentSpan();
void ClearTlsParentSpan();
bool HasTlsParentSpan();

void* CreateBthreadSpanAsVoid();
void DestroyRpczParentSpan(void* ptr);
void EndBthreadSpan();

DECLARE_bool(enable_rpcz);

class Span;
class SpanContainer;

// Deleter for Span.
struct SpanDeleter {
    void operator()(Span* r) const;
};

// Collect information required by /rpcz and tracing system whose idea is
// described in http://static.googleusercontent.com/media/research.google.com/en//pubs/archive/36356.pdf
class Span : public std::enable_shared_from_this<Span> {
friend class SpanDB;
friend struct SpanDeleter;
friend class SpanContainer;
public:
    struct Forbidden {};
    // Call CreateServerSpan/CreateClientSpan instead.
    Span(Forbidden);
    ~Span();

    // Create a span to track a request inside server.
    static std::shared_ptr<Span> CreateServerSpan(
        const std::string& full_method_name,
        uint64_t trace_id, uint64_t span_id, uint64_t parent_span_id,
        int64_t base_real_us);
    // Create a span without name to track a request inside server.
    static std::shared_ptr<Span> CreateServerSpan(
        uint64_t trace_id, uint64_t span_id, uint64_t parent_span_id,
        int64_t base_real_us);

    // Clear all annotations and reset name of the span.
    void ResetServerSpanName(const std::string& name);

    // Create a span to track a request inside channel.
    static std::shared_ptr<Span> CreateClientSpan(const std::string& full_method_name,
                                                  int64_t base_real_us);

    // Create a span to track start bthread
    static std::shared_ptr<Span> CreateBthreadSpan(const std::string& full_method_name,
                                                   int64_t base_real_us);

    static void Submit(std::shared_ptr<Span> span, int64_t cpuwide_time_us);

    // Set this span as the TLS parent for subsequent child span creation.
    // Typical flow:
    // 1. Server span calls AsParent() before user callback to enable tracing
    // 2. Client spans created in user code automatically link to this parent
    // 3. When client RPC completes, it restores its own parent via AsParent()
    //    to maintain the trace chain (see Controller::SubmitSpan)
    // 4. Server span calls EndAsParent() when submitting to clear TLS parent
    void AsParent() {
        SetTlsParentSpan(shared_from_this());
    }

    // Add log with time.
    void Annotate(const char* fmt, ...);
    void Annotate(const char* fmt, va_list args);
    void Annotate(const std::string& info);
    // When length <= 0, use strlen instead.
    void AnnotateCStr(const char* cstr, size_t length);

    // #child spans, Not O(1)
    size_t CountClientSpans() const;

    int64_t GetStartRealTimeUs() const;
    int64_t GetEndRealTimeUs() const;

    void set_log_id(uint64_t cid) { _log_id = cid; }
    void set_base_cid(bthread_id_t id) { _base_cid = id; }
    void set_ending_cid(bthread_id_t id) { _ending_cid = id; }
    void set_remote_side(const butil::EndPoint& pt) { _remote_side = pt; }
    void set_protocol(ProtocolType p) { _protocol = p; }
    void set_error_code(int error_code) { _error_code = error_code; }
    void set_request_size(int size) { _request_size = size; }
    void set_response_size(int size) { _response_size = size; }
    void set_async(bool async) { _async = async; }
    
    void set_base_real_us(int64_t tm) { _base_real_us = tm; }
    void set_received_us(int64_t tm)
    { _received_real_us = tm + _base_real_us; }
    void set_start_parse_us(int64_t tm)
    { _start_parse_real_us = tm + _base_real_us; }
    void set_start_callback_us(int64_t tm)
    { _start_callback_real_us = tm + _base_real_us; }
    void set_start_send_us(int64_t tm)
    { _start_send_real_us = tm + _base_real_us; }
    void set_sent_us(int64_t tm)
    { _sent_real_us = tm + _base_real_us; }

    bool is_active() const { return _ending_cid == INVALID_BTHREAD_ID; }

    std::weak_ptr<Span> local_parent() const { return _local_parent; }
    static std::shared_ptr<Span> tls_parent() {
        auto parent = GetTlsParentSpan();
        if (parent && parent->is_active()) {
            return parent;
        }
        return nullptr;
    }

    uint64_t trace_id() const { return _trace_id; }
    uint64_t parent_span_id() const { return _parent_span_id; }
    uint64_t span_id() const { return _span_id; }
    uint64_t log_id() const { return _log_id; }
    bthread_id_t base_cid() const { return _base_cid; }
    bthread_id_t ending_cid() const { return _ending_cid; }
    const butil::EndPoint& remote_side() const { return _remote_side; }
    SpanType type() const { return _type; }
    ProtocolType protocol() const { return _protocol; }
    int error_code() const { return _error_code; }
    int request_size() const { return _request_size; }
    int response_size() const { return _response_size; }
    int64_t received_real_us() const { return _received_real_us; }
    int64_t start_parse_real_us() const { return _start_parse_real_us; }
    int64_t start_callback_real_us() const { return _start_callback_real_us; }
    int64_t start_send_real_us() const { return _start_send_real_us; }
    int64_t sent_real_us() const { return _sent_real_us; }
    bool async() const { return _async; }
    const std::string& full_method_name() const { return _full_method_name; }
    
    // Returns a copy instead of a reference for thread safety.
    // 
    // Current usage: Only called by Span2Proto() which immediately passes the result
    // to protobuf's set_info(). In this specific scenario, returning a reference would
    // also be safe because set_info() copies the string before the reference could be
    // invalidated by concurrent Annotate() calls.
    //
    // However, returning by value is more robust: it prevents potential data races if
    // future code holds the reference longer, and has no performance penalty due to
    // C++11 move semantics (the temporary is moved, not copied, into protobuf).
    std::string info() const { 
        BAIDU_SCOPED_LOCK(_info_spinlock);
        return _info; 
    }
    
private:
    DISALLOW_COPY_AND_ASSIGN(Span);

    void dump_to_db();
    void submit(int64_t cpuwide_us);
    bvar::CollectorSpeedLimit* speed_limit();
    bvar::CollectorPreprocessor* preprocessor();

    // Clear this span from TLS parent if it's currently set as the parent.
    // Called when server span is being submitted to prevent subsequent spans
    // from incorrectly linking to an ended span. Only clears if the current
    // TLS parent is this span (avoids clearing if another span has taken over).
    void EndAsParent() {
        std::shared_ptr<Span> current_parent = GetTlsParentSpan();
        if (current_parent.get() == this) {
            ClearTlsParentSpan();
        }
    }

    uint64_t _trace_id;
    uint64_t _span_id;
    uint64_t _parent_span_id;
    uint64_t _log_id;
    bthread_id_t _base_cid;
    bthread_id_t _ending_cid;
    butil::EndPoint _remote_side;
    SpanType _type;
    bool _async;
    ProtocolType _protocol;
    int _error_code;
    int  _request_size;
    int  _response_size;
    int64_t _base_real_us;
    int64_t _received_real_us;
    int64_t _start_parse_real_us;
    int64_t _start_callback_real_us;
    int64_t _start_send_real_us;
    int64_t _sent_real_us;
    std::string _full_method_name;
    // Format: 
    //   time1_us \s annotation1 <SEP>
    //   time2_us \s annotation2 <SEP>
    //   ...
    std::string _info;
    // Protects _info from concurrent modifications.
    // Multiple threads may call Annotate() simultaneously (e.g., retry logic,
    // network layer, user code via TRACEPRINTF), causing data corruption in
    // string concatenation without synchronization.
    mutable pthread_spinlock_t _info_spinlock;

    std::weak_ptr<Span> _local_parent;
    std::list<std::shared_ptr<Span>> _client_list;
    // Protects _client_list from concurrent modifications.
    // In some scenarios, multiple bthreads may simultaneously create child spans
    // (e.g.,raft leader parallel RPCs to followers) and push_back to parent's _client_list.
    // Also protects against concurrent iteration (e.g., CountClientSpans, SpanDB::Index)
    // while the list is being modified.
    mutable pthread_spinlock_t _client_list_spinlock;
};

class SpanContainer : public bvar::Collected {
public:
    explicit SpanContainer(std::shared_ptr<Span> span) : _span(span) {}
    ~SpanContainer() {}

    // Implementations of bvar::Collected
    void dump_and_destroy(size_t round_index) override;
    void destroy() override;
    bvar::CollectorSpeedLimit* speed_limit() override;

    void submit(int64_t cpuwide_us);

    const std::shared_ptr<Span>& span() const { return _span; }

private:
    std::shared_ptr<Span> _span;
};

// Extract name and annotations from Span::info()
class SpanInfoExtractor {
public:
    SpanInfoExtractor(const char* info);
    bool PopAnnotation(int64_t before_this_time,
                       int64_t* time, std::string* annotation);
private:
    butil::StringSplitter _sp;
};

// These two functions can be used for composing TRACEPRINT// Add an annotation to the current span.
// If current bthread is not tracing, this function does nothing.
void AnnotateSpan(const char* fmt, ...);

// Add an annotation to the given span.
// If the span is NULL, this function does nothing.
void AnnotateSpanEx(std::shared_ptr<Span> span, const char* fmt, ...);


class SpanFilter {
public:
    virtual ~SpanFilter() = default;
    virtual bool Keep(const BriefSpan&) = 0;
};

class SpanDB;
    
// Find a span by its trace_id and span_id, serialize it into `span'.
int FindSpan(uint64_t trace_id, uint64_t span_id, RpczSpan* span);

// Find spans by their trace_id, serialize them into `out'
void FindSpans(uint64_t trace_id, std::deque<RpczSpan>* out);

// Put at most `max_scan' spans before `before_this_time' into `out'.
// If filter is not NULL, only push spans that make SpanFilter::Keep()
// true.
void ListSpans(int64_t before_this_time, size_t max_scan,
               std::deque<BriefSpan>* out, SpanFilter* filter);

void DescribeSpanDB(std::ostream& os);

SpanDB* LoadSpanDBFromFile(const char* filepath);
int FindSpan(SpanDB* db, uint64_t trace_id, uint64_t span_id, RpczSpan* span);
void FindSpans(SpanDB* db, uint64_t trace_id, std::deque<RpczSpan>* out);
void ListSpans(SpanDB* db, int64_t before_this_time, size_t max_scan,
               std::deque<BriefSpan>* out, SpanFilter* filter);

// Check this function first before creating a span.
// If rpcz of upstream is enabled, local rpcz is enabled automatically.
inline bool IsTraceable(bool is_upstream_traced) {
    extern bvar::CollectorSpeedLimit g_span_sl;
    return is_upstream_traced ||
        (FLAGS_enable_rpcz && bvar::is_collectable(&g_span_sl));
}

} // namespace brpc


#endif // BRPC_SPAN_H
