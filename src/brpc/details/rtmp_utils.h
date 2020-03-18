// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_DETAILS_RTMP_UTILS_H
#define BRPC_DETAILS_RTMP_UTILS_H

#include <stdint.h>  // int32_t
#include <stddef.h>  // size_t

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
