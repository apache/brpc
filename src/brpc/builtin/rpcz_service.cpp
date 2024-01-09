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


#include <ostream>
#include <iomanip>
#include <gflags/gflags.h>
#include "butil/string_printf.h"
#include "butil/string_splitter.h"
#include "butil/macros.h"
#include "butil/time.h"
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/builtin/common.h"
#include "brpc/server.h"
#include "brpc/errno.pb.h"
#include "brpc/span.h"
#include "brpc/builtin/rpcz_service.h"


namespace brpc {

// Defined in span.cpp
bool has_span_db();

DEFINE_bool(enable_rpcz, false, "Turn on rpcz");
BRPC_VALIDATE_GFLAG(enable_rpcz, PassValidate);

DEFINE_bool(rpcz_hex_log_id, false, "Show log_id in hexadecimal");
BRPC_VALIDATE_GFLAG(rpcz_hex_log_id, PassValidate);

struct Hex {
    explicit Hex(uint64_t val) : _val(val) {}
    uint64_t _val;
};

inline std::ostream& operator<<(std::ostream& os, const Hex& h) {
    const std::ios::fmtflags old_flags =
        os.setf(std::ios::hex, std::ios::basefield);
    os << h._val;
    os.flags(old_flags);
    return os;
}

void RpczService::enable(::google::protobuf::RpcController* cntl_base,
                         const ::brpc::RpczRequest*,
                         ::brpc::RpczResponse*,
                         ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    const bool use_html = UseHTML(cntl->http_request());
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");
    if (!GFLAGS_NS::SetCommandLineOption("enable_rpcz", "true").empty()) {
        if (use_html) {
            // Redirect to /rpcz
            cntl->response_attachment().append(
                "<!DOCTYPE html><html><head>"
                "<meta http-equiv=\"refresh\" content=\"0; url=/rpcz\" />"
                "</head><body>");
        }
        cntl->response_attachment().append("rpcz is enabled");
    } else {
        if (use_html) {
            cntl->response_attachment().append("<!DOCTYPE html><html><body>");
        }
        cntl->response_attachment().append("Fail to set --enable_rpcz");
    }
    if (use_html) {
        cntl->response_attachment().append("</body></html>");
    }
}

void RpczService::disable(::google::protobuf::RpcController* cntl_base,
                          const ::brpc::RpczRequest*,
                          ::brpc::RpczResponse*,
                          ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    const bool use_html = UseHTML(cntl->http_request());
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");
    if (!GFLAGS_NS::SetCommandLineOption("enable_rpcz", "false").empty()) {
        if (use_html) {
            // Redirect to /rpcz
            cntl->response_attachment().append(
                "<!DOCTYPE html><html><head>"
                "<meta http-equiv=\"refresh\" content=\"0; url=/rpcz\" />"
                "</head><body>");
        }
        cntl->response_attachment().append("rpcz is disabled");
    } else {
        if (use_html) {
            cntl->response_attachment().append("<!DOCTYPE html><html><body>");
        }
        cntl->response_attachment().append("Fail to set --enable_rpcz");
    }
    if (use_html) {
        cntl->response_attachment().append("</body></html>");
    }
}

void RpczService::hex_log_id(::google::protobuf::RpcController* cntl_base,
                             const ::brpc::RpczRequest*,
                             ::brpc::RpczResponse*,
                             ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    FLAGS_rpcz_hex_log_id = true;
    cntl->response_attachment().append("log_id is hexadecimal");
}

void RpczService::dec_log_id(::google::protobuf::RpcController* cntl_base,
                             const ::brpc::RpczRequest*,
                             ::brpc::RpczResponse*,
                             ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    FLAGS_rpcz_hex_log_id = false;
    cntl->response_attachment().append("log_id is decimal");
}

void RpczService::stats(::google::protobuf::RpcController* cntl_base,
                        const ::brpc::RpczRequest*,
                        ::brpc::RpczResponse*,
                        ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");

    if (!FLAGS_enable_rpcz && !has_span_db()) {
        cntl->response_attachment().append(
            "rpcz is not enabled yet. You can turn on/off rpcz by accessing "
            "/rpcz/enable and /rpcz/disable respectively");
        return;
    }

    butil::IOBufBuilder os;
    DescribeSpanDB(os);
    os.move_to(cntl->response_attachment());
}

inline void PrintRealTime(std::ostream& os, int64_t tm) {
    char buf[16];
    const time_t tm_s = tm / 1000000L;
    struct tm lt;
    strftime(buf, sizeof(buf), "%H:%M:%S.", localtime_r(&tm_s, &lt));
    const char old_fill = os.fill('0');
    os << buf << std::setw(6) << tm % 1000000L;
    os.fill(old_fill);
}

static void PrintElapse(std::ostream& os, int64_t cur_time,
                        int64_t* last_time) {
    const int64_t elp = cur_time - *last_time;
    *last_time = cur_time;
    if (elp < 0) {
        os << std::fixed << std::setw(11) << std::setprecision(6)
           << elp / 1000000.0;
    } else {
        if (elp >= 1000000L) {
            os << std::setw(4) << elp / 1000000L << '.';
        } else {
            os << "    .";
        }
        os << std::setw(6) << (elp % 1000000L);
    }
}

static void PrintAnnotations(
    std::ostream& os, int64_t cur_time, int64_t* last_time,
    SpanInfoExtractor** extractors, int num_extr) {
    int64_t anno_time;
    std::string a;
    // TODO: Going through all extractors is not strictly correct because 
    // later extractors may have earlier annotations.
    for (int i = 0; i < num_extr; ++i) {
        while (extractors[i]->PopAnnotation(cur_time, &anno_time, &a)) {
            PrintRealTime(os, anno_time);
            PrintElapse(os, anno_time, last_time);
            os << ' ' << WebEscape(a);
            if (a.empty() || butil::back_char(a) != '\n') {
                os << '\n';
            }
        }
    }
}

static bool PrintAnnotationsAndRealTimeSpan(
    std::ostream& os, int64_t cur_time, int64_t* last_time,
    SpanInfoExtractor** extr, int num_extr) {
    if (cur_time == 0) {
        // the field was not set.
        return false;
    }
    PrintAnnotations(os, cur_time, last_time, extr, num_extr);
    PrintRealTime(os, cur_time);
    PrintElapse(os, cur_time, last_time);
    return true;
}

inline int64_t GetStartRealTime(const RpczSpan& span) {
    return span.type() == SPAN_TYPE_SERVER ?
        span.received_real_us() : span.start_send_real_us();
}

struct CompareByStartRealTime {
    bool operator()(const RpczSpan& s1, const RpczSpan& s2) const {
        return GetStartRealTime(s1) < GetStartRealTime(s2);
    }
};

static butil::ip_t loopback_ip = butil::IP_ANY;
static int ALLOW_UNUSED init_loopback_ip_dummy = butil::str2ip("127.0.0.1", &loopback_ip);

static void PrintClientSpan(
    std::ostream& os, const RpczSpan& span,
    int64_t* last_time, SpanInfoExtractor* server_extr, bool use_html) {
    SpanInfoExtractor client_extr(span.info().c_str());
    int num_extr = 0;
    SpanInfoExtractor* extr[2];
    if (server_extr) {
        extr[num_extr++] = server_extr;
    }
    extr[num_extr++] = &client_extr;
    // start_send_us is always set for client spans.
    CHECK(PrintAnnotationsAndRealTimeSpan(os, span.start_send_real_us(),
                                          last_time, extr, num_extr));
    const Protocol* protocol = FindProtocol(span.protocol());
    const char* protocol_name = (protocol ? protocol->name : "Unknown");
    const butil::EndPoint remote_side(butil::int2ip(span.remote_ip()), span.remote_port());
    butil::EndPoint abs_remote_side = remote_side;
    if (abs_remote_side.ip == loopback_ip) {
        abs_remote_side.ip = butil::my_ip();
    }
    os << " Requesting " << WebEscape(span.full_method_name()) << '@' << remote_side
       << ' ' << protocol_name << ' ' << LOG_ID_STR << '=';
    if (FLAGS_rpcz_hex_log_id) {
        os << Hex(span.log_id());
    } else {
        os << span.log_id();
    }
    os << " call_id=" << span.base_cid()
       << ' ' << TRACE_ID_STR << '=' << Hex(span.trace_id())
       << ' ' << SPAN_ID_STR << '=';
    if (use_html) {
        os << "<a href=\"http://" << abs_remote_side
           << "/rpcz?" << TRACE_ID_STR << '=' << Hex(span.trace_id())
           << '&' << SPAN_ID_STR << '=' << Hex(span.span_id()) << "\">";
    }
    os << Hex(span.span_id());
    if (use_html) {
        os << "</a>";
    }
    os << std::endl;

    if (PrintAnnotationsAndRealTimeSpan(os, span.sent_real_us(),
                                        last_time, extr, num_extr)) {
        os << " Requested(" << span.request_size() << ") [1]" << std::endl;
    }
    if (PrintAnnotationsAndRealTimeSpan(os, span.received_real_us(),
                                        last_time, extr, num_extr)) {
        os << " Received response(" << span.response_size() << ")";
        if (span.base_cid() != 0 && span.ending_cid() != 0) {
            int64_t ver = span.ending_cid() - span.base_cid();
            if (ver >= 1) {
                os << " of request[" << ver << "]";
            } else {
                os << " of invalid version=" << ver;
            }
        }
        os << std::endl;
    }

    if (PrintAnnotationsAndRealTimeSpan(os, span.start_parse_real_us(),
                                        last_time, extr, num_extr)) {
        os << " Processing the response in a new bthread" << std::endl;
    }

    if (PrintAnnotationsAndRealTimeSpan(
            os, span.start_callback_real_us(),
            last_time, extr, num_extr)) {
        os << (span.async() ? " Enter user's done" : " Back to user's callsite") << std::endl;
    }

    PrintAnnotations(os, std::numeric_limits<int64_t>::max(),
                     last_time, extr, num_extr);
}

static void PrintClientSpan(std::ostream& os,const RpczSpan& span,
                            bool use_html) {
    int64_t last_time = span.start_send_real_us();
    PrintClientSpan(os, span, &last_time, NULL, use_html);
}


static void PrintServerSpan(std::ostream& os, const RpczSpan& span,
                            bool use_html) {
    SpanInfoExtractor server_extr(span.info().c_str());
    SpanInfoExtractor* extr[1] = { &server_extr };
    int64_t last_time = span.received_real_us();
    const butil::EndPoint remote_side(
        butil::int2ip(span.remote_ip()), span.remote_port());
    PrintRealDateTime(os, last_time);
    const Protocol* protocol = FindProtocol(span.protocol());
    const char* protocol_name = (protocol ? protocol->name : "Unknown");
    os << " Received request(" << span.request_size() << ") from "
       << remote_side << ' ' << protocol_name << ' ' << LOG_ID_STR << '=';
    if (FLAGS_rpcz_hex_log_id) {
        os << Hex(span.log_id());
    } else {
        os << span.log_id();
    }
    // TODO: We can't hyperlink parent_span now because there's no generic
    // way to get the port of upstream server yet.
    os << ' ' << TRACE_ID_STR << '=' << Hex(span.trace_id())
       << ' ' << SPAN_ID_STR << '=' << Hex(span.span_id());
    if (span.parent_span_id() != 0) {
        os << " parent_span=" << Hex(span.parent_span_id());
    }
    os << std::endl;
    if (PrintAnnotationsAndRealTimeSpan(
            os, span.start_parse_real_us(),
            &last_time, extr, ARRAY_SIZE(extr))) {
        os << " Processing the request in a new bthread" << std::endl;
    }

    bool entered_user_method = false;
    if (PrintAnnotationsAndRealTimeSpan(
            os, span.start_callback_real_us(),
            &last_time, extr, ARRAY_SIZE(extr))) {
        entered_user_method = true;
        os << " Enter " << WebEscape(span.full_method_name()) << std::endl;
    }

    const int nclient = span.client_spans_size();
    for (int i = 0; i < nclient; ++i) {
        PrintClientSpan(os, span.client_spans(i), &last_time,
                        &server_extr, use_html);
    }

    if (PrintAnnotationsAndRealTimeSpan(
            os, span.start_send_real_us(),
            &last_time, extr, ARRAY_SIZE(extr))) {
        if (entered_user_method) {
            os << " Leave " << WebEscape(span.full_method_name()) << std::endl;
        } else {
            os << " Responding" << std::endl;
        }
    }
    
    if (PrintAnnotationsAndRealTimeSpan(
            os, span.sent_real_us(),
            &last_time, extr, ARRAY_SIZE(extr))) {
        os << " Responded(" << span.response_size() << ')' << std::endl;
    }

    PrintAnnotations(os, std::numeric_limits<int64_t>::max(),
                     &last_time, extr, ARRAY_SIZE(extr));
}

class RpczSpanFilter : public SpanFilter {
public:
    RpczSpanFilter()
        : _min_latency(std::numeric_limits<int64_t>::min())
        , _min_request_size(std::numeric_limits<int>::min())
        , _min_response_size(std::numeric_limits<int>::min())
        , _log_id(0)
        , _check_log_id(false)
        , _check_error_code(false)
        , _error_code(0)
    {}

