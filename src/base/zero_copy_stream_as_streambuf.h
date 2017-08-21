// iobuf - A non-continuous zero-copied buffer
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Nov 22 13:57:56 CST 2012

#ifndef BRPC_BASE_ZERO_COPY_STREAM_AS_STREAMBUF_H
#define BRPC_BASE_ZERO_COPY_STREAM_AS_STREAMBUF_H

#include <streambuf>
#include <google/protobuf/io/zero_copy_stream.h>

namespace base {

// Wrap a ZeroCopyOutputStream into std::streambuf. Notice that before 
// destruction or shrink(), BackUp() of the stream are not called. In another
// word, if the stream is wrapped from IOBuf, the IOBuf may be larger than 
// appended data.
class ZeroCopyStreamAsStreamBuf : public std::streambuf {
public:
    ZeroCopyStreamAsStreamBuf(google::protobuf::io::ZeroCopyOutputStream* stream)
        : _zero_copy_stream(stream) {}
    virtual ~ZeroCopyStreamAsStreamBuf();

    // BackUp() unused bytes. Automatically called in destructor.
    void shrink();
    
protected:
    virtual int overflow(int ch);
    virtual int sync();
    std::streampos seekoff(std::streamoff off,
                           std::ios_base::seekdir way,
                           std::ios_base::openmode which);

private:
    google::protobuf::io::ZeroCopyOutputStream* _zero_copy_stream;
};

}  // namespace base

#endif  // BRPC_BASE_ZERO_COPY_STREAM_AS_STREAMBUF_H
