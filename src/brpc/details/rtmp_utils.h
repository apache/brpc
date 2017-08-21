// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep 18 11:19:44 CST 2016

#ifndef BRPC_DETAILS_RTMP_UTILS_H
#define BRPC_DETAILS_RTMP_UTILS_H

#include <stdint.h>
#include <cstddef>

namespace brpc {

// Extract bits one by one from the bytes.
class BitStream {
public:
    BitStream(const void* data, size_t len)
        : _data(data), _data_end((const char*)data + len), _shift(7) {}
    ~BitStream() {}
    
    // True if no bits any more.
    bool empty() { return _data == _data_end; }

    // Read one bit from the data. empty() must be checked before calling
    // this function, otherwise the behavior is undefined.
    int8_t read_bit() {
        const int8_t* p = (const int8_t*)_data;
        int8_t result = (*p >> _shift) & 0x1;
        if (_shift == 0) {
            _shift = 7;
            _data = p + 1;
        } else {
            --_shift;
        }
        return result;
    }

private:
    const void* _data;
    const void* const _data_end;
    int _shift;
};

int avc_nalu_read_uev(BitStream* stream, int32_t* v);
int avc_nalu_read_bit(BitStream* stream, int8_t* v);

} // namespace brpc


#endif  // BRPC_DETAILS_RTMP_UTILS_H