    void CheckLatency(int64_t min_latency) { _min_latency = min_latency; }

    void CheckRequest(int64_t min_request_size) {
        _min_request_size = min_request_size;
    }

    void CheckResponse(int64_t min_response_size) {
        _min_response_size = min_response_size;
    }

    void CheckLogId(uint64_t log_id) {
        _check_log_id = true;
        _log_id = log_id;
    }

    void CheckErrorCode(int error_code) {
        _check_error_code = true;
        _error_code = error_code;
    }
    
    bool Keep(const BriefSpan& span) {
        return span.latency_us() >= _min_latency &&
            span.request_size() >= _min_request_size &&
            span.response_size() >= _min_response_size &&
            (!_check_log_id || span.log_id() == _log_id) &&
            (!_check_error_code || span.error_code() == _error_code);
    }

private:
    int64_t _min_latency;
    int _min_request_size;
    int _min_response_size;
    uint64_t _log_id;
    bool _check_log_id;
    bool _check_error_code;
    int _error_code;
};

static int64_t ParseDateTime(const std::string& time_str) {
    struct tm timeinfo;
    int64_t microseconds = 999999;
    char* endptr = strptime(time_str.c_str(), "%Y/%m/%d-%H:%M:%S", &timeinfo);
    if (endptr == NULL) {
        time_t now;
        time(&now);
        if (localtime_r(&now, &timeinfo) == NULL) {
            return -1;
        }
        endptr = strptime(time_str.c_str(), "%H:%M:%S", &timeinfo);
        if (endptr == NULL) {
            return -1;
        }
    } 
    if (*endptr == '.') {
        char* endptr2;
        microseconds = strtol(endptr + 1, &endptr2, 10);
        if (*endptr2 != '\0') {
            microseconds = 999999;
        }
    }
    return timelocal(&timeinfo) * 1000000L + microseconds;
}

static bool ParseUint64(const std::string* str, uint64_t* val) {
    if (NULL == str) {
        return false;
    }
    const char* p = str->c_str();
    char* endptr = NULL;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        *val = strtoull(p + 2, &endptr, 16);
        return (*endptr == '\0');
    }
    *val = strtoull(p, &endptr, 10);
    if (*endptr == '\0') {
        return true;
    }
    if ((*endptr >= 'a' && *endptr <= 'f') ||
        (*endptr >= 'A' && *endptr <= 'F')) {
        *val = strtoull(p, &endptr, 16);
        return (*endptr == '\0');
    }
    return false;
}

