// Copyright (c) 2012 Baidu, Inc.
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

// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Nov 22 13:57:56 CST 2012

#ifndef BUTIL_BINARY_PRINTER_H
#define BUTIL_BINARY_PRINTER_H

#include "butil/strings/string_piece.h"

namespace butil {
class IOBuf;

// Print binary content within max length.
// The printing format is optimized for humans and may change in future.

class ToPrintable {
public:
    static const size_t DEFAULT_MAX_LENGTH = 64;
    
    ToPrintable(const IOBuf& b, size_t max_length = DEFAULT_MAX_LENGTH)
        : _iobuf(&b), _max_length(max_length) {}

    ToPrintable(const StringPiece& str, size_t max_length = DEFAULT_MAX_LENGTH)
        : _iobuf(NULL), _str(str), _max_length(max_length) {}

    ToPrintable(const void* data, size_t n, size_t max_length = DEFAULT_MAX_LENGTH)
        : _iobuf(NULL), _str((const char*)data, n), _max_length(max_length) {}
    
    void Print(std::ostream& os) const;

private:
    const IOBuf* _iobuf;
    StringPiece _str;
    size_t _max_length;
};

// Keep old name for compatibility.
typedef ToPrintable PrintedAsBinary;

inline std::ostream& operator<<(std::ostream& os, const ToPrintable& p) {
    p.Print(os);
    return os;
}

// Convert binary data to a printable string.
std::string ToPrintableString(const IOBuf& data,
                              size_t max_length = ToPrintable::DEFAULT_MAX_LENGTH);
std::string ToPrintableString(const StringPiece& data,
                              size_t max_length = ToPrintable::DEFAULT_MAX_LENGTH);
std::string ToPrintableString(const void* data, size_t n,
                              size_t max_length = ToPrintable::DEFAULT_MAX_LENGTH);

} // namespace butil

#endif  // BUTIL_BINARY_PRINTER_H
