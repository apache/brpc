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

#include "brpc/details/json_util.h"

namespace brpc {
namespace policy {

// Keep the naming-service entry points while sharing the depth policy and
// parser with other JSON consumers.
const int kMaxNamingServiceJsonDepth = brpc::kMaxJsonDepth;

inline int MaxJsonDepth(const std::string& text) {
    return brpc::MaxJsonDepth(text);
}

inline bool ParseNamingServiceJson(const std::string& text,
                                   BUTIL_RAPIDJSON_NAMESPACE::Document* doc) {
    return brpc::ParseJsonWithDepthLimit(text, doc);
}

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_NAMING_SERVICE_JSON_H
