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


#ifndef BRPC_POLICY_NAMING_SERVICE_JSON_H
#define BRPC_POLICY_NAMING_SERVICE_JSON_H

#include <string>
#include "butil/third_party/rapidjson/document.h"

namespace brpc {
namespace policy {

// Maximum nesting depth of JSON replies accepted from naming-service
// backends. Legit replies of consul/nacos/discovery are only a few levels
// deep, deeper replies are treated as malformed and rejected before
// parsing: rapidjson builds, visits and destroys its DOM recursively, so
// unbounded nesting could exhaust the stack even though the parsing itself
// is iterative (same rationale as json2pb's kParseIterativeFlag plus
// json2pb_max_recursion_depth).
const int kMaxNamingServiceJsonDepth = 100;

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
                if (max_depth > kMaxNamingServiceJsonDepth) {
                    // Too deep, no need to scan further.
                    return max_depth;
                }
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return max_depth;
}

// Parse a JSON reply of a naming service into `doc' iteratively. Returns
// false if `text' is not valid JSON or is nested deeper than
// kMaxNamingServiceJsonDepth, `doc' is left empty in the latter case.
inline bool ParseNamingServiceJson(const std::string& text,
                                   BUTIL_RAPIDJSON_NAMESPACE::Document* doc) {
    if (MaxJsonDepth(text) > kMaxNamingServiceJsonDepth) {
        return false;
    }
    doc->Parse<BUTIL_RAPIDJSON_NAMESPACE::kParseIterativeFlag>(text.c_str());
    return !doc->HasParseError();
}

} // namespace policy
} // namespace brpc

#endif // BRPC_POLICY_NAMING_SERVICE_JSON_H
