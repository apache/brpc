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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)
//          Ge,Jun (gejun@baidu.com)

#ifndef  BRPC_URI_H
#define  BRPC_URI_H

#include <string>                   // std::string
#include "butil/containers/flat_map.h"
#include "butil/status.h"
#include "butil/string_splitter.h"

// To brpc developers: This is a class exposed to end-user. DON'T put impl.
// details in this header, use opaque pointers instead.


namespace brpc {

// The class for URI scheme : http://en.wikipedia.org/wiki/URI_scheme
//
//  foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=narwhal#nose
//  \_/   \_______________/ \_________/ \__/            \___/ \_/ \______________________/ \__/
//   |           |               |       |                |    |            |                |
//   |       userinfo           host    port              |    |          query          fragment
//   |    \________________________________/\_____________|____|/ \__/        \__/
// schema                 |                          |    |    |    |          |
//                    authority                      |    |    |    |          |
//                                                 path   |    |    interpretable as keys
//                                                        |    |
//        \_______________________________________________|____|/       \____/     \_____/
//                             |                          |    |          |           |
//                     hierarchical part                  |    |    interpretable as values
//                                                        |    |
//                                   interpretable as filename |
//                                                             |
//                                                             |
//                                               interpretable as extension
class URI {
public:
    static const size_t QUERY_MAP_INITIAL_BUCKET = 16;
    typedef butil::FlatMap<std::string, std::string> QueryMap;
    typedef QueryMap::const_iterator QueryIterator;

    // You can copy a URI.
    URI();
    ~URI();

    // Exchange internal fields with another URI.
    void Swap(URI &rhs);

    // Reset internal fields as if they're just default-constructed.
    void Clear(); 

    // Decompose `url' and set into corresponding fields.
    // heading and trailing spaces are allowed and skipped.
    // Returns 0 on success, -1 otherwise and status() is set.
    int SetHttpURL(const char* url);
    int SetHttpURL(const std::string& url) { return SetHttpURL(url.c_str()); }
    // syntactic sugar of SetHttpURL
    void operator=(const char* url) { SetHttpURL(url); }
    void operator=(const std::string& url) { SetHttpURL(url); }

    // Status of previous SetHttpURL or opreator=.
    const butil::Status& status() const { return _st; }

    // Sub fields. Empty string if the field is not set.
    const std::string& schema() const { return _schema; } // scheme in http2
    const std::string& host() const { return _host; }
    int port() const { return _port; } // -1 on unset.
    const std::string& path() const { return _path; }
    const std::string& user_info() const { return _user_info; }
    const std::string& fragment() const { return _fragment; }
    // NOTE: This method is not thread-safe because it may re-generate the
    // query-string if SetQuery()/RemoveQuery() were successfully called.
    const std::string& query() const;
    // Put path?query#fragment into `h2_path'
    void GenerateH2Path(std::string* h2_path) const;

    // Overwrite parts of the URL.
    // NOTE: The input MUST be guaranteed to be valid.
    void set_schema(const std::string& schema) { _schema = schema; }
    void set_path(const std::string& path) { _path = path; }
    void set_host(const std::string& host) { _host = host; }
    void set_port(int port) { _port = port; }
    void SetHostAndPort(const std::string& host_and_optional_port);
    // Set path/query/fragment with the input in form of "path?query#fragment"
    void SetH2Path(const char* h2_path);
    void SetH2Path(const std::string& path) { SetH2Path(path.c_str()); }
    
    // Get the value of a CASE-SENSITIVE key.
    // Returns pointer to the value, NULL when the key does not exist.
    const std::string* GetQuery(const char* key) const
    { return get_query_map().seek(key); }
    const std::string* GetQuery(const std::string& key) const
    { return get_query_map().seek(key); }

    // Add key/value pair. Override existing value.
    void SetQuery(const std::string& key, const std::string& value);

    // Remove value associated with `key'.
    // Returns 1 on removed, 0 otherwise.
    size_t RemoveQuery(const char* key);
    size_t RemoveQuery(const std::string& key);

    // Get query iterators which are invalidated after calling SetQuery()
    // or SetHttpURL().
    QueryIterator QueryBegin() const { return get_query_map().begin(); }
    QueryIterator QueryEnd() const { return get_query_map().end(); }
    // #queries
    size_t QueryCount() const { return get_query_map().size(); }

