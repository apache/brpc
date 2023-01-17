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

#include "mcpack2pb/serializer.h"

namespace mcpack2pb {

const static OutputStream::Area INVALID_AREA(butil::LINKER_INITIALIZED);

// Binary head before fixed-size types.
struct FieldFixedHead {
public:
    void set_type(uint8_t type) { _type = type; }
    uint8_t type() const { return _type; }
    size_t name_offset() const { return sizeof(FieldFixedHead); }
    size_t name_size() const { return _name_size; }
    void set_name_size(size_t name_size) { _name_size = name_size; }
    size_t value_offset() const { return name_offset() + name_size(); }
    size_t value_size() const { return (_type & FIELD_FIXED_MASK); }
    size_t full_size() const { return value_offset() + value_size(); }
public:// NOTE(gejun): initializer requires public members.
    uint8_t _type;  // FieldType
    uint8_t _name_size;
} __attribute__((__packed__));

// Binary head before string<=254 or raw<=255
class FieldShortHead {
public:
    void set_type(uint8_t type) { _type = type; }
    uint8_t type() const { return _type; }
    size_t name_offset() const { return sizeof(FieldShortHead); }
    size_t name_size() const { return _name_size; }
    void set_name_size(size_t name_size) { _name_size = name_size; }
    size_t value_offset() const { return name_offset() + name_size(); }
    size_t value_size() const { return _value_size; }
    void set_value_size(size_t value_size) { _value_size = value_size; }
    size_t full_size() const { return value_offset() + value_size(); }
private:
    uint8_t _type;
    uint8_t _name_size;
    uint8_t _value_size;
} __attribute__((__packed__));

// Binary head before variable-size fields.
class FieldLongHead {
public:
    void set_type(uint8_t type) { _type = type; }
    uint8_t type() const { return _type; }
    size_t name_offset() const { return sizeof(FieldLongHead); }
    size_t name_size() const { return _name_size; }
    void set_name_size(size_t name_size) { _name_size = name_size; }
    size_t value_offset() const { return name_offset() + name_size(); }
    size_t value_size() const { return _value_size; }
    void set_value_size(size_t value_size) { _value_size = value_size; }
    size_t full_size() const { return value_offset() + value_size(); }
private:
    uint8_t _type;
    uint8_t _name_size;
    uint32_t _value_size;
} __attribute__((__packed__));

// Binary head before items of array/object except isomorphic array.
struct ItemsHead {
    uint32_t item_count;
} __attribute__((__packed__));

// Assert type sizes:
BAIDU_CASSERT(sizeof(FieldFixedHead) == 2, size_assert);
BAIDU_CASSERT(sizeof(FieldShortHead) == 3, size_assert);
BAIDU_CASSERT(sizeof(FieldLongHead) == 6, size_assert);
BAIDU_CASSERT(sizeof(ItemsHead) == 4, size_assert);

template<typename T> struct GetFieldType {};

template<> struct GetFieldType<int8_t> {
    static const FieldType value = FIELD_INT8;
};
template<> struct GetFieldType<int16_t> {
    static const FieldType value = FIELD_INT16;
};
template<> struct GetFieldType<int32_t> {
    static const FieldType value = FIELD_INT32;
};
template<> struct GetFieldType<int64_t> {
    static const FieldType value = FIELD_INT64;
};
template<> struct GetFieldType<uint8_t> {
    static const FieldType value = FIELD_UINT8;
};
template<> struct GetFieldType<uint16_t> {
    static const FieldType value = FIELD_UINT16;
};
template<> struct GetFieldType<uint32_t> {
    static const FieldType value = FIELD_UINT32;
};
template<> struct GetFieldType<uint64_t> {
    static const FieldType value = FIELD_UINT64;
};
template<> struct GetFieldType<float> {
    static const FieldType value = FIELD_FLOAT;
};
template<> struct GetFieldType<double> {
    static const FieldType value = FIELD_DOUBLE;
};
template<> struct GetFieldType<bool> {
    static const FieldType value = FIELD_BOOL;
};

void Serializer::GroupInfo::print(std::ostream& os) const {
    os << type2str(type);
    if (type == FIELD_ARRAY) {
        os << '[' << type2str(item_type) << ']';
    }
    
    // os << type2str(type) << '=';
    // const uint8_t first_byte = *head_buf;
    // butil::StringPiece name;
    // if (first_byte & FIELD_FIXED_MASK) {
    //     FieldFixedHead head;
    //     memcpy(&head, head_buf, sizeof(FieldFixedHead));
    //     name.set(head_buf + head.name_offset(), head.name_size());
    // } else if (first_byte & FIELD_SHORT_MASK) {
    //     FieldShortHead head;
    //     memcpy(&head, head_buf, sizeof(FieldFixedHead));
    //     name.set(head_buf + head.name_offset(), head.name_size());
    // } else {
    //     FieldLongHead head;
    //     memcpy(&head, head_buf, sizeof(FieldFixedHead));
    //     name.set(head_buf + head.name_offset(), head.name_size());
    // }
    // if (name.empty()) {
    //     os << "<anonymous>";
    // } else {
    //     os << '`' << name << '\'';
    // }
}

std::ostream& operator<<(std::ostream& os, const Serializer::GroupInfo& gi) {
    gi.print(os);
    return os;
}
    
Serializer::Serializer(OutputStream* stream)
    : _stream(stream)
    , _ndepth(0)
    , _group_info_more(NULL) {
    GroupInfo & info = _group_info_fast[0];
    info.item_count = 0;
    info.isomorphic = false;
    info.item_type = 0;
    info.type = FIELD_OBJECT;
    info.name_size = 0;
    info.output_offset = 0;
    info.pending_null_count = 0;
    info.head_area = INVALID_AREA;
    info.items_head_area = INVALID_AREA;
}

Serializer::~Serializer() {
    if (_ndepth && good()/*error always causes opening braces*/) {
        std::ostringstream oss;
        oss << "Serializer(" << this << ") has opening";
        for (; _ndepth > 0; --_ndepth) {
            oss << ' ' << peek_group_info();
        }
        CHECK(false) << oss.str();
    }
    free(_group_info_more);
    _group_info_more = NULL;
}

void add_pending_nulls(OutputStream* stream,
                       Serializer::GroupInfo& group_info);

inline bool object_add_item(Serializer::GroupInfo & group_info,
                            const StringWrapper& name) {
    if (name.size() > 254) {
        CHECK(false) << "Too long name=`" << name << '\'';
        return false;
    }
    if (group_info.type != FIELD_OBJECT) {
        CHECK(false) << "Cannot add `" << name << "' to " <<  group_info;
        return false;
    }
    ++group_info.item_count;
    return true;
}

inline bool array_add_item(OutputStream* stream,
                           Serializer::GroupInfo & group_info,
                           FieldType item_type,
                           uint32_t n) {
    if (group_info.pending_null_count) {
        add_pending_nulls(stream, group_info);
    }
    if (group_info.item_type == item_type ||
        (group_info.item_type == FIELD_OBJECT && item_type == FIELD_ARRAY)) {
        group_info.item_count += n;
        return true;
    }
    if (group_info.type == FIELD_ARRAY) {
        CHECK(false) << "Different item_type=" << type2str(item_type)
                     << " from " << group_info;
        return false;
    }        
    if (group_info.output_offset == 0) {
        // Enable anynomous object at first level.
        group_info.item_count += n;
        return true;
    } else {
        CHECK(false) << "Cannot add field without name to " << group_info;
        return false;
    }
}

//=========================
//adding primitive types

template <typename T>
struct FixedHeadAndValue {
    FieldFixedHead head;
    T value;
} __attribute__((__packed__));

template <typename T>
inline void add_primitive(
    OutputStream* stream, Serializer::GroupInfo & group_info, T value) {
    if (!stream->good()) {
        return;
    }
    if (!array_add_item(stream, group_info, GetFieldType<T>::value, 1)) {
        return stream->set_bad();
    }
    if (group_info.isomorphic) {
        stream->append_packed_pod(value);
    } else {
        FixedHeadAndValue<T> head_and_value;
        head_and_value.head.set_type(GetFieldType<T>::value);
        head_and_value.head.set_name_size(0);
        head_and_value.value = value;
        stream->append_packed_pod(head_and_value);
    }
}

template <typename T>
inline void add_primitive(OutputStream* stream,
                          Serializer::GroupInfo & group_info,
                          const StringWrapper& name,
                          T value) {
    if (name.empty()) {
        return add_primitive(stream, group_info, value);
    }
    if (!stream->good()) {
        return;
    }
    if (!object_add_item(group_info, name)) {
        return stream->set_bad();
    }
    FieldFixedHead head;
    head.set_type(GetFieldType<T>::value);
    head.set_name_size(name.size() + 1);
    void* data = stream->skip_continuous(
        sizeof(FieldFixedHead) + name.size() + 1 + sizeof(T));
    if (data) {
        // the stream has enough continuous space, we do the copying directly.
        // Comparing to the branch below, it saves 2 memcpy and many if/else.
        *(FieldFixedHead*)data = head;
        data = (char*)data + sizeof(FieldFixedHead);
        fast_memcpy(data, name.data(), name.size() + 1);
        data = (char*)data + name.size() + 1;
        *(T*)data = value;
    } else {
        stream->append_packed_pod(head);
        stream->append(name.data(), name.size() + 1);
        stream->append_packed_pod(value);
    }
}

template <typename T>
inline void add_primitives(OutputStream* stream,
                           Serializer::GroupInfo & group_info,
                           const T* values, size_t n) {
    if (!stream->good()) {
        return;
    }
    if (!array_add_item(stream, group_info, GetFieldType<T>::value, n)) {
        return stream->set_bad();
    }
    if (group_info.isomorphic) {
        stream->append(values, sizeof(T) * n);
    } else {
        // Even for mcpack arrays, we need to batch the values into an array
        // of head+value and write the array into stream for (much) better
        // throughput.
        static const size_t BATCH = 128;
        size_t nwritten = 0;
        while (n) {
            const size_t cur_batch = std::min(n, BATCH);
            FixedHeadAndValue<T> tmp[cur_batch];
            for (size_t i = 0; i < cur_batch; ++i) {
                tmp[i].head.set_type(GetFieldType<T>::value);
                tmp[i].head.set_name_size(0);
                tmp[i].value = values[nwritten + i];
            }
            nwritten += cur_batch;
            n -= cur_batch;
            stream->append(tmp, sizeof(FixedHeadAndValue<T>) * cur_batch);
        }
    }
}

void Serializer::add_int8(const StringWrapper& name, int8_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_int16(const StringWrapper& name, int16_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_int32(const StringWrapper& name, int32_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_int64(const StringWrapper& name, int64_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_uint8(const StringWrapper& name, uint8_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_uint16(const StringWrapper& name, uint16_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_uint32(const StringWrapper& name, uint32_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_uint64(const StringWrapper& name, uint64_t value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_bool(const StringWrapper& name, bool value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_float(const StringWrapper& name, float value)
{ add_primitive(_stream, peek_group_info(), name, value); }
void Serializer::add_double(const StringWrapper& name, double value)
{ add_primitive(_stream, peek_group_info(), name, value); }

void Serializer::add_int8(int8_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_int16(int16_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_int32(int32_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_int64(int64_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_uint8(uint8_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_uint16(uint16_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_uint32(uint32_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_uint64(uint64_t value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_bool(bool value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_float(float value)
{ add_primitive(_stream, peek_group_info(), value); }
void Serializer::add_double(double value)
{ add_primitive(_stream, peek_group_info(), value); }

void Serializer::add_multiple_int8(const int8_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_int16(const int16_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_int32(const int32_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_int64(const int64_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_uint8(const uint8_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_uint16(const uint16_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_uint32(const uint32_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_uint64(const uint64_t* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_bool(const bool* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_float(const float* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }
void Serializer::add_multiple_double(const double* values, size_t count)
{ add_primitives(_stream, peek_group_info(), values, count); }

// ==================
// append string/raw

inline void add_binary_internal(OutputStream* stream,
                            Serializer::GroupInfo & group_info,
                            const butil::StringPiece& value,
                            FieldType type) {
    if (!stream->good()) {
        return;
    }
    if (!array_add_item(stream, group_info, type, 1)) {
        return stream->set_bad();
    }
    if (value.size() <= 255) {
        FieldShortHead shead;
        shead.set_type(type | FIELD_SHORT_MASK);
        shead.set_name_size(0);
        shead.set_value_size(value.size());
        stream->append_packed_pod(shead);
        stream->append(value.data(), value.size());
    } else {
        FieldLongHead lhead;
        lhead.set_type(type);
        lhead.set_name_size(0);
        lhead.set_value_size(value.size());
        stream->append_packed_pod(lhead);
        stream->append(value.data(), value.size());
    }
}

inline void add_binary_internal(OutputStream* stream,
                             Serializer::GroupInfo & group_info,
                             const StringWrapper& name,
                             const butil::StringPiece& value,
                             FieldType type) {
    if (name.empty()) {
        return add_binary_internal(stream, group_info, value, type);
    }
    if (!stream->good()) {
        return;
    }
    if (!object_add_item(group_info, name)) {
        return stream->set_bad();
    }    
    if (value.size() <= 255) {
        FieldShortHead shead;
        shead.set_type(type | FIELD_SHORT_MASK);
        shead.set_name_size(name.size() + 1);
        shead.set_value_size(value.size());
        stream->append_packed_pod(shead);
        stream->append(name.data(), name.size() + 1);
        stream->append(value.data(), value.size());
    } else {
        FieldLongHead lhead;
        lhead.set_type(type);
        lhead.set_name_size(name.size() + 1);
        lhead.set_value_size(value.size());
        stream->append_packed_pod(lhead);
        stream->append(name.data(), name.size() + 1);
        stream->append(value.data(), value.size());
    }
}

void Serializer::add_string(const StringWrapper& name, const StringWrapper& s) {
    add_binary_internal(_stream, peek_group_info(), name,
                     butil::StringPiece(s.data(), s.size() + 1), FIELD_STRING);
}
void Serializer::add_string(const StringWrapper& s) {
    add_binary_internal(_stream, peek_group_info(),
                     butil::StringPiece(s.data(), s.size() + 1), FIELD_STRING);
}
void Serializer::add_binary(const StringWrapper& name,
                         const std::string& data) {
    add_binary_internal(_stream, peek_group_info(), name, data, FIELD_BINARY);
}
void Serializer::add_binary(const StringWrapper& name,
                         const void* data, size_t n) {
    add_binary_internal(_stream, peek_group_info(), name,
                     butil::StringPiece((const char*)data, n), FIELD_BINARY);
}
void Serializer::add_binary(const std::string& data) {
    add_binary_internal(_stream, peek_group_info(), data, FIELD_BINARY);
}
void Serializer::add_binary(const void* data, size_t n) {
    add_binary_internal(_stream, peek_group_info(),
                     butil::StringPiece((const char*)data, n), FIELD_BINARY);
}

// ===============
// append null.

inline void add_null_internal(OutputStream* stream,
                              Serializer::GroupInfo& group_info) {
    ++group_info.pending_null_count;
}

struct NullLayout {
    FieldFixedHead head;
    char zero;
} __attribute__((__packed__));

#define MCPACK_NULL_INITIALIZER {{FIELD_NULL,0},0}
#define MCPACK_NULL_ARRAY_INITIALIZER_4                     \
    MCPACK_NULL_INITIALIZER, MCPACK_NULL_INITIALIZER,       \
    MCPACK_NULL_INITIALIZER, MCPACK_NULL_INITIALIZER
#define MCPACK_NULL_ARRAY_INITIALIZER_16                                \
    MCPACK_NULL_ARRAY_INITIALIZER_4, MCPACK_NULL_ARRAY_INITIALIZER_4,   \
    MCPACK_NULL_ARRAY_INITIALIZER_4, MCPACK_NULL_ARRAY_INITIALIZER_4
#define MCPACK_NULL_ARRAY_INITIALIZER_64                                \
    MCPACK_NULL_ARRAY_INITIALIZER_16, MCPACK_NULL_ARRAY_INITIALIZER_16,   \
    MCPACK_NULL_ARRAY_INITIALIZER_16, MCPACK_NULL_ARRAY_INITIALIZER_16

static NullLayout s_null_array[] = {MCPACK_NULL_ARRAY_INITIALIZER_64};

// no inline. inline makes branches in array_add_item worse.
void add_pending_nulls(OutputStream* stream,
                       Serializer::GroupInfo& group_info) {
    if (!stream->good()) {
        return;
    }
    if (group_info.type != FIELD_ARRAY) {
        CHECK(false) << "Cannot add nulls without name to " << group_info;
        return stream->set_bad();
    }
    if (group_info.isomorphic) {
        CHECK(false) << "Cannot add nulls to isomorphic " << group_info;
        return stream->set_bad();
    }
    int n = group_info.pending_null_count;
    group_info.pending_null_count = 0;
    group_info.item_count += n;
    // layout of nulls = [{FIELD_NULL,0,0},{FIELD_NULL,0,0},...]
    while (n) {
        const int cur_batch = std::min(n, (int)arraysize(s_null_array));
        n -= cur_batch;
        stream->append(s_null_array, cur_batch * sizeof(NullLayout));
    }
}

inline void add_null_internal(OutputStream* stream,
                              Serializer::GroupInfo & group_info,
                              const StringWrapper& name) {
    if (name.empty()) {
        return add_null_internal(stream, group_info);
    }
    if (!stream->good()) {
        return;
    }
    if (!object_add_item(group_info, name)) {
        return stream->set_bad();
    }
    FieldFixedHead head;
    head.set_type(FIELD_NULL);
    head.set_name_size(name.size() + 1);
    stream->append_packed_pod(head);
    stream->append(name.data(), name.size() + 1);
    stream->push_back(0);
}

void Serializer::add_null(const StringWrapper& name) {
    add_null_internal(_stream, peek_group_info(), name);
}
void Serializer::add_null() {
    add_null_internal(_stream, peek_group_info());
}

// ===============
// append empty_array.

struct ArrayHead {
    FieldLongHead head;
    ItemsHead items_head;
} __attribute__((__packed__));

struct ObjectHead {
    FieldLongHead head;
    ItemsHead fields_head;
} __attribute__((__packed__));


inline void add_empty_array_internal(OutputStream* stream,
                                    Serializer::GroupInfo& group_info) {
    if (!stream->good()) {
        return;
    }
    if (!array_add_item(stream, group_info, FIELD_ARRAY, 1)) {
        return stream->set_bad();
    }
    ArrayHead arrhead;
    arrhead.head.set_type(FIELD_ARRAY);
    arrhead.head.set_name_size(0);
    arrhead.head.set_value_size(sizeof(ItemsHead));
    arrhead.items_head.item_count = 0;
    stream->append_packed_pod(arrhead);
}

inline void add_empty_array_internal(OutputStream* stream,
                                     Serializer::GroupInfo & group_info,
                                     const StringWrapper& name) {
    if (name.empty()) {
        return add_empty_array_internal(stream, group_info);
    }
    if (!stream->good()) {
        return;
    }
    if (!object_add_item(group_info, name)) {
        return stream->set_bad();
    }
    FieldLongHead head;
    head.set_type(FIELD_ARRAY);
    head.set_name_size(name.size() + 1);
    head.set_value_size(sizeof(ItemsHead));
    ItemsHead items_head = { 0 };
    stream->append_packed_pod(head);
    stream->append(name.data(), name.size() + 1);
    stream->append_packed_pod(items_head);
}

void Serializer::add_empty_array(const StringWrapper& name) {
    add_empty_array_internal(_stream, peek_group_info(), name);
}
void Serializer::add_empty_array() {
    add_empty_array_internal(_stream, peek_group_info());
}

// =========================
// append array/object

void Serializer::begin_object_internal() {
    if (!_stream->good()) {
        return;
    }
    if (!array_add_item(_stream, peek_group_info(), FIELD_OBJECT, 1)) {
        return _stream->set_bad();
    }
    GroupInfo* info = push_group_info();
    if (info == NULL) {
        CHECK(false) << "Fail to push object";
        return _stream->set_bad();
    }
    info->item_count = 0;
    info->isomorphic = false;
    info->item_type = 0;
    info->type = FIELD_OBJECT;
    info->name_size = 0;
    info->output_offset = _stream->pushed_bytes();
    info->pending_null_count = 0;
    info->head_area = _stream->reserve(sizeof(ObjectHead));
    info->items_head_area = INVALID_AREA;
}

void Serializer::begin_object_internal(const StringWrapper& name) {
    if (name.empty()) {
        return begin_object_internal();
    }
    if (!_stream->good()) {
        return;
    }
    if (!object_add_item(peek_group_info(), name)) {
        return _stream->set_bad();
    }
    GroupInfo* info = push_group_info();
    if (info == NULL) {
        CHECK(false) << "Fail to push object=" << name;
        return _stream->set_bad();
    }
    info->item_count = 0;
    info->isomorphic = false;
    info->item_type = 0;
    info->type = FIELD_OBJECT;
    info->name_size = (uint8_t)(name.size() + 1);
    info->output_offset = _stream->pushed_bytes();
    info->pending_null_count = 0;
    info->head_area = _stream->reserve(sizeof(FieldLongHead));
    _stream->append(name.data(), name.size() + 1);
    info->items_head_area = _stream->reserve(sizeof(ItemsHead));
}

inline void pop_group_info(int & ndepth) {
    if (ndepth > 0) {
        --ndepth;
    } else {
        CHECK(false) << "Nothing to pop";
    }
}

void Serializer::end_object_internal(bool objectisoarray) {
    if (!_stream->good()) {
        return;
    }
    GroupInfo & group_info = peek_group_info();
    if (FIELD_OBJECT != group_info.type) {
        CHECK(false) << "end_object() is called on " << group_info;
        return _stream->set_bad();
    }
    if (group_info.name_size == 0) {
        ObjectHead objhead;
        objhead.head.set_type(objectisoarray ? FIELD_OBJECTISOARRAY : FIELD_OBJECT);
        objhead.head.set_name_size(0);
        objhead.head.set_value_size(_stream->pushed_bytes() - group_info.output_offset
                                    - sizeof(FieldLongHead));
        objhead.fields_head.item_count = group_info.item_count;
        _stream->assign(group_info.head_area, &objhead);
        pop_group_info(_ndepth);
    } else {
        FieldLongHead lhead;
        lhead.set_type(objectisoarray ? FIELD_OBJECTISOARRAY : FIELD_OBJECT);
        lhead.set_name_size(group_info.name_size);
        lhead.set_value_size(_stream->pushed_bytes() - group_info.output_offset
                             - group_info.name_size - sizeof(FieldLongHead));
        _stream->assign(group_info.head_area, &lhead);
        const ItemsHead items_head = { group_info.item_count };
        _stream->assign(group_info.items_head_area, &items_head);
        pop_group_info(_ndepth);
    }
}

void Serializer::begin_array_internal(FieldType item_type, bool compack) {
    if (!_stream->good()) {
        return;
    }
    if (!array_add_item(_stream, peek_group_info(), FIELD_ARRAY, 1)) {
        return _stream->set_bad();
    }
    GroupInfo* info = push_group_info();
    if (info == NULL) {
        CHECK(false) << "Fail to push array";
        return _stream->set_bad();
    }
    info->item_count = 0;
    info->item_type = item_type;
    info->type = FIELD_ARRAY;
    info->name_size = 0;
    info->output_offset = _stream->pushed_bytes();
    info->pending_null_count = 0;
    info->head_area = _stream->reserve(sizeof(FieldLongHead));
    if (compack && get_primitive_type_size(item_type)) {
        info->isomorphic = true;
        info->items_head_area = INVALID_AREA;
        _stream->push_back((char)item_type);
    } else {
        info->isomorphic = false;
        info->items_head_area = _stream->reserve(sizeof(ItemsHead));
    }
}

void Serializer::begin_array_internal(const StringWrapper& name,
                                      FieldType item_type,
                                      bool compack) {
    if (name.empty()) {
        return begin_array_internal(item_type, compack);
    }
    if (!_stream->good()) {
        return;
    }
    if (!object_add_item(peek_group_info(), name)) {
        return _stream->set_bad();
    }
    GroupInfo* info = push_group_info();
    if (info == NULL) {
        CHECK(false) << "Fail to push array";
        return _stream->set_bad();
    }
    info->item_count = 0;
    info->item_type = item_type;
    info->type = FIELD_ARRAY;
    info->name_size = (uint8_t)(name.size() + 1);
    info->output_offset = _stream->pushed_bytes();
    info->pending_null_count = 0;
    info->head_area = _stream->reserve(sizeof(FieldLongHead));
    _stream->append(name.data(), name.size() + 1);
    if (compack && get_primitive_type_size(item_type)) {
        info->isomorphic = true;
        info->items_head_area = INVALID_AREA;
        _stream->push_back((char)item_type);
    } else {
        info->isomorphic = false;
        info->items_head_area = _stream->reserve(sizeof(ItemsHead));
    }
}

void Serializer::end_array() {
    if (!_stream->good()) {
        return;
    }
    GroupInfo & group_info = peek_group_info();
    if (FIELD_ARRAY != group_info.type) {
        CHECK(false) << "end_array() is called on " << group_info;
        return _stream->set_bad();
    }
    if (group_info.item_count == 0 && group_info.pending_null_count == 0) {
        // Remove the heading. This is a must because idl cannot load an empty
        // array only with header.
        _stream->backup(_stream->pushed_bytes() - group_info.output_offset);
        pop_group_info(_ndepth);
        --peek_group_info().item_count;
        return;
    }
    // Reset lhead/items_head
    FieldLongHead lhead;
    if (group_info.isomorphic) {
        lhead.set_type(FIELD_ISOARRAY);
    } else {
        lhead.set_type(FIELD_ARRAY);
        if (group_info.pending_null_count) {
            add_pending_nulls(_stream, group_info);
        }
        const ItemsHead items_head = { group_info.item_count };
        _stream->assign(group_info.items_head_area, &items_head);
    }
    lhead.set_name_size(group_info.name_size);
    lhead.set_value_size(_stream->pushed_bytes() - group_info.output_offset
                         - group_info.name_size - sizeof(FieldLongHead));
    _stream->assign(group_info.head_area, &lhead);
    pop_group_info(_ndepth);
}

}  // namespace mcpack2pb
