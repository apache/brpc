// Copyright (c) 2016 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_AMF_INL_H
#define BRPC_AMF_INL_H

void* fast_memcpy(void *__restrict dest, const void *__restrict src, size_t n);


namespace brpc {

inline size_t AMFInputStream::cutn(void* out, size_t n) {
    const size_t saved_n = n;
    do {
        if (_size >= (int64_t)n) {
            memcpy(out, _data, n);
            _data = (const char*)_data + n;
            _size -= n;
            _popped_bytes += saved_n;
            return saved_n;
        }
        if (_size) {
            memcpy(out, _data, _size);
            out = (char*)out + _size;
            n -= _size;
        }
    } while (_zc_stream->Next(&_data, &_size));
    _data = NULL;
    _size = 0;
    _popped_bytes += saved_n - n;
    return saved_n - n;
}

inline bool AMFInputStream::check_emptiness() {
    return _size == 0 && !_zc_stream->Next(&_data, &_size);
}

inline size_t AMFInputStream::cut_u8(uint8_t* val) {
    if (_size >= 1) {
        *val = *(uint8_t*)_data;
        _data = (const char*)_data + 1;
        _size -= 1;
        _popped_bytes += 1;
        return 1;
    }
    return cutn(val, 1);
}

inline size_t AMFInputStream::cut_u16(uint16_t* val) {
    if (_size >= 2) {
        const uint16_t netval = *(uint16_t*)_data;
        *val = butil::NetToHost16(netval);
        _data = (const char*)_data + 2;
        _size -= 2;
        _popped_bytes += 2;
        return 2;
    }
    uint16_t netval = 0;
    const size_t ret = cutn(&netval, 2);
    *val = butil::NetToHost16(netval);
    return ret;
}

inline size_t AMFInputStream::cut_u32(uint32_t* val) {
    if (_size >= 4) {
        const uint32_t netval = *(uint32_t*)_data;
        *val = butil::NetToHost32(netval);
        _data = (const char*)_data + 4;
        _size -= 4;
        _popped_bytes += 4;
        return 4;
    }
    uint32_t netval = 0;
    const size_t ret = cutn(&netval, 4);
    *val = butil::NetToHost32(netval);
    return ret;
}

inline size_t AMFInputStream::cut_u64(uint64_t* val) {
    if (_size >= 8) {
        const uint64_t netval = *(uint64_t*)_data;
        *val = butil::NetToHost64(netval);
        _data = (const char*)_data + 8;
        _size -= 8;
        _popped_bytes += 8;
        return 8;
    }
    uint64_t netval = 0;
    const size_t ret = cutn(&netval, 8);
    *val = butil::NetToHost64(netval);
    return ret;
}

inline void AMFOutputStream::done() {
    if (_good && _size) {
        _zc_stream->BackUp(_size);
        _size = 0;
    }
}

inline void AMFOutputStream::putn(const void* data, int n) {
    const int saved_n = n;
    do {
        if (n <= _size) {
            fast_memcpy(_data, data, n);
            _data = (char*)_data + n;
            _size -= n;
            _pushed_bytes += saved_n;
            return;
        }
        fast_memcpy(_data, data, _size);
        data = (const char*)data + _size;
        n -= _size;
    } while (_zc_stream->Next(&_data, &_size));
    _data = NULL;
    _size = 0;
    _pushed_bytes += (saved_n - n);
    if (n) {
        set_bad();
    }
}

inline void AMFOutputStream::put_u8(uint8_t val) {
    do {
        if (_size > 0) {
            *(uint8_t*)_data = val;
            _data = (char*)_data + 1;
            --_size;
            ++_pushed_bytes;
            return;
        }
    } while (_zc_stream->Next(&_data, &_size));
    _data = NULL;
    _size = 0;
    set_bad();
}

inline void AMFOutputStream::put_u16(uint16_t val) {
    uint16_t netval = butil::HostToNet16(val);
    return putn(&netval, sizeof(netval));
}

inline void AMFOutputStream::put_u32(uint32_t val) {
    uint32_t netval = butil::HostToNet32(val);
    return putn(&netval, sizeof(netval));
}

inline void AMFOutputStream::put_u64(uint64_t val) {
    uint64_t netval = butil::HostToNet64(val);
    return putn(&netval, sizeof(netval));
}

} // namespace brpc


#endif  // BRPC_AMF_INL_H
