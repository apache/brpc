// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date 2014/10/27 10:31:51

#include "brpc/details/http_parser.h"      // http_parser_parse_url
#include "brpc/uri.h"                      // URI


namespace brpc {

URI::URI() 
    : _port(-1)
    , _query_was_modified(false)
{}

URI::~URI() {
}

void URI::Clear() {
    _st.reset();
    _port = -1;
    _host.clear();
    _path.clear();
    _user_info.clear();
    _fragment.clear();
    _schema.clear();
    _query_map.clear();
    _query.clear();
}

// Parse queries, which is case-sensitive
int ParseQueries(URI::QueryMap& query_map, const std::string &query) {
    query_map.clear();
    if (query.empty()) {
        return 0;
    }
    for (QuerySplitter sp(query.data(), query.data() + query.size()); 
        sp; ++sp) {
        if (!query_map.initialized()) {
            query_map.init(URI::QUERY_MAP_INITIAL_BUCKET);
        }
        query_map[sp.key().as_string()] = sp.value().as_string();
    }
        
    return 0;
}

int URI::SetHttpURL(base::StringPiece url) {
    if (url.empty()) { // responses
        return 0;
    }
    url.trim_spaces();
    if (url.empty()) {
        _st.set_error(EINVAL, "url only has spaces");
        return -1;
    }
    std::string new_url;
    const char* p = url.data();
    if (*p != '/' &&
        (strncmp("http", p, 4) != 0 ||
         (strncmp("://", p + 4, 3) != 0 && strncmp("s://", p + 4, 4) != 0))) {
        // Normalize "HOST/PATH" to "http://HOST/PATH"
        new_url.reserve(7 + url.data() + url.size() - p);
        new_url.append("http://");
        new_url.append(p, url.data() + url.size() - p);
        url = new_url;
    }
    struct http_parser_url layout;
    // Have to initialize layout because http_parser may use undefined
    // field_data[UF_HOST] when there's no host field in `url'.
    memset(&layout, 0, sizeof(layout));
    // FIXME:
    const int rc = http_parser_parse_url(url.data(), url.size(), 0, &layout);
    if (rc) {
        _st.set_error(EINVAL, "Fail to parse url=`%.*s'",
                      (int)url.size(), url.data());
        return -1;
    }
    //enum http_parser_url_fields
    //  { UF_SCHEMA           = 0
    //  , UF_HOST             = 1
    //  , UF_PORT             = 2
    //  , UF_PATH             = 3
    //  , UF_QUERY            = 4
    //  , UF_FRAGMENT         = 5
    //  , UF_USERINFO         = 6
    //  , UF_MAX              = 7
    //  };
    std::string *fields[] = { &_schema, &_host, NULL, 
                              &_path, &_query,
                              &_fragment, &_user_info};
    for (int i = 0; i < UF_MAX; ++i) {
        if (fields[i] == NULL) {
            continue;
        }
        if (layout.field_set & (1 << i)) {
            fields[i]->assign(url.data() + layout.field_data[i].off,
                              layout.field_data[i].len);
        } else {
            fields[i]->clear();
        }
    }
    if (layout.field_set & (1 << UF_PORT)) {
        _port = layout.port;
    } else {
        _port = -1;
    }
    if (ParseQueries(_query_map, _query) != 0) {
        _st.set_error(EINVAL, "Fail to parse queries=`%s'", _query.c_str());
        return -1;
    }
    _query_was_modified = false;
    return 0;
}

void URI::Swap(URI &rhs) {
    _st.swap(rhs._st);
    std::swap(_port, rhs._port);
    std::swap(_query_was_modified, rhs._query_was_modified);
    _host.swap(rhs._host);
    _path.swap(rhs._path);
    _user_info.swap(rhs._user_info);
    _fragment.swap(rhs._fragment);
    _schema.swap(rhs._schema);
    _query.swap(rhs._query);
    _query_map.swap(rhs._query_map);
}

void URI::Print(std::ostream& os, bool always_show_host) const {
    if (!_host.empty() && always_show_host) {
        if (!_schema.empty()) {
            os << _schema << "://";
        } else {
            os << "http://";
        }
        // user_info is passed by Authorization
        os << _host;
        if (_port >= 0) {
            os << ':' << _port;
        }
    }
    if (_path.empty()) {
        // According to rfc2616#section-5.1.2, the absolute path
        // cannot be empty; if none is present in the original URI, it MUST
        // be given as "/" (the server root).
        os << '/';
    } else {
        os << _path;
    }
    if (!_query.empty() && !_query_was_modified) {
        os << '?' << _query;
    } else if (QueryCount()) {
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
    }
    if (!_fragment.empty()) {
        os << '#' << _fragment;
    }
}

void URI::GenerateQueryString(std::string* query) const {
    query->clear();
    if (_query_map.empty()) {
        return;
    }
    for (QueryIterator it = QueryBegin(); it != QueryEnd(); ++it) {
        if (!query->empty()) {
            query->push_back('&');
        }
        query->append(it->first);
        if (!it->second.empty()) {
            query->push_back('=');
            query->append(it->second);
        }
    }
}

void QuerySplitter::split() {
    base::StringPiece query_pair(_sp.field(), _sp.length());
    const size_t pos = query_pair.find('=');
    if (pos == base::StringPiece::npos) {
        _key = query_pair;
        _value.clear();
    } else {
        _key= query_pair.substr(0, pos);
        _value = query_pair.substr(pos + 1);
    }
    _is_split = true;
}

QueryRemover::QueryRemover(const std::string& str)
    : _qs(str.data(), str.data() + str.size())
    , _query(str) 
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
        size_t offset = key().data() - _query.data();
        size_t len = offset - ((offset > 0 && _query[offset - 1] == '&')? 1: 0);
        _modified_query.append(_query.data(), len);
        _iterated_len += len;
    }
    return;
}

std::string QueryRemover::modified_query() {
    if (!_ever_removed) {
        return _query;
    }
    size_t offset = key().data() - _query.data();
    // find out where the remaining string starts
    if (_removed_current_key_value) {
        size_t size = key_and_value().length();
        while (offset + size < _query.size() && _query[offset + size] == '&') {
            // ingore unnecessary '&'
            size += 1;
        }
        offset += size;
    }
    _modified_query.resize(_iterated_len);
    if (offset < _query.size()) {
        if (!_modified_query.empty()) {
            _modified_query.push_back('&');
        }
        _modified_query.append(_query, offset, std::string::npos);
    }
    return _modified_query;
}

void append_query(std::string *query_string,
                  const base::StringPiece& key,
                  const base::StringPiece& value) {
    if (!query_string->empty() && base::back_char(*query_string) != '?') {
        query_string->push_back('&');
    }
    query_string->append(key.data(), key.size());
    query_string->push_back('=');
    query_string->append(value.data(), value.size());
}

} // namespace brpc

