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

#include <inttypes.h>
#include "butil/iobuf.h"
#include "butil/binary_printer.h"

namespace butil {

static char s_binary_char_map[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F'
};

template <typename Appender>
class BinaryCharPrinter {
public:
    static const size_t BUF_SIZE = 127;
    explicit BinaryCharPrinter(Appender* a) : _n(0), _appender(a) {}
    ~BinaryCharPrinter() { Flush(); }
    void PushChar(unsigned char c);
    void Flush();
private:
    uint32_t _n;
    Appender* _appender;
    char _buf[BUF_SIZE];
};

template <typename Appender>
void BinaryCharPrinter<Appender>::Flush() {
    if (_n > 0) {
        _appender->Append(_buf, _n);
        _n = 0;
    }
}

template <typename Appender>
void BinaryCharPrinter<Appender>::PushChar(unsigned char c) {
    if (_n > BUF_SIZE - 3) {
        _appender->Append(_buf, _n);
        _n = 0;
    }
    if (c >= 32 && c <= 126) { // displayable ascii characters
        if (c != '\\') {
            _buf[_n++] = c;
        } else {
            _buf[_n++] = '\\';
            _buf[_n++] = '\\';
        }
    } else {
        _buf[_n++] = '\\';
        switch (c) {
        case '\b': _buf[_n++] = 'b'; break;
        case '\t': _buf[_n++] = 't'; break;
        case '\n': _buf[_n++] = 'n'; break;
        case '\r': _buf[_n++] = 'r'; break;
        default: 
            _buf[_n++] = s_binary_char_map[c >> 4];
            _buf[_n++] = s_binary_char_map[c & 0xF];
            break;
        }
    }
}

class OStreamAppender {
public:
    OStreamAppender(std::ostream& os) : _os(&os) {}
    void Append(const char* b, size_t n) { _os->write(b, n); }
private:
    std::ostream* _os;
};

class StringAppender {
public:
    StringAppender(std::string* str) : _str(str) {}
    void Append(const char* b, size_t n) { _str->append(b, n); }
private:
    std::string* _str;
};

template <typename Appender>
static void PrintIOBuf(Appender* appender, const IOBuf& b, size_t max_length) {
    BinaryCharPrinter<Appender> printer(appender);
    const size_t n = b.backing_block_num();
    size_t nw = 0;
    for (size_t i = 0; i < n; ++i) {
        StringPiece blk = b.backing_block(i);
        for (size_t j = 0; j < blk.size(); ++j) {
            if (nw >= max_length) {
                printer.Flush();
                char buf[48];
                int len = snprintf(buf, sizeof(buf), "...<skipping %" PRIu64 " bytes>",
                         b.size() - nw);
                appender->Append(buf, len);
                return;
            }
            ++nw;
            printer.PushChar(blk[j]);
        }
    }
}

template <typename Appender>
static void PrintString(Appender* appender, const StringPiece& s, size_t max_length) {
    BinaryCharPrinter<Appender> printer(appender);
    for (size_t i = 0; i < s.size(); ++i) {
        if (i >= max_length) {
            printer.Flush();
            char buf[48];
            int len = snprintf(buf, sizeof(buf), "...<skipping %" PRIu64 " bytes>",
                               s.size() - i);
            appender->Append(buf, len);
            return;
        }
        printer.PushChar(s[i]);
    }
}

void ToPrintable::Print(std::ostream& os) const {
    OStreamAppender appender(os);
    if (_iobuf) {
        PrintIOBuf(&appender, *_iobuf, _max_length);
    } else if (!_str.empty()) {
        PrintString(&appender, _str, _max_length);
    }
}

std::string ToPrintableString(const IOBuf& data, size_t max_length) {
    std::string result;
    StringAppender appender(&result);
    PrintIOBuf(&appender, data, max_length);
    return result;
}

std::string ToPrintableString(const StringPiece& data, size_t max_length) {
    std::string result;
    StringAppender appender(&result);
    PrintString(&appender, data, max_length);
    return result;
}

std::string ToPrintableString(const void* data, size_t n, size_t max_length) {
    std::string result;
    StringAppender appender(&result);
    PrintString(&appender, StringPiece((const char*)data, n), max_length);
    return result;
}

} // namespace butil
