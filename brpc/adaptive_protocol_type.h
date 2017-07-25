// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Sep  4 12:28:07 CST 2015

#ifndef BRPC_ADAPTIVE_PROTOCOL_TYPE_H
#define BRPC_ADAPTIVE_PROTOCOL_TYPE_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "base/strings/string_piece.h"
#include "brpc/options.pb.h"

namespace brpc {

// NOTE: impl. are in brpc/protocol.cpp

// Convert a case-insensitive string to corresponding ProtocolType which is
// defined in protocol/brpc/options.proto
// Returns: PROTOCOL_UNKNOWN on error.
ProtocolType StringToProtocolType(const base::StringPiece& type,
                                  bool print_log_on_unknown);
inline ProtocolType StringToProtocolType(const base::StringPiece& type)
{ return StringToProtocolType(type, true); }

// Convert a ProtocolType to a c-style string.
const char* ProtocolTypeToString(ProtocolType);

// Assignable by both ProtocolType and names.
class AdaptiveProtocolType {
public:    
    AdaptiveProtocolType() : _type(PROTOCOL_UNKNOWN) {}
    AdaptiveProtocolType(ProtocolType type) : _type(type) {}
    ~AdaptiveProtocolType() {}

    void operator=(ProtocolType type) { _type = type; }
    void operator=(const base::StringPiece& name)
    { _type = StringToProtocolType(name); };

    operator ProtocolType() const { return _type; }
    const char* name() const { return ProtocolTypeToString(_type); }
    
private:
    ProtocolType _type;
};

} // namespace brpc


#endif  // BRPC_ADAPTIVE_PROTOCOL_TYPE_H
