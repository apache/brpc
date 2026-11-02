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


#ifndef BRPC_DETAILS_JSON_UTIL_H
#define BRPC_DETAILS_JSON_UTIL_H

#include <string.h>
#include <string>
#include "butil/third_party/rapidjson/document.h"

namespace brpc {

// Include the root object/array in the depth count. A root object can contain
// at most 99 nested containers below it. Bound depth before constructing the
// DOM, whose destruction is recursive even when parsing is iterative.
const int kMaxJsonDepth = 100;

// Count the maximum nesting depth of '{'/'[' in `text', ignoring brackets
// inside string literals.
inline int MaxJsonDepth(const std::string& text) {
    int depth = 0;
    int max_depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max_depth) {
                max_depth = depth;
                if (max_depth > kMaxJsonDepth) {
                    // Too deep, no need to scan further.
                    return max_depth;
                }
            }
        } else if (c == '}' || c == ']') {
            // Don't go negative on malformed text with more closing than
            // opening brackets.
            if (depth > 0) {
                --depth;
            }
        }
    }
    return max_depth;
}

// Reject raw NULs and excessive nesting before building the DOM. Depth and
// NUL rejection leave *doc unchanged; other parse errors may modify it.
inline bool ParseJsonWithDepthLimit(
    const std::string& text, BUTIL_RAPIDJSON_NAMESPACE::Document* doc) {
    if (memchr(text.data(), '\0', text.size()) != nullptr ||
        MaxJsonDepth(text) > kMaxJsonDepth) {
        return false;
    }
    doc->Parse<BUTIL_RAPIDJSON_NAMESPACE::kParseIterativeFlag>(text.c_str());
    return !doc->HasParseError();
}

}  // namespace brpc

#endif  // BRPC_DETAILS_JSON_UTIL_H
