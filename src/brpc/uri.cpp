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


#include <ctype.h>                         // isalnum

#include <unordered_set>

#include "brpc/log.h"
#include "brpc/details/http_parser.h"      // http_parser_parse_url
#include "brpc/uri.h"                      // URI


namespace brpc {

URI::URI() 
    : _port(-1)
    , _query_was_modified(false)
    , _initialized_query_map(false)
{}

URI::~URI() {
}

void URI::Clear() {
    _st.reset();
    _port = -1;
    _query_was_modified = false;
    _initialized_query_map = false;
    _host.clear();
    _path.clear();
    _user_info.clear();
    _fragment.clear();
    _scheme.clear();
    _query.clear();
    _query_map.clear();
}

void URI::Swap(URI &rhs) {
    _st.swap(rhs._st);
    std::swap(_port, rhs._port);
    std::swap(_query_was_modified, rhs._query_was_modified);
    std::swap(_initialized_query_map, rhs._initialized_query_map);
    _host.swap(rhs._host);
    _path.swap(rhs._path);
    _user_info.swap(rhs._user_info);
    _fragment.swap(rhs._fragment);
    _scheme.swap(rhs._scheme);
    _query.swap(rhs._query);
    _query_map.swap(rhs._query_map);
}

// Parse queries, which is case-sensitive
static void ParseQueries(URI::QueryMap& query_map, const std::string &query) {
    query_map.clear();
    if (query.empty()) {
        return;
    }
    for (QuerySplitter sp(query.c_str()); sp; ++sp) {
        if (!sp.key().empty()) {
            if (!query_map.initialized()) {
                query_map.init(URI::QUERY_MAP_INITIAL_BUCKET);
            }
            std::string key(sp.key().data(), sp.key().size());
            std::string value(sp.value().data(), sp.value().size());
            query_map[key] = value;
        }
    }
}

inline const char* SplitHostAndPort(const char* host_begin,
                                    const char* host_end,
                                    int* port) {
    uint64_t port_raw = 0;
    uint64_t multiply = 1;
    for (const char* q = host_end - 1; q > host_begin; --q) {
        if (*q >= '0' && *q <= '9') {
            port_raw += (*q - '0') * multiply;
            multiply *= 10;
        } else if (*q == ':') {
            *port = static_cast<int>(port_raw);
            return q;
        } else {
            break;
        }
    }
    *port = -1;
    return host_end;
}

// valid characters in URL
// https://datatracker.ietf.org/doc/html/rfc3986#section-2.1
// https://datatracker.ietf.org/doc/html/rfc3986#section-2.3
// https://datatracker.ietf.org/doc/html/rfc3986#section-2.4
// space is not allowed by rfc3986, but allowed by brpc
static bool is_valid_char(char c) {
    static const std::unordered_set<char> other_valid_char = {
        ':', '/', '?', '#', '[', ']', '@', '!', '$', '&',
        '\'', '(', ')', '*', '+', ',', ';', '=', '-', '.',
        '_', '~', '%', ' '
    };

    return (isalnum(c) || other_valid_char.count(c));
}

static bool is_all_spaces(const char* p) {
    for (; *p == ' '; ++p) {}
    return !*p;
}

const char URI_PARSE_CONTINUE = 0;
const char URI_PARSE_CHECK = 1;
const char URI_PARSE_BREAK = 2;
static const char g_url_parsing_fast_action_map_raw[] = {
    0/*-128*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-118*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-108*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-98*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-88*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-78*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-68*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-58*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-48*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-38*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-28*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-18*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*-8*/, 0, 0, 0, 0, 0, 0, 0, URI_PARSE_BREAK/*\0*/, 0,
    0/*2*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*12*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*22*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    URI_PARSE_CHECK/* */, 0, 0, URI_PARSE_BREAK/*#*/, 0, 0, 0, 0, 0, 0,
    0/*42*/, 0, 0, 0, 0, URI_PARSE_BREAK/*/*/, 0, 0, 0, 0,
    0/*52*/, 0, 0, 0, 0, 0, URI_PARSE_CHECK/*:*/, 0, 0, 0,
    0/*62*/, URI_PARSE_BREAK/*?*/, URI_PARSE_CHECK/*@*/, 0, 0, 0, 0, 0, 0, 0,
    0/*72*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*82*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*92*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*102*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*112*/, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0/*122*/, 0, 0, 0, 0, 0
};
static const char* const g_url_parsing_fast_action_map =
    g_url_parsing_fast_action_map_raw + 128;

// This implementation is faster than http_parser_parse_url() and allows
// ignoring of scheme("http://")
int URI::SetHttpURL(const char* url) {
    Clear();
    
    const char* p = url;
    // skip heading blanks
    if (*p == ' ') {
        for (++p; *p == ' '; ++p) {}
    }
    const char* start = p;
    // Find end of host, locate scheme and user_info during the searching
    bool need_scheme = true;
    bool need_user_info = true;
    for (; true; ++p) {
        const char action = g_url_parsing_fast_action_map[(int)*p];
        if (action == URI_PARSE_CONTINUE) {
            continue;
        }
        if (action == URI_PARSE_BREAK) {
            break;
        }
        if (!is_valid_char(*p)) {
            _st.set_error(EINVAL, "invalid character in url");
            return -1;
        } else if (*p == ':') {
            if (p[1] == '/' && p[2] == '/' && need_scheme) {
                need_scheme = false;
                _scheme.assign(start, p - start);
                p += 2;
                start = p + 1;
            }
        } else if (*p == '@') {
            if (need_user_info) {
                need_user_info = false;
                _user_info.assign(start, p - start);
                start = p + 1;
            }
        } else if (*p == ' ') {
            if (!is_all_spaces(p + 1)) {
                _st.set_error(EINVAL, "Invalid space in url");
                return -1;
            }
            break;
        }
    }
    const char* host_end = SplitHostAndPort(start, p, &_port);
    _host.assign(start, host_end - start);
    if (*p == '/') {
        start = p; //slash pointed by p is counted into _path
        ++p;
        for (; *p && *p != '?' && *p != '#'; ++p) {
            if (*p == ' ') {
                if (!is_all_spaces(p + 1)) {
                    _st.set_error(EINVAL, "Invalid space in path");
                    return -1;
                }
                break;
            }
        }
        _path.assign(start, p - start);
    }
    if (*p == '?') {
        start = ++p;
        for (; *p && *p != '#'; ++p) {
            if (*p == ' ') {
                if (!is_all_spaces(p + 1)) {
                    _st.set_error(EINVAL, "Invalid space in query");
                    return -1;
                }
                break;
            }
        }
        _query.assign(start, p - start);
    }
    if (*p == '#') {
        start = ++p;
        for (; *p; ++p) {
            if (*p == ' ') {
                if (!is_all_spaces(p + 1)) {
                    _st.set_error(EINVAL, "Invalid space in fragment");
                    return -1;
                }
                break;
            }
        }
        _fragment.assign(start, p - start);
    }
    return 0;
}

int ParseURL(const char* url,
             std::string* scheme_out, std::string* host_out, int* port_out) {
    const char* p = url;
    // skip heading blanks
    if (*p == ' ') {
        for (++p; *p == ' '; ++p) {}
    }
    const char* start = p;
    // Find end of host, locate scheme and user_info during the searching
    bool need_scheme = true;
    bool need_user_info = true;
    for (; true; ++p) {
        const char action = g_url_parsing_fast_action_map[(int)*p];
        if (action == URI_PARSE_CONTINUE) {
            continue;
        }
        if (action == URI_PARSE_BREAK) {
            break;
        }
        if (*p == ':') {
            if (p[1] == '/' && p[2] == '/' && need_scheme) {
                need_scheme = false;
                if (scheme_out) {
                    scheme_out->assign(start, p - start);
                }
                p += 2;
                start = p + 1;
            }
        } else if (*p == '@') {
            if (need_user_info) {
                need_user_info = false;
                start = p + 1;
            }
        } else if (*p == ' ') {
            if (!is_all_spaces(p + 1)) {
                LOG(ERROR) << "Invalid space in url=`" << url << '\'';
                return -1;
            }
            break;
        }
    }
    int port = -1;
    const char* host_end = SplitHostAndPort(start, p, &port);
    if (host_out) {
        host_out->assign(start, host_end - start);
    }
    if (port_out) {
        *port_out = port;
    }
    return 0;
}

void URI::Print(std::ostream& os) const {
    if (!_host.empty()) {
        if (!_scheme.empty()) {
            os << _scheme << "://";
        } else {
            os << "http://";
        }
        // user_info is passed by Authorization
        os << _host;
        if (_port >= 0) {
            os << ':' << _port;
        }
    }
    PrintWithoutHost(os);
}
    
void URI::PrintWithoutHost(std::ostream& os) const {
    if (_path.empty()) {
        // According to rfc2616#section-5.1.2, the absolute path
        // cannot be empty; if none is present in the original URI, it MUST
        // be given as "/" (the server root).
        os << '/';
    } else {
        os << _path;
    }
    if (_initialized_query_map && _query_was_modified) {
        bool is_first = true;
        for (QueryIterator it = QueryBegin(); it != QueryEnd(); ++it) {
            if (is_first) {
                is_first = false;
                os << '?';
            } else {
                os << '&';
            }
            os << it->first;
            if (!it->second.empty()) {
                os << '=' << it->second;
            }
        }
    } else if (!_query.empty()) {
        os << '?' << _query;
    }
    if (!_fragment.empty()) {
        os << '#' << _fragment;
    }
}

void URI::InitializeQueryMap() const {
    if (!_query_map.initialized()) {
        CHECK_EQ(0, _query_map.init(QUERY_MAP_INITIAL_BUCKET));
    }
    ParseQueries(_query_map, _query);
    _query_was_modified = false;
    _initialized_query_map = true;
}

void URI::AppendQueryString(std::string* query, bool append_question_mark) const {
    if (_query_map.empty()) {
        return;
    }
    if (append_question_mark) {
        query->push_back('?');
    }
    QueryIterator it = QueryBegin();
    query->append(it->first);
    if (!it->second.empty()) {
        query->push_back('=');
        query->append(it->second);
    }
    ++it;
    for (; it != QueryEnd(); ++it) {
        query->push_back('&');
        query->append(it->first);
        if (!it->second.empty()) {
            query->push_back('=');
            query->append(it->second);
        }
    }
}

void URI::GenerateH2Path(std::string* h2_path) const {
    h2_path->reserve(_path.size() + _query.size() + _fragment.size() + 3);
    h2_path->clear();
    if (_path.empty()) {
        h2_path->push_back('/');
    } else {
        h2_path->append(_path);
    }
    if (_initialized_query_map && _query_was_modified) {
        AppendQueryString(h2_path, true);
    } else if (!_query.empty()) {
        h2_path->push_back('?');
        h2_path->append(_query);
    }
    if (!_fragment.empty()) {
        h2_path->push_back('#');
        h2_path->append(_fragment);
    }
}

void URI::SetHostAndPort(const std::string& host) {
    const char* const host_begin = host.c_str();
    const char* host_end =
        SplitHostAndPort(host_begin, host_begin + host.size(), &_port);
    _host.assign(host_begin, host_end - host_begin);
}

void URI::SetH2Path(const char* h2_path) {
    _path.clear();
    _query.clear();
    _fragment.clear();
    _query_was_modified = false;
    _initialized_query_map = false;
    _query_map.clear();

    const char* p = h2_path;
    const char* start = p;
    for (; *p && *p != '?' && *p != '#'; ++p) {}
    _path.assign(start, p - start);
    if (*p == '?') {
        start = ++p;
        for (; *p && *p != '#'; ++p) {}
        _query.assign(start, p - start);
    }
    if (*p == '#') {
        start = ++p;
        for (; *p; ++p) {}
        _fragment.assign(start, p - start);
    }
}

QueryRemover::QueryRemover(const std::string* str)
    : _query(str)
    , _qs(str->data(), str->data() + str->size())
    , _iterated_len(0)
    , _removed_current_key_value(false)
    , _ever_removed(false) {
}

QueryRemover& QueryRemover::operator++() {
    if (!_qs) {
        return *this;
    }
    if (!_ever_removed) {
        _qs.operator++();
        return *this;
    }
    if (!_removed_current_key_value) {
        _modified_query.resize(_iterated_len);
        if (!_modified_query.empty()) {
            _modified_query.push_back('&');
            _iterated_len += 1;
        }
        _modified_query.append(key_and_value().data(), key_and_value().length());
        _iterated_len += key_and_value().length();
    } else {
        _removed_current_key_value = false;
    }
    _qs.operator++();
    return *this;
}

QueryRemover QueryRemover::operator++(int) {
    QueryRemover tmp = *this;
    operator++();
    return tmp;
}

void QueryRemover::remove_current_key_and_value() {
    _removed_current_key_value = true;
    if (!_ever_removed) {
        _ever_removed = true;
        size_t offset = key().data() - _query->data();
        size_t len = offset - ((offset > 0 && (*_query)[offset - 1] == '&')? 1: 0);
        _modified_query.append(_query->data(), len);
        _iterated_len += len;
    }
    return;
}

std::string QueryRemover::modified_query() {
    if (!_ever_removed) {
        return *_query;
    }
    size_t offset = key().data() - _query->data();
    // find out where the remaining string starts
    if (_removed_current_key_value) {
        size_t size = key_and_value().length();
        while (offset + size < _query->size() && (*_query)[offset + size] == '&') {
            // ingore unnecessary '&'
            size += 1;
        }
        offset += size;
    }
    _modified_query.resize(_iterated_len);
    if (offset < _query->size()) {
        if (!_modified_query.empty()) {
            _modified_query.push_back('&');
        }
        _modified_query.append(*_query, offset, std::string::npos);
    }
    return _modified_query;
}

void append_query(std::string *query_string,
                  const butil::StringPiece& key,
                  const butil::StringPiece& value) {
    if (!query_string->empty() && butil::back_char(*query_string) != '?') {
        query_string->push_back('&');
    }
    query_string->append(key.data(), key.size());
    query_string->push_back('=');
    query_string->append(value.data(), value.size());
}

} // namespace brpc
