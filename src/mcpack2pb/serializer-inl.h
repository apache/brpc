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

// mcpack2pb - Make protobuf be front-end of mcpack/compack

// Date: Mon Oct 19 17:17:36 CST 2015

#ifndef MCPACK2PB_MCPACK_SERIALIZER_INL_H
#define MCPACK2PB_MCPACK_SERIALIZER_INL_H

void* fast_memcpy(void *__restrict dest, const void *__restrict src, size_t n);

namespace mcpack2pb {

inline OutputStream::Area::Area(const Area& rhs) 
    : _addr1(rhs._addr1)
    , _addr2(rhs._addr2)
    , _size1(rhs._size1)
    , _size2(rhs._size2)
    , _addional_area(NULL) {

    if (rhs._addional_area) {
        _addional_area = new std::vector<butil::StringPiece>(*rhs._addional_area);
    }
}

inline OutputStream::Area::~Area() {
    if (_addional_area) {
        delete _addional_area;
        _addional_area = NULL;
    }
}

inline OutputStream::Area& OutputStream::Area::operator=(const OutputStream::Area& rhs) {
    if (this == &rhs) {
        return *this;
    }
    this->~Area();
    new (this) Area(rhs);
    return *this;
}

inline void OutputStream::Area::add(void* data, size_t n) {
    if (!data) {
        return;
    }
    if (_addr1 == NULL) {
        _addr1 = data;
        _size1 = n;
    } else if (_addr2 == NULL) {
        _addr2 = data;
        _size2 = n;
    } else {
        if (_addional_area == NULL) {
            _addional_area = new std::vector<butil::StringPiece>;
        }
        _addional_area->push_back(butil::StringPiece((const char*)data, n));
    }
}

inline void OutputStream::Area::assign(const void* data) const {
    if (_addr1) {
        fast_memcpy(_addr1, data, _size1);
        if (!_addr2) {
            return;
        }
        fast_memcpy(_addr2, (const char*)data + _size1, _size2);
        if (!_addional_area) {
            return;
        }
        size_t offset = _size1 + _size2;
        for (std::vector<butil::StringPiece>::const_iterator iter =
                _addional_area->begin(); iter != _addional_area->end(); ++iter) {
            fast_memcpy((void*)iter->data(), (const char*)data + offset, iter->size());
            offset += iter->size();
        }
    }
}

inline void OutputStream::done() {
    if (_good && _size) {
        _zc_stream->BackUp(_size);
        _size = 0;
        _fullsize = 0;
    }
}

inline void OutputStream::append(const void* data, int n) {
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
        if (!_zc_stream->Next(&_data, &_size)) {
            break;
        }
        _fullsize = _size;
    } while (1);
    _data = NULL;
    _size = 0;
    _fullsize = 0;
    _pushed_bytes += (saved_n - n);
    if (n) {
        set_bad();
    }
}

template <typename T>
inline void OutputStream::append_packed_pod(const T& packed_pod) {
    // if (sizeof(T) <= _size) {
    //     *(T*)_data = packed_pod;
    //     _data = (char*)_data + sizeof(T);
    //     _size -= sizeof(T);
    //     _pushed_bytes += sizeof(T);
    //     return;
    // }
    return append(&packed_pod, sizeof(T));
}

inline void OutputStream::push_back(char c) {
    do {
        if (_size > 0) {
            *(char*)_data = c;
            _data = (char*)_data + 1;
            --_size;
            ++_pushed_bytes;
            return;
        }
        if (!_zc_stream->Next(&_data, &_size)) {
            break;
        }
        _fullsize = _size;
    } while (1);
    _data = NULL;
    _size = 0;
    _fullsize = 0;
    set_bad();
}

inline void* OutputStream::skip_continuous(int n) {
    if (_size >= n) {
        void* ret = _data;
        _data = (char*)_data + n;
        _size -= n;
        _pushed_bytes += n;
        return ret;
    }
    return NULL;
}

inline OutputStream::Area OutputStream::reserve(int n) {
    Area area;
    const int saved_n = n;
    do {
        if (n <= _size) {
            area.add(_data, n);
            _data = (char*)_data + n;
            _size -= n;
            _pushed_bytes += saved_n;
            return area;
        }
        area.add(_data, _size);
        n -= _size;
        if (!_zc_stream->Next(&_data, &_size)) {
            break;
        }
        _fullsize = _size;
    } while (1);
    _data = NULL;
    _size = 0;
    _fullsize = 0;
    _pushed_bytes += (saved_n - n);
    if (n) {
        set_bad();
    }
    return area;
}

