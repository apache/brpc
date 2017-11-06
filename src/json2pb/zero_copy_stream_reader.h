// Copyright (c) 2014 Baidu, Inc.
// File: iobuf_read_stream.h
// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2014/10/29 15:01:09

#ifndef  BRPC_JSON2PB_ZERO_COPY_STREAM_READER_H
#define  BRPC_JSON2PB_ZERO_COPY_STREAM_READER_H

#include <google/protobuf/io/zero_copy_stream.h> // ZeroCopyInputStream

namespace json2pb {

class ZeroCopyStreamReader {
public:
    typedef char Ch;
    ZeroCopyStreamReader(google::protobuf::io::ZeroCopyInputStream *stream)
            : _data(NULL), _data_size(0), _nread(0), _stream(stream) {
    }
    //Take a charactor and return its address.
    const char* PeekAddr() { 
        if (!ReadBlockTail()) {
            return _data;
        }
        while (_stream->Next((const void **)&_data, &_data_size)) {
            if (!ReadBlockTail()) {
                return _data;
            }
        }
        return NULL;
    }
    const char* TakeWithAddr() {
        const char* c = PeekAddr();
        if (c) {
            ++_nread;
            --_data_size;
            return _data++;
        }
        return NULL;
    }
    char Take() {
        const char* c = PeekAddr();
        if (c) {
            ++_nread;
            --_data_size;
            ++_data;
            return *c;
        }
        return '\0';
    }
    
    char Peek() {
        const char* c = PeekAddr();
        return (c ? *c : '\0');
    }
    //Tell whether read the end of this block.
    bool ReadBlockTail() {
        return !_data_size;
    }
    size_t Tell() { return _nread; }
    void Put(char) {}
    void Flush() {}
    char *PutBegin() { return NULL; }
    size_t PutEnd(char *) { return 0; }
private:
    const char *_data;
    int _data_size;
    size_t _nread;
    google::protobuf::io::ZeroCopyInputStream *_stream;
};

} // namespace json2pb

#endif  //BRPC_JSON2PB_ZERO_COPY_STREAM_READER_H
