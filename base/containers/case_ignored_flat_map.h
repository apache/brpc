// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Dec  4 14:57:27 CST 2016

#ifndef BASE_CASE_IGNORED_FLAT_MAP_H
#define BASE_CASE_IGNORED_FLAT_MAP_H

#include "base/containers/flat_map.h"

namespace base {

// NOTE: Using ascii_tolower instead of ::tolower shortens 150ns in
// FlatMapTest.perf_small_string_map (with -O2 added, -O0 by default)
inline char ascii_tolower(char c) {
    extern const char* const g_tolower_map;
    return g_tolower_map[(int)c];
}

struct CaseIgnoredHasher {
    size_t operator()(const std::string& s) const {
        std::size_t result = 0;                                               
        for (std::string::const_iterator i = s.begin(); i != s.end(); ++i) {
            result = result * 101 + ascii_tolower(*i);
        }
        return result;
    }
    size_t operator()(const char* s) const {
        std::size_t result = 0;                                               
        for (; *s; ++s) {
            result = result * 101 + ascii_tolower(*s);
        }
        return result;
    }
};

struct CaseIgnoredEqual {
    // NOTE: No overload for base::StringPiece. It needs strncasecmp
    // which is much slower than strcasecmp in micro-benchmarking. As a
    // result, methods in HttpHeader does not accept StringPiece as well.
    bool operator()(const std::string& s1, const std::string& s2) const {
        return s1.size() == s2.size() &&
            strcasecmp(s1.c_str(), s2.c_str()) == 0;
    }
    bool operator()(const std::string& s1, const char* s2) const
    { return strcasecmp(s1.c_str(), s2) == 0; }
};

template <typename T>
class CaseIgnoredFlatMap : public base::FlatMap<
    std::string, T, CaseIgnoredHasher, CaseIgnoredEqual> {};

class CaseIgnoredFlatSet : public base::FlatMap<
    std::string, CaseIgnoredHasher, CaseIgnoredEqual> {};

} // namespace base

#endif  // BASE_CASE_IGNORED_FLAT_MAP_H
