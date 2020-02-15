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


#include "butil/logging.h"
#include "brpc/adaptive_connection_type.h"


namespace brpc {

inline bool CompareStringPieceWithoutCase(
        const butil::StringPiece& s1, const char* s2) {
    if (strlen(s2) != s1.size()) {
        return false;
    }
    return strncasecmp(s1.data(), s2, s1.size()) == 0;
}

ConnectionType StringToConnectionType(const butil::StringPiece& type,
                                      bool print_log_on_unknown) {
    if (CompareStringPieceWithoutCase(type, "single")) {
        return CONNECTION_TYPE_SINGLE;
    } else if (CompareStringPieceWithoutCase(type, "pooled")) {
        return CONNECTION_TYPE_POOLED;
    } else if (CompareStringPieceWithoutCase(type, "short")) {
        return CONNECTION_TYPE_SHORT;
    }
    LOG_IF(ERROR, print_log_on_unknown && !type.empty())
        << "Unknown connection_type `" << type
        << "', supported types: single pooled short";
    return CONNECTION_TYPE_UNKNOWN;
}

const char* ConnectionTypeToString(ConnectionType type) {
    switch (type) {
    case CONNECTION_TYPE_UNKNOWN:
        return "unknown";
    case CONNECTION_TYPE_SINGLE:
        return "single";
    case CONNECTION_TYPE_POOLED:
        return "pooled";
    case CONNECTION_TYPE_SHORT:
        return "short";
    }
    return "unknown";
}

void AdaptiveConnectionType::operator=(const butil::StringPiece& name) {
    if (name.empty()) {
        _type = CONNECTION_TYPE_UNKNOWN;
        _error = false;
    } else {
        _type = StringToConnectionType(name);
        _error = (_type == CONNECTION_TYPE_UNKNOWN);
    }
}

} // namespace brpc
