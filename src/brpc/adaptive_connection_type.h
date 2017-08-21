// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Sep  4 12:28:07 CST 2015

#ifndef BRPC_ADAPTIVE_CONNECTION_TYPE_H
#define BRPC_ADAPTIVE_CONNECTION_TYPE_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "base/strings/string_piece.h"
#include "brpc/options.pb.h"

namespace brpc {

// Convert a case-insensitive string to corresponding ConnectionType
// Possible options are: short, pooled, single
// Returns: CONNECTION_TYPE_UNKNOWN on error.
ConnectionType StringToConnectionType(const base::StringPiece& type,
                                      bool print_log_on_unknown);
inline ConnectionType StringToConnectionType(const base::StringPiece& type)
{ return StringToConnectionType(type, true); }

// Convert a ConnectionType to a c-style string.
const char* ConnectionTypeToString(ConnectionType);

// Assignable by both ConnectionType and names.
class AdaptiveConnectionType {
public:    
    AdaptiveConnectionType() : _type(CONNECTION_TYPE_UNKNOWN), _error(false) {}
    AdaptiveConnectionType(ConnectionType type) : _type(type), _error(false) {}
    ~AdaptiveConnectionType() {}

    void operator=(ConnectionType type) {
        _type = type;
        _error = false;
    }
    void operator=(const base::StringPiece& name);

    operator ConnectionType() const { return _type; }
    const char* name() const { return ConnectionTypeToString(_type); }
    bool has_error() const { return _error; }
    
private:
    ConnectionType _type;
    // Since this structure occupies 8 bytes in 64-bit machines anyway,
    // we add a field to mark if last operator=(name) failed so that
    // channel can print a error log before re-selecting a valid
    // ConnectionType for user.
    bool _error;
};

} // namespace brpc


#endif  // BRPC_ADAPTIVE_CONNECTION_TYPE_H
