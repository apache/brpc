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

#ifndef MCPACK2PB_MCPACK_PARSER_INL_H
#define MCPACK2PB_MCPACK_PARSER_INL_H

namespace mcpack2pb {

// Binary head before items of array/object except isomorphic array.
struct ItemsHead {
    uint32_t item_count;
} __attribute__((__packed__));

inline size_t InputStream::popn(size_t n) {
    const size_t saved_n = n;
    do {
        if (_size >= (int64_t)n) {
            _data = (const char*)_data + n;
            _size -= n;
            _popped_bytes += saved_n;
            return saved_n;
        }
        n -= _size;
    } while (_zc_stream->Next(&_data, &_size));
    _data = NULL;
    _size = 0;
    _popped_bytes += saved_n - n;
    return saved_n - n;
}
    
inline size_t InputStream::cutn(void* out, size_t n) {
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

template <typename T>
inline size_t InputStream::cut_packed_pod(T* packed_pod) {
    if (_size >= (int)sizeof(T)) {
        *packed_pod = *(T*)_data;
        _data = (const char*)_data + sizeof(T);
        _size -= sizeof(T);
        _popped_bytes += sizeof(T);
        return sizeof(T);
    }
    return cutn(packed_pod, sizeof(T));
}

template <typename T>
inline T InputStream::cut_packed_pod() {
    T packed_pod;
    if (_size >= (int)sizeof(T)) {
        packed_pod = *(T*)_data;
        _data = (const char*)_data + sizeof(T);
        _size -= sizeof(T);
        _popped_bytes += sizeof(T);
        return packed_pod;
    }
    cutn(&packed_pod, sizeof(T));
    return packed_pod;
}
    
inline butil::StringPiece InputStream::ref_cut(std::string* aux, size_t n) {
    if (_size >= (int64_t)n) {
        butil::StringPiece ret((const char*)_data, n);
        _data = (const char*)_data + n;
        _size -= n;
        _popped_bytes += n;
        return ret;
    }
    aux->resize(n);
    size_t m = cutn(&(*aux)[0], n);
    if (m != n) {
        aux->resize(m);
    }
    return *aux;
}

inline uint8_t InputStream::peek1() {
    if (_size > 0) {
        return *(const uint8_t*)_data;
    }
    while (_zc_stream->Next(&_data, &_size)) {
        if (_size > 0) {
            return *(const uint8_t*)_data;
        }
    }
    return 0;
}

// Binary head before items of isomorphic array.
struct IsoItemsHead {
    uint8_t type;
} __attribute__((__packed__));

inline ObjectIterator UnparsedValue::as_object() {
    return ObjectIterator(_stream, _size);
}

inline ArrayIterator UnparsedValue::as_array() {
    return ArrayIterator(_stream, _size);
}

inline ISOArrayIterator UnparsedValue::as_iso_array() {
    return ISOArrayIterator(_stream, _size);
}

inline void ObjectIterator::init(InputStream* stream, size_t size) {
    _field_count = 0;
    _stream = stream;
    _expected_popped_bytes = _stream->popped_bytes() + sizeof(ItemsHead);
    _expected_popped_end = _stream->popped_bytes() + size;
    ItemsHead items_head;
    if (_stream->cut_packed_pod(&items_head) != sizeof(ItemsHead)) {
        CHECK(false) << "buffer(size=" << size << ") is not enough";
        return set_bad();
    }
    _field_count = items_head.item_count;
    operator++();
}

inline void ArrayIterator::init(InputStream* stream, size_t size) {
    _item_count = 0;
    _stream = stream;
    _expected_popped_bytes = _stream->popped_bytes() + sizeof(ItemsHead);
    _expected_popped_end = _stream->popped_bytes() + size;
    ItemsHead items_head;
    if (_stream->cut_packed_pod(&items_head) != sizeof(ItemsHead)) {
        CHECK(false) << "buffer(size=" << size << ") is not enough";
        return set_bad();
    }
    _item_count = items_head.item_count;
    operator++();
}

inline void ISOArrayIterator::init(InputStream* stream, size_t size) {
    _buf_index = 0;
    _buf_count = 0;
    _stream = stream;
    _item_type = (PrimitiveFieldType)0;
    _item_size = 0;
    _item_count = 0;
    _left_item_count = 0;
    IsoItemsHead items_head;
    if (_stream->cut_packed_pod(&items_head) != sizeof(IsoItemsHead)) {
        CHECK(false) << "Not enough data";
        return set_bad();
    }
    _item_type = (PrimitiveFieldType)items_head.type;
    _item_size = get_primitive_type_size(_item_type);
    if (!_item_size) {
        CHECK(false) << "type=" << type2str(_item_type)
                   << " in primitive isoarray is not primitive";
        return set_bad();
    }
    const size_t items_full_size = size - sizeof(IsoItemsHead);
    _item_count = items_full_size / _item_size;
    if (_item_count * _item_size != items_full_size) {
        CHECK(false) << "inconsistent item_count(" << _item_count
                   << ") and value_size(" << items_full_size
                   << "), item_size=" << _item_size;
        return set_bad();
    }
    _left_item_count = _item_count;
    operator++();
}

inline void ISOArrayIterator::operator++() {
    if (_buf_index + 1 < _buf_count) {
        ++_buf_index;
        return;
    }
    // Iterate all items in isomorphic array. We have to do this
    // right here because the items are lacking of headings.
    // Call on_primitives in batch to reduce overhead.
    if (_left_item_count == 0) {
        set_end();
        return;
    }
    _buf_count = std::min((uint32_t)sizeof(_item_buf) / _item_size, _left_item_count);
    _buf_index = 0;
    if (_stream->cutn(_item_buf, _buf_count * _item_size) !=
        _buf_count * _item_size) {
        CHECK(false) << "Not enough data";
        return set_bad();
    }
    _left_item_count -= _buf_count;
}

template <typename T>
inline T ISOArrayIterator::as_integer() const {
    const void* ptr = (_item_buf + _buf_index * _item_size);
    switch ((PrimitiveFieldType)_item_type) {
    case PRIMITIVE_FIELD_INT8:
        return *static_cast<const int8_t*>(ptr);
    case PRIMITIVE_FIELD_INT16:
        return *static_cast<const int16_t*>(ptr);
    case PRIMITIVE_FIELD_INT32:
        return *static_cast<const int32_t*>(ptr);
    case PRIMITIVE_FIELD_INT64:
        return *static_cast<const int64_t*>(ptr);
    case PRIMITIVE_FIELD_UINT8:
        return *static_cast<const uint8_t*>(ptr);
    case PRIMITIVE_FIELD_UINT16:
        return *static_cast<const uint16_t*>(ptr);
    case PRIMITIVE_FIELD_UINT32:
        return *static_cast<const uint32_t*>(ptr);
    case PRIMITIVE_FIELD_UINT64:
        return *static_cast<const uint64_t*>(ptr);
    case PRIMITIVE_FIELD_BOOL:
        return *static_cast<const bool*>(ptr);
    case PRIMITIVE_FIELD_FLOAT:
        return 0;
    case PRIMITIVE_FIELD_DOUBLE:
        return 0;
    }
    return 0;
}

template <typename T>
inline T ISOArrayIterator::as_fp() const {
    const void* ptr = (_item_buf + _buf_index * _item_size);
    if (_item_type == PRIMITIVE_FIELD_FLOAT) {
        return *static_cast<const float*>(ptr);
    } else if (_item_type == PRIMITIVE_FIELD_DOUBLE) {
        return *static_cast<const double*>(ptr);
    }
    return T();
}

}  // namespace mcpack2pb

#endif  // MCPACK2PB_MCPACK_PARSER_INL_H
