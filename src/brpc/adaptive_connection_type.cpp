// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Sep  4 12:28:07 CST 2015

#include "base/logging.h"
#include "brpc/adaptive_connection_type.h"


namespace brpc {

inline bool CompareStringPieceWithoutCase(
        const base::StringPiece& s1, const char* s2) {
    if (strlen(s2) != s1.size()) {
        return false;
    }
    return strncasecmp(s1.data(), s2, s1.size()) == 0;
}

ConnectionType StringToConnectionType(const base::StringPiece& type,
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

void AdaptiveConnectionType::operator=(const base::StringPiece& name) {
    if (name.empty()) {
        _type = CONNECTION_TYPE_UNKNOWN;
        _error = false;
    } else {
        _type = StringToConnectionType(name);
        _error = (_type == CONNECTION_TYPE_UNKNOWN);
    }
}

} // namespace brpc

