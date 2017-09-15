// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BRPC_ADAPTIVE_PROTOCOL_TYPE_H
#define BRPC_ADAPTIVE_PROTOCOL_TYPE_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "butil/strings/string_piece.h"
#include "brpc/options.pb.h"

namespace brpc {

// NOTE: impl. are in brpc/protocol.cpp

// Convert a case-insensitive string to corresponding ProtocolType which is
// defined in src/brpc/options.proto
// Returns: PROTOCOL_UNKNOWN on error.
ProtocolType StringToProtocolType(const butil::StringPiece& type,
                                  bool print_log_on_unknown);
inline ProtocolType StringToProtocolType(const butil::StringPiece& type)
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
    void operator=(const butil::StringPiece& name)
    { _type = StringToProtocolType(name); };

    operator ProtocolType() const { return _type; }
    const char* name() const { return ProtocolTypeToString(_type); }
    
private:
    ProtocolType _type;
};

} // namespace brpc


#endif  // BRPC_ADAPTIVE_PROTOCOL_TYPE_H
