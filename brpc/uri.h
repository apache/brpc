// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date 2014/10/24 19:01:57

#ifndef  BRPC_URI_H
#define  BRPC_URI_H

#include <string>                   // std::string
#include "base/containers/flat_map.h"
#include "base/status.h"
#include "base/string_splitter.h"

// To baidu-rpc developers: This is a class exposed to end-user. DON'T put impl.
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
    typedef base::FlatMap<std::string, std::string> QueryMap;
    typedef QueryMap::const_iterator QueryIterator;

    // You can copy a URI.
    URI();
    ~URI();

    // Exchange internal fields with another URI.
    void Swap(URI &rhs);

    // Reset internal fields as if they're just default-constructed.
    void Clear();
    
    // Overwrite fields that are specified in `http_url'.
    int SetHttpURL(base::StringPiece http_url);
    // syntactic sugar of SetHttpURL
    void operator=(const base::StringPiece& http_url) { SetHttpURL(http_url); }

    // Status of previous SetHttpURL or opreator=.
    const base::Status& status() const { return _st; }

    // Getters of fields. Empty string if the field is not set.
    const std::string& schema() const { return _schema; }
    const std::string& host() const { return _host; }
    const std::string& path() const { return _path; }
    const std::string& user_info() const { return _user_info; }
    const std::string& fragment() const { return _fragment; }
    // NOTE: This method is not thread-safe because it may re-generate the
    // query-string if SetQuery()/RemoveQuery() were successfully called.
    const std::string& query() const;

    // Get port. Non-negative if port is set, -1 otherwise
    int port() const { return _port; }

    // Overwrite path only.
    void set_path(const std::string& path) { _path = path; }
    // Clear the host.
    void clear_host() { _host.clear(); }
    
    // Get the value of a CASE-SENSITIVE key.
    // Returns pointer to the value, NULL when the key does not exist.
    const std::string* GetQuery(const char* key) const
    { return _query_map.seek(key); }
    const std::string* GetQuery(const std::string& key) const
    { return _query_map.seek(key); }

    // Add key/value pair. Override existing value.
    void SetQuery(const std::string& key, const std::string& value);

    // Remove value associated with `key'.
    // Returns 1 on removed, 0 otherwise.
    size_t RemoveQuery(const char* key);
    size_t RemoveQuery(const std::string& key);

    // Get query iterators which are invalidated after calling SetQuery()
    // or SetHttpURL().
    QueryIterator QueryBegin() const { return _query_map.begin(); }
    QueryIterator QueryEnd() const { return _query_map.end(); }
    // #queries
    size_t QueryCount() const { return _query_map.size(); }

    // Print this URI. If `always_show_host' is false, host:port will
    // be hidden is there's no authority part.
    void Print(std::ostream& os, bool always_show_host) const;

private:
friend class HttpMessage;
    void GenerateQueryString(std::string* query) const;

    base::Status                            _st;
    int                                     _port;
    mutable bool                            _query_was_modified;
    std::string                             _host;
    std::string                             _path;
    std::string                             _user_info;
    std::string                             _fragment;
    std::string                             _schema;
    mutable std::string                     _query;
    QueryMap _query_map;
};

inline void URI::SetQuery(const std::string& key, const std::string& value) {
    if (!_query_map.initialized()) {
        _query_map.init(QUERY_MAP_INITIAL_BUCKET);
    }
    _query_map[key] = value;
    _query_was_modified = true;
}

inline size_t URI::RemoveQuery(const char* key) {
    if (_query_map.erase(key)) {
        _query_was_modified = true;
        return 1;
    }
    return 0;
}

inline size_t URI::RemoveQuery(const std::string& key) {
    if (_query_map.erase(key)) {
        _query_was_modified = true;
        return 1;
    }
    return 0;
}

inline const std::string& URI::query() const {
    if (_query_was_modified) {
        _query_was_modified = false;
        GenerateQueryString(&_query);
    }
    return _query;
}

inline std::ostream& operator<<(std::ostream& os, const URI& uri) {
    uri.Print(os, true/*always show host*/);
    return os;
}

// A class to split query in the format of "key1=value1&key2=value2"
// This class can also handle some exceptional cases, such as
// consecutive ampersand, only one equality, only one key and so on.
class QuerySplitter {
public:
    QuerySplitter(const char* str_begin, const char* str_end)
        : _sp(str_begin, str_end, '&')
        , _is_split(false) {
    }

    QuerySplitter(const char* str_begin)
        : _sp(str_begin, '&')
        , _is_split(false) {
    }

    QuerySplitter(const base::StringPiece &sp)
        : _sp(sp.begin(), sp.end(), '&')
        , _is_split(false) {
    }

    const base::StringPiece& key() {
        if (!_is_split) {
            split();
        }
        return _key;
    }

    const base::StringPiece& value() {
        if (!_is_split) {
            split();
        }
        return _value;
    }

    // Get the current value of key and value 
    // in the format of "key=value"
    base::StringPiece key_and_value(){
        return base::StringPiece(_sp.field(), _sp.length());
    }

    // Move splitter forward.
    QuerySplitter& operator++() {
        ++_sp;
        _is_split = false;
        return *this;
    }

    QuerySplitter operator++(int) {
        QuerySplitter tmp = *this;
        operator++();
        return tmp;
    }

    operator const void*() const { return _sp; }

private:
    void split();

private:
    base::StringSplitter _sp;
    base::StringPiece _key;
    base::StringPiece _value;
    bool _is_split;
};

// A class to remove some specific keys in a query string, 
// when removal is over, call modified_query() to get modified
// query.
class QueryRemover {
public:
    QueryRemover(const std::string& str);

    const base::StringPiece& key() { return _qs.key();}
    const base::StringPiece& value() { return _qs.value(); }
    base::StringPiece key_and_value() { return _qs.key_and_value(); }

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
    QuerySplitter _qs;
    const std::string _query;
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
                  const base::StringPiece& key,
                  const base::StringPiece& value);

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
