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

// Date: Sun Dec  4 14:57:27 CST 2016

#ifndef BUTIL_CASE_IGNORED_FLAT_MAP_H
#define BUTIL_CASE_IGNORED_FLAT_MAP_H

#include "butil/containers/flat_map.h"

namespace butil {

// Using ascii_tolower instead of ::tolower shortens 150ns in
// FlatMapTest.perf_small_string_map (with -O2 added, -O0 by default)
// note: using char caused crashes on ubuntu 20.04 aarch64 (VM on apple M1)
inline char ascii_tolower(int/*note*/ c) {
    extern const signed char* const g_tolower_map;
    return g_tolower_map[c];
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
    // NOTE: No overload for butil::StringPiece. It needs strncasecmp
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
class CaseIgnoredFlatMap : public butil::FlatMap<
    std::string, T, CaseIgnoredHasher, CaseIgnoredEqual> {};

class CaseIgnoredFlatSet : public butil::FlatSet<
    std::string, CaseIgnoredHasher, CaseIgnoredEqual> {};

} // namespace butil

#endif  // BUTIL_CASE_IGNORED_FLAT_MAP_H