inline void OutputStream::assign(const Area& area, const void* data) {
    area.assign(data);
}

inline void OutputStream::backup(int n) {
    if (_fullsize >= _size + n) {
        _size += n;
        _data = (char*)_data - n;
        _pushed_bytes -= n;
        return;
    }
    const int64_t saved_bytecount = _zc_stream->ByteCount();
    // Backup the remaining size + what user requests. The implementation
    // <= r33563 backups n + _size - _fullsize which is wrong.
    _zc_stream->BackUp(n + _size);
    const int64_t nbackup = saved_bytecount - _zc_stream->ByteCount();
    if (nbackup != n + _size) {
        CHECK(false) << "Expect output stream backward for " << n + _size
                     << " bytes, actually " << nbackup << " bytes";
    }
    _size = 0;
    _fullsize = 0;
    _data = NULL;
    _pushed_bytes -= n;
}

inline Serializer::GroupInfo* Serializer::push_group_info() {
    if (_ndepth + 1 < (int)arraysize(_group_info_fast)) {
        return &_group_info_fast[++_ndepth];
    }
    if (_ndepth >= MAX_DEPTH) {
        return NULL;
    }
    if (_group_info_more == NULL) {
        _group_info_more =
            (GroupInfo*)malloc((MAX_DEPTH + 1 - arraysize(_group_info_fast))
                               * sizeof(GroupInfo));
        if (_group_info_more == NULL) {
            return NULL;
        }
    }
    return &_group_info_more[++_ndepth - arraysize(_group_info_fast)];
}

inline Serializer::GroupInfo& Serializer::peek_group_info() {
    if (_ndepth < (int)arraysize(_group_info_fast)) {
        return _group_info_fast[_ndepth];
    } else {
        return _group_info_more[_ndepth - arraysize(_group_info_fast)];
    }
}

inline void Serializer::add_multiple_int8(const uint8_t* values, size_t count) {
    return add_multiple_int8((const int8_t*)values, count);
}
inline void Serializer::add_multiple_int16(const uint16_t* values, size_t count) {
    return add_multiple_int16((const int16_t*)values, count);
}
inline void Serializer::add_multiple_int32(const uint32_t* values, size_t count) {
    return add_multiple_int32((const int32_t*)values, count);
}
inline void Serializer::add_multiple_int64(const uint64_t* values, size_t count) {
    return add_multiple_int64((const int64_t*)values, count);
}

inline void Serializer::add_multiple_uint8(const int8_t* values, size_t count) {
    return add_multiple_uint8((const uint8_t*)values, count);
}
inline void Serializer::add_multiple_uint16(const int16_t* values, size_t count) {
    return add_multiple_uint16((const uint16_t*)values, count);
}
inline void Serializer::add_multiple_uint32(const int32_t* values, size_t count) {
    return add_multiple_uint32((const uint32_t*)values, count);
}
inline void Serializer::add_multiple_uint64(const int64_t* values, size_t count) {
    return add_multiple_uint64((const uint64_t*)values, count);
}

inline void Serializer::begin_mcpack_array(const StringWrapper& name, FieldType item_type)
{ begin_array_internal(name, item_type, false); }

inline void Serializer::begin_mcpack_array(FieldType item_type)
{ begin_array_internal(item_type, false); }

inline void Serializer::begin_compack_array(const StringWrapper& name, FieldType item_type)
{ begin_array_internal(name, item_type, true); }

inline void Serializer::begin_compack_array(FieldType item_type)
{ begin_array_internal(item_type, true); }

inline void Serializer::begin_object(const StringWrapper& name)
{ begin_object_internal(name); }

inline void Serializer::begin_object()
{ begin_object_internal(); }

inline void Serializer::end_object()
{ end_object_internal(false); }

inline void Serializer::end_object_iso()
{ end_object_internal(true); }

}  // namespace mcpack2pb

#endif  // MCPACK2PB_MCPACK_SERIALIZER_INL_H