    // Print this URI to the ostream.
    // PrintWithoutHost only prints components including and after path.
    void PrintWithoutHost(std::ostream& os) const;
    void Print(std::ostream& os) const;

private:
friend class HttpMessage;

    void InitializeQueryMap() const;

    QueryMap& get_query_map() const {
        if (!_initialized_query_map) {
            InitializeQueryMap();
        }
        return _query_map;
    }

    // Iterate _query_map and append all queries to `query'
    void AppendQueryString(std::string* query, bool append_question_mark) const;

    butil::Status                            _st;
    int                                     _port;
    mutable bool                            _query_was_modified;
    mutable bool                            _initialized_query_map;
    std::string                             _host;
    std::string                             _path;
    std::string                             _user_info;
    std::string                             _fragment;
    std::string                             _schema;
    mutable std::string                     _query;
    mutable QueryMap _query_map;
};

// Parse host/port/schema from `url' if the corresponding parameter is not NULL.
// Returns 0 on success, -1 otherwise.
int ParseURL(const char* url, std::string* schema, std::string* host, int* port);

inline void URI::SetQuery(const std::string& key, const std::string& value) {
    get_query_map()[key] = value;
    _query_was_modified = true;
}

inline size_t URI::RemoveQuery(const char* key) {
    if (get_query_map().erase(key)) {
        _query_was_modified = true;
        return 1;
    }
    return 0;
}

inline size_t URI::RemoveQuery(const std::string& key) {
    if (get_query_map().erase(key)) {
        _query_was_modified = true;
        return 1;
    }
    return 0;
}

inline const std::string& URI::query() const {
    if (_initialized_query_map && _query_was_modified) {
        _query_was_modified = false;
        _query.clear();
        AppendQueryString(&_query, false);
    }
    return _query;
}

inline std::ostream& operator<<(std::ostream& os, const URI& uri) {
    uri.Print(os);
    return os;
}

// Split query in the format of "key1=value1&key2&key3=value3"
class QuerySplitter : public butil::KeyValuePairsSplitter {
public:
    inline QuerySplitter(const char* str_begin, const char* str_end)
        : KeyValuePairsSplitter(str_begin, str_end, '&', '=')
    {}

    inline QuerySplitter(const char* str_begin)
        : KeyValuePairsSplitter(str_begin, '&', '=')
    {}

    inline QuerySplitter(const butil::StringPiece &sp)
        : KeyValuePairsSplitter(sp, '&', '=')
    {}
};

// A class to remove some specific keys in a query string, 
// when removal is over, call modified_query() to get modified
// query.
class QueryRemover {
public:
    QueryRemover(const std::string* str);

    butil::StringPiece key() { return _qs.key();}
    butil::StringPiece value() { return _qs.value(); }
    butil::StringPiece key_and_value() { return _qs.key_and_value(); }

    // Move splitter forward.
    QueryRemover& operator++();
    QueryRemover operator++(int);

    operator const void*() const { return _qs; }

    // After this function is called, current query will be removed from
    // modified_query(), calling this function more than once has no effect.
    void remove_current_key_and_value();
 
    // Return the modified query string
    std::string modified_query();

private:
    const std::string* _query;
    QuerySplitter _qs;
    std::string _modified_query;
    size_t _iterated_len;
    bool _removed_current_key_value;
    bool _ever_removed;
};

// This function can append key and value to *query_string
// in consideration of all possible format of *query_string
// for example:
// "" -> "key=value"
// "key1=value1" -> "key1=value1&key=value"
// "/some/path?" -> "/some/path?key=value"
// "/some/path?key1=value1" -> "/some/path?key1=value1&key=value"
void append_query(std::string *query_string,
                  const butil::StringPiece& key,
                  const butil::StringPiece& value);

} // namespace brpc


#if __cplusplus < 201103L  // < C++11
#include <algorithm>  // std::swap until C++11
#else
#include <utility>  // std::swap since C++11
#endif  // __cplusplus < 201103L

namespace std {
template<>
inline void swap(brpc::URI &lhs, brpc::URI &rhs) {
    lhs.Swap(rhs);
}
}  // namespace std

#endif  //BRPC_URI_H
