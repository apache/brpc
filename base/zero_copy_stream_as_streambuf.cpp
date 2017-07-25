// iobuf - A non-continuous zero-copied buffer
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Nov 22 13:57:56 CST 2012

#include "base/macros.h"
#include "base/zero_copy_stream_as_streambuf.h"

namespace base {

BAIDU_CASSERT(sizeof(std::streambuf::char_type) == sizeof(char),
              only_support_char);

int ZeroCopyStreamAsStreamBuf::overflow(int ch) {
    if (ch == std::streambuf::traits_type::eof()) {
        return ch;
    }
    void* block = NULL;
    int size = 0;
    if (_zero_copy_stream->Next(&block, &size)) {
        setp((char*)block, (char*)block + size);
        // if size == 1, this function will call overflow again.
        return sputc(ch);
    } else {
        setp(NULL, NULL);
        return std::streambuf::traits_type::eof();
    }
}

int ZeroCopyStreamAsStreamBuf::sync() {
    // data are already in IOBuf.
    return 0;
}

ZeroCopyStreamAsStreamBuf::~ZeroCopyStreamAsStreamBuf() {
    shrink();
}

void ZeroCopyStreamAsStreamBuf::shrink() {
    if (pbase() != NULL) {
        _zero_copy_stream->BackUp(epptr() - pptr());
        setp(NULL, NULL);
    }
}

std::streampos ZeroCopyStreamAsStreamBuf::seekoff(
    std::streamoff off,
    std::ios_base::seekdir way,
    std::ios_base::openmode which) {
    if (off == 0 && way == std::ios_base::cur) {
        return _zero_copy_stream->ByteCount() - (epptr() - pptr());
    }
    return (std::streampos)(std::streamoff)-1;
}


}  // namespace base