void RpczService::default_method(::google::protobuf::RpcController* cntl_base,
                                 const ::brpc::RpczRequest*,
                                 ::brpc::RpczResponse*,
                                 ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    uint64_t trace_id = 0;
    const bool use_html = UseHTML(cntl->http_request());
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");

    butil::IOBufBuilder os;
    if (use_html) {
        os << "<!DOCTYPE html><html><head>\n"
           << "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/jquery_min\"></script>\n"
           << TabsHead()
           << "</head><body>";
        cntl->server()->PrintTabsBody(os, "rpcz");
    }

    if (!FLAGS_enable_rpcz && !has_span_db()) {
        if (use_html) {
            os << "<input type='button' "
                "onclick='location.href=\"/rpcz/enable\";' value='enable' />"
                " rpcz to track recent RPC calls with small overhead, "
                "you can turn it off at any time.";
        } else {
            os << "rpcz is not enabled yet. You can turn on/off rpcz by accessing "
                "/rpcz/enable and /rpcz/disable respectively.";
        }
        os.move_to(cntl->response_attachment());
        return;
    }
    butil::EndPoint my_addr(butil::my_ip(),
                           cntl->server()->listen_address().port);
    
    const std::string* trace_id_str =
        cntl->http_request().uri().GetQuery(TRACE_ID_STR);
    if (trace_id_str) {
        char* endptr;
        trace_id = strtoull(trace_id_str->c_str(), &endptr, 16);
        if (*endptr != '\0') {
            trace_id = 0;
        }
    }
    if (trace_id != 0) {  // Point search
        uint64_t span_id = 0;
        const std::string* span_id_str =
            cntl->http_request().uri().GetQuery(SPAN_ID_STR);
        if (span_id_str) {
            char* endptr;
            span_id = strtoull(span_id_str->c_str(), &endptr, 16);
            if (*endptr != '\0') {
                span_id = 0;
            }
        }
        std::deque<RpczSpan> spans;
        if (span_id == 0) {
            FindSpans(trace_id, &spans);
            std::sort(spans.begin(), spans.end(), CompareByStartRealTime());
        } else {
            spans.resize(1);
            if (FindSpan(trace_id, span_id, &spans[0]) != 0) {
                spans.clear();
            }
        }
        if (spans.empty()) {
            os << "Fail to find any spans"
               << (use_html ? "</body></html>" : "");
            os.move_to(cntl->response_attachment());
            return;
        }
        if (use_html) {
            os << "<pre>\n";
        }
        for (size_t i = 0; i < spans.size(); ++i) {
            RpczSpan& span = spans[i];
            if (span.type() == SPAN_TYPE_SERVER) {
                PrintServerSpan(os, span, use_html);
            } else {
                PrintClientSpan(os, span, use_html);
            }
            os << std::endl;
        }
        if (use_html) {
            os << "</pre></body></html>";
        }
        os.move_to(cntl->response_attachment());
        return;
    } else {
        const std::string* time_str =
            cntl->http_request().uri().GetQuery(TIME_STR);
        int64_t start_tm;
        if (time_str == NULL) {
            start_tm = butil::gettimeofday_us();
        } else {
            start_tm = ParseDateTime(*time_str);
            if (start_tm < 0) {
                os << "Invalid " << TIME_STR << "=`" << time_str << '\''
                   << (use_html ? "</body></html>" : "");
                os.move_to(cntl->response_attachment());
                return;
            }
        }
        int max_count = 100;
        const std::string* max_scan_str =
            cntl->http_request().uri().GetQuery(MAX_SCAN_STR);
        if (max_scan_str) {
            char* endptr;
            int max_count2 = strtol(max_scan_str->c_str(), &endptr, 10);
            if (*endptr == '\0') {
                max_count = max_count2;
            }
        }

        // Set up SpanFilter.
        RpczSpanFilter filter;
        const std::string* min_latency_str = 
            cntl->http_request().uri().GetQuery(MIN_LATENCY_STR);
        if (min_latency_str) {
            char* endptr;
            filter.CheckLatency(strtoll(min_latency_str->c_str(), &endptr, 10));
        }
        const std::string* min_reqsize_str = 
            cntl->http_request().uri().GetQuery(MIN_REQUEST_SIZE_STR);
        if (min_reqsize_str) {
            char* endptr;
            filter.CheckRequest(strtol(min_reqsize_str->c_str(), &endptr, 10));
        }
        const std::string* min_respsize_str = 
            cntl->http_request().uri().GetQuery(MIN_RESPONSE_SIZE_STR);
        if (min_respsize_str) {
            char* endptr;
            filter.CheckResponse(strtol(min_respsize_str->c_str(), &endptr, 10));
        }
        uint64_t log_id = 0;
        if (ParseUint64(cntl->http_request().uri().GetQuery(LOG_ID_STR),
                        &log_id)) {
            filter.CheckLogId(log_id);
        }
        const std::string* error_code_str =
            cntl->http_request().uri().GetQuery(ERROR_CODE_STR);
        if (error_code_str) {
            char* endptr;
            filter.CheckErrorCode(strtol(error_code_str->c_str(), &endptr, 10));
        }
        
        max_count = std::max(std::min(max_count, 10000), 1);
        std::deque<BriefSpan> spans;
        ListSpans(start_tm, max_count, &spans, &filter);
        if (spans.empty()) {
            os << "Fail to find matched spans"
               << (use_html ? "</body></html>" : "");
            os.move_to(cntl->response_attachment());
            return;
        }
        if (use_html) {
            const char* action = (FLAGS_enable_rpcz ? "disable" : "enable");
            os << "<div><input type='button' onclick='location.href=\"/rpcz/"
               << action << "\";' value='" << action << "' /></div>" "<pre>\n";
        }
        for (size_t i = 0; i < spans.size(); ++i) {
            BriefSpan& span = spans[i];
            int64_t last_time = span.start_real_us();
            PrintRealDateTime(os, last_time);
            PrintElapse(os, span.latency_us() + last_time, &last_time);
            os << ' ' << (span.type() == SPAN_TYPE_SERVER ? 'S' : 'C')
               << ' ' << TRACE_ID_STR << '=';
            if (use_html) {
                os << "<a href=\"/rpcz?" << TRACE_ID_STR
                   << '=' << Hex(span.trace_id()) << "\">";
            }
            os << Hex(span.trace_id());
            if (use_html) {
                os << "</a>";
            }

            os << ' ' << SPAN_ID_STR << '=';
            if (use_html) {
                os << "<a href=\"/rpcz?" << TRACE_ID_STR << '=' 
                   << Hex(span.trace_id())
                   << '&' << SPAN_ID_STR << '=' << Hex(span.span_id()) << "\">";
            }
            os << Hex(span.span_id());
            if (use_html) {
                os << "</a>";
            }
            os << ' ' << LOG_ID_STR << '=';
            if (FLAGS_rpcz_hex_log_id) {
                os << Hex(span.log_id());
            } else {
                os << span.log_id();
            }
            os << ' ' << WebEscape(span.full_method_name()) << '(' << span.request_size()
               << ")=" << span.response_size();
            
            if (span.error_code() == 0) {
                os << " [OK]";
            } else {
                os << " [" << berror(span.error_code()) << "] ";
            }
            os << std::endl;
        }
        if (use_html) {
            os << "</pre></body></html>";
        }
        os.move_to(cntl->response_attachment());
    }
}

void RpczService::GetTabInfo(TabInfoList* info_list) const {
    TabInfo* info = info_list->add();
    info->path = "/rpcz";
    info->tab_name = "rpcz";
}

} // namespace brpc
