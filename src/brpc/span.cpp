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

// Authors: Ge,Jun (gejun@baidu.com)

#include <netinet/in.h>
#include <gflags/gflags.h>
#include "bthread/bthread.h"
#include "butil/thread_local.h"
#include "butil/string_printf.h"
#include "butil/time.h"
#include "butil/logging.h"
#include "butil/object_pool.h"
#include "butil/fast_rand.h"
#include "brpc/reloadable_flags.h"
#include "brpc/span_exporter.h"
#include "brpc/span.h"

namespace brpc {

// TODO: collected per second is customizable.
// const int32_t MAX_RPCZ_MAX_SPAN_PER_SECOND = 10000;
// DEFINE_int32(rpcz_max_span_per_second, MAX_RPCZ_MAX_SPAN_PER_SECOND,
//              "Index so many spans per second at most");
// static bool validate_rpcz_max_span_per_second(const char*, int32_t val) {
//     return (val >= 1 && val <= MAX_RPCZ_MAX_SPAN_PER_SECOND);
// }
// BRPC_VALIDATE_GFLAG(rpcz_max_span_per_second,
//                          validate_rpcz_max_span_per_second);

DEFINE_bool(enable_trace, false, "Turn on to start generating spans.");
BRPC_VALIDATE_GFLAG(enable_trace, PassValidate);

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
    span->_annotation_list.clear();
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
    span->_annotation_list.clear();
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
        p->_annotation_list.clear();
        butil::return_object(p);
        p = p_next;
    }
    _annotation_list.clear();
    butil::return_object(this);
}

void Span::Annotate(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Annotate(fmt, ap);
    va_end(ap);
}

void Span::Annotate(const char* fmt, va_list args) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    std::string anno_content;
    butil::string_vappendf(&anno_content, fmt, args);
    _annotation_list.emplace_back(anno_time, std::move(anno_content));
}

void Span::Annotate(const std::string& info) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    _annotation_list.push_back(Annotation(anno_time, info));
}

void Span::AnnotateCStr(const char* info, size_t length) {
    const int64_t anno_time = butil::cpuwide_time_us() + _base_real_us;
    std::string anno_content;
    if (length > 0) {
        anno_content = std::string(info, length);
    } else {
        anno_content = std::string(info);
    }
    _annotation_list.emplace_back(anno_time, std::move(anno_content));
}

size_t Span::CountClientSpans() const {
    size_t n = 0;
    for (Span* p = _next_client; p; p = p->_next_client, ++n);
    return n;
}

void Span::Copy2TracingSpan(TracingSpan* out) const {
    out->clear_client_spans();
    size_t client_span_count = CountClientSpans();
    for (size_t i = 0; i < client_span_count; ++i) {
        out->add_client_spans();
    }
    const Span* src = this;
    TracingSpan* dest = out;
    for (int i = 0; src; src = src->_next_client) {
        if (src != this) {
            // client spans should be reversed.
            dest = out->mutable_client_spans(client_span_count - i - 1);
            ++i;
        }
        dest->set_trace_id(src->trace_id());
        dest->set_span_id(src->span_id());
        dest->set_parent_span_id(src->parent_span_id());
        dest->set_log_id(src->log_id());
        dest->set_base_cid(src->base_cid().value);
        dest->set_ending_cid(src->ending_cid().value);
        dest->set_remote_ip(butil::ip2int(src->remote_side().ip));
        dest->set_remote_port(src->remote_side().port);
        dest->set_type(src->type());
        dest->set_async(src->async());
        dest->set_protocol(src->protocol());
        dest->set_error_code(src->error_code());
        dest->set_request_size(src->request_size());
        dest->set_response_size(src->response_size());
        dest->set_received_real_us(src->received_real_us());
        dest->set_start_parse_real_us(src->start_parse_real_us());
        dest->set_start_callback_real_us(src->start_callback_real_us());
        dest->set_start_send_real_us(src->start_send_real_us());
        dest->set_sent_real_us(src->sent_real_us());
        dest->set_span_name(src->full_method_name());
        size_t anno_count = src->_annotation_list.size();
        for (size_t i = 0; i < anno_count; ++i) {
            dest->add_annotations();
            dest->mutable_annotations(i)->set_realtime_us(src->_annotation_list[i].realtime_us);
            dest->mutable_annotations(i)->set_content(src->_annotation_list[i].content);
        }
    }
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

bvar::CollectorSpeedLimit g_span_sl = BVAR_COLLECTOR_SPEED_LIMIT_INITIALIZER;
static bvar::DisplaySamplingRatio s_display_sampling_ratio(
    "rpcz_sampling_ratio", &g_span_sl);

struct SpanEarlier {
    bool operator()(bvar::Collected* c1, bvar::Collected* c2) const {
        return GetStartRealTimeUs((Span*)c1) < GetStartRealTimeUs((Span*)c2);
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
static SpanPreprocessor g_span_prep;

bvar::CollectorSpeedLimit* Span::speed_limit() {
    return &g_span_sl;
}

bvar::CollectorPreprocessor* Span::preprocessor() {
    return &g_span_prep;
}

void Span::Submit(Span* span, int64_t cpuwide_time_us) {
    if (span->local_parent() == NULL) {
        span->submit(cpuwide_time_us);
    }
}

void Span::dump_and_destroy(size_t /*round*/) {
    TracingSpan tracing_span;
    Copy2TracingSpan(&tracing_span);
    butil::get_leaky_singleton<SpanExporterManager>()->DumpSpan(&tracing_span);
    destroy();
}


} // namespace brpc
