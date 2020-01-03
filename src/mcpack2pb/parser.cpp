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

#include "mcpack2pb/parser.h"

namespace mcpack2pb {

// Binary head before fixed-size types.
class FieldFixedHead {
public:
    void set_type(uint8_t type) { _type = type; }
    uint8_t type() const { return _type; }
    size_t name_offset() const { return sizeof(FieldFixedHead); }
    size_t name_size() const { return _name_size; }
    void set_name_size(size_t name_size) { _name_size = name_size; }
    size_t value_offset() const { return name_offset() + name_size(); }
    size_t value_size() const { return (_type & FIELD_FIXED_MASK); }
    size_t full_size() const { return value_offset() + value_size(); }
private:
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

// Assert type sizes:
BAIDU_CASSERT(sizeof(FieldFixedHead) == 2, size_assert);
BAIDU_CASSERT(sizeof(FieldShortHead) == 3, size_assert);
BAIDU_CASSERT(sizeof(FieldLongHead) == 6, size_assert);
BAIDU_CASSERT(sizeof(ItemsHead) == 4, size_assert);
BAIDU_CASSERT(sizeof(IsoItemsHead) == 1, size_assert);

void ObjectIterator::operator++() {
    if (_stream->popped_bytes() != _expected_popped_bytes) {
        if (_stream->popped_bytes() + _current_field.value.size() ==
            _expected_popped_bytes) {
            // skipping untouched values is acceptible.
            _stream->popn(_current_field.value.size());
        } else if (_stream->popped_bytes() < _expected_popped_bytes) {
            CHECK(false) << "value of name=" << _current_field.name
                         << " is not fully consumed, expected="
                         << _expected_popped_bytes << " actually="
                         << _stream->popped_bytes();
            return set_bad();
        } else {
            CHECK(false) << "Over popped in value of name=" << _current_field.name
                       << " expected=" << _expected_popped_bytes << " actually="
                       << _stream->popped_bytes();
            return set_bad();
        }
    }
    if (_expected_popped_bytes >= _expected_popped_end) {
        return set_end();
    }
    const uint8_t first_byte = _stream->peek1();
    if (first_byte & FIELD_FIXED_MASK) {
        FieldFixedHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldFixedHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldFixedHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            _stream->popn(head.full_size() - sizeof(FieldFixedHead));
            return operator++();  // tailr
        }
        _current_field.name = _stream->ref_cut(&_name_backup_string, head.name_size());
        if (!_current_field.name.empty()) {
            _current_field.name.remove_suffix(1);
        }
        _current_field.value.set((FieldType)head.type(), _stream, head.value_size());
    } else if (first_byte & FIELD_SHORT_MASK) {
        FieldShortHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldShortHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldShortHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            // Skip deleted field.
            _stream->popn(head.full_size() - sizeof(FieldShortHead));
            return operator++();  // tailr
        }
        // Remove FIELD_SHORT_MASK.
        FieldType type = (FieldType)(head.type() & ~FIELD_SHORT_MASK);
        _current_field.name = _stream->ref_cut(&_name_backup_string, head.name_size());
        if (!_current_field.name.empty()) {
            _current_field.name.remove_suffix(1);
        }
        _current_field.value.set(type, _stream, head.value_size());
    } else {
        FieldLongHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldLongHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldLongHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            // Skip deleted field.
            _stream->popn(head.full_size() - sizeof(FieldLongHead));
            return operator++();  // tailr
        }
        _current_field.name = _stream->ref_cut(&_name_backup_string, head.name_size());
        if (!_current_field.name.empty()) {
            _current_field.name.remove_suffix(1);
        }
        _current_field.value.set((FieldType)head.type(), _stream, head.value_size());
    }
}

void ArrayIterator::operator++() {
    if (_stream->popped_bytes() != _expected_popped_bytes) {
        if (_stream->popped_bytes() + _current_field.size() ==
            _expected_popped_bytes) {
            // skipping untouched values is acceptible.
            _stream->popn(_current_field.size());
        } else if (_stream->popped_bytes() < _expected_popped_bytes) {
            CHECK(false) << "previous value is not fully consumed, expected="
                         << _expected_popped_bytes << " actually="
                         << _stream->popped_bytes();
            return set_bad();
        } else {
            CHECK(false) << "Over popped in previous value, expected="
                       << _expected_popped_bytes << " actually="
                       << _stream->popped_bytes();
            return set_bad();
        }
    }
    if (_expected_popped_bytes >= _expected_popped_end) {
        return set_end();
    }
    const uint8_t first_byte = _stream->peek1();
    if (first_byte & FIELD_FIXED_MASK) {
        FieldFixedHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldFixedHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldFixedHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            // Skip deleted fields.
            _stream->popn(head.full_size() - sizeof(FieldFixedHead));
            return operator++();  // tailr
        }
        const uint32_t name_size = head.name_size();
        if (name_size) {
            _stream->popn(name_size);
        }
        _current_field.set((FieldType)head.type(), _stream, head.value_size());
    } else if (first_byte & FIELD_SHORT_MASK) {
        FieldShortHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldShortHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldShortHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            // Skip deleted field.
            _stream->popn(head.full_size() - sizeof(FieldShortHead));
            return operator++();  // tailr
        }
        // Remove FIELD_SHORT_MASK.
        const FieldType type = (FieldType)(head.type() & ~FIELD_SHORT_MASK);
        const uint32_t name_size = head.name_size();
        if (name_size) {
            _stream->popn(name_size);
        }
        _current_field.set(type, _stream, head.value_size());
    } else {
        FieldLongHead head;
        if (_stream->cut_packed_pod(&head) != sizeof(FieldLongHead) ||
            left_size() < head.full_size()) {
            CHECK(false) << "buffer(size=" << left_size() << ") is not enough";
            return set_bad();
        }
        _expected_popped_bytes = _stream->popped_bytes() + head.full_size()
            - sizeof(FieldLongHead);
        if (!(head.type() & FIELD_NON_DELETED_MASK)) {
            // Skip deleted field.
            _stream->popn(head.full_size() - sizeof(FieldLongHead));
            return operator++();  // tailr
        }
        const uint32_t name_size = head.name_size();
        if (name_size) {
            _stream->popn(name_size);
        }
        _current_field.set((FieldType)head.type(), _stream, head.value_size());
    }
}

size_t unbox(InputStream* stream) {
    FieldLongHead head;
    if (stream->cut_packed_pod(&head) != sizeof(FieldLongHead)) {
        CHECK(false) << "Input buffer is not enough";
        return 0;
    }
    if (head.type() != FIELD_OBJECT) {
        CHECK(false) << "type=" << type2str(head.type()) << " is not object";
        return 0;
    }
    if (!(head.type() & FIELD_NON_DELETED_MASK)) {
        CHECK(false) << "The item is deleted";
        return 0;
    }
    if (head.name_size() != 0) {
        CHECK(false) << "The object should not have name";
        return 0;
    }
    return head.value_size();
}

std::ostream& operator<<(std::ostream& os, const UnparsedValue& value) {
    // TODO(gejun): More info.
    return os << type2str(value.type());
}

// NOTE: following functions are too big to be inlined. Actually non-inlining
// them perform better in tests.
int64_t UnparsedValue::as_int64(const char* var) {
    switch ((PrimitiveFieldType)_type) {
    case PRIMITIVE_FIELD_INT8:
        return _stream->cut_packed_pod<int8_t>();
    case PRIMITIVE_FIELD_INT16:
        return _stream->cut_packed_pod<int16_t>();
    case PRIMITIVE_FIELD_INT32:
        return _stream->cut_packed_pod<int32_t>();
    case PRIMITIVE_FIELD_INT64:
        return _stream->cut_packed_pod<int64_t>();
    case PRIMITIVE_FIELD_UINT8:
        return _stream->cut_packed_pod<uint8_t>();
    case PRIMITIVE_FIELD_UINT16:
        return _stream->cut_packed_pod<uint16_t>();
    case PRIMITIVE_FIELD_UINT32:
        return _stream->cut_packed_pod<uint32_t>();
    case PRIMITIVE_FIELD_UINT64: {
        const uint64_t value = _stream->cut_packed_pod<uint64_t>();
        if (value <= (uint64_t)std::numeric_limits<int64_t>::max()) {
            return (int64_t)value;
        }
        CHECK(false) << "uint64=" << value << " to " << var << " overflows";
        _stream->set_bad();
        return std::numeric_limits<int64_t>::max();
    }
    case PRIMITIVE_FIELD_BOOL:
        return _stream->cut_packed_pod<bool>();
    case PRIMITIVE_FIELD_FLOAT:
        CHECK(false) << "Can't set float=" << _stream->cut_packed_pod<float>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    case PRIMITIVE_FIELD_DOUBLE:
        CHECK(false) << "Can't set double=" << _stream->cut_packed_pod<double>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

uint64_t UnparsedValue::as_uint64(const char* var) {
    switch ((PrimitiveFieldType)_type) {
    case PRIMITIVE_FIELD_INT8: {
        const int8_t value = _stream->cut_packed_pod<int8_t>();
        if (value >= 0) {
            return (uint64_t)value;
        }
        CHECK(false) << "Can't set int8=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT16: {
        const int16_t value = _stream->cut_packed_pod<int16_t>();
        if (value >= 0) {
            return (uint64_t)value;
        }
        CHECK(false) << "Can't set int16=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT32: {
        const int32_t value = _stream->cut_packed_pod<int32_t>();
        if (value >= 0) {
            return (uint64_t)value;
        }
        CHECK(false) << "Can't set int32=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT64: {
        const int64_t value = _stream->cut_packed_pod<int64_t>();
        if (value >= 0) {
            return (uint64_t)value;
        }
        CHECK(false) << "Can't set int64=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_UINT8:
        return _stream->cut_packed_pod<uint8_t>();
    case PRIMITIVE_FIELD_UINT16:
        return _stream->cut_packed_pod<uint16_t>();
    case PRIMITIVE_FIELD_UINT32:
        return _stream->cut_packed_pod<uint32_t>();
    case PRIMITIVE_FIELD_UINT64:
        return _stream->cut_packed_pod<uint64_t>();
    case PRIMITIVE_FIELD_BOOL:
        return _stream->cut_packed_pod<bool>();
    case PRIMITIVE_FIELD_FLOAT:
        CHECK(false) << "Can't set float=" << _stream->cut_packed_pod<float>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    case PRIMITIVE_FIELD_DOUBLE:
        CHECK(false) << "Can't set double=" << _stream->cut_packed_pod<double>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

int32_t UnparsedValue::as_int32(const char* var) {
    switch ((PrimitiveFieldType)_type) {
    case PRIMITIVE_FIELD_INT8:
        return _stream->cut_packed_pod<int8_t>();
    case PRIMITIVE_FIELD_INT16:
        return _stream->cut_packed_pod<int16_t>();
    case PRIMITIVE_FIELD_INT32:
        return _stream->cut_packed_pod<int32_t>();
    case PRIMITIVE_FIELD_INT64: {
        const int64_t value = _stream->cut_packed_pod<int64_t>();
        if (value > std::numeric_limits<int32_t>::max()) {
            CHECK(false) << "int64=" << value << " to " << var << " overflows";
            _stream->set_bad();
            return std::numeric_limits<int32_t>::max();
        } else if (value < std::numeric_limits<int32_t>::min()) {
            CHECK(false) << "int64=" << value << " to " << var << " underflows";
            _stream->set_bad();
            return std::numeric_limits<int32_t>::min();
        }
        return (int32_t)value;
    }
    case PRIMITIVE_FIELD_UINT8:
        return _stream->cut_packed_pod<uint8_t>();
    case PRIMITIVE_FIELD_UINT16:
        return _stream->cut_packed_pod<uint16_t>();
    case PRIMITIVE_FIELD_UINT32: {
        const uint32_t value = _stream->cut_packed_pod<uint32_t>();
        if (value <= (uint32_t)std::numeric_limits<int32_t>::max()) {
            return (int32_t)value;
        }
        CHECK(false) << "uint32=" << value << " to " << var << " overflows";
        _stream->set_bad();
        return std::numeric_limits<int32_t>::max();
    }
    case PRIMITIVE_FIELD_UINT64: {
        const uint64_t value = _stream->cut_packed_pod<uint64_t>();
        if (value <= (uint64_t)std::numeric_limits<int32_t>::max()) {
            return (int32_t)value;
        }
        CHECK(false) << "uint64=" << value << " to " << var << " overflows";
        _stream->set_bad();
        return std::numeric_limits<int32_t>::max();
    }
    case PRIMITIVE_FIELD_BOOL:
        return _stream->cut_packed_pod<bool>();
    case PRIMITIVE_FIELD_FLOAT:
        CHECK(false) << "Can't set float=" << _stream->cut_packed_pod<float>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    case PRIMITIVE_FIELD_DOUBLE:
        CHECK(false) << "Can't set double=" << _stream->cut_packed_pod<double>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

uint32_t UnparsedValue::as_uint32(const char* var) {
    switch ((PrimitiveFieldType)_type) {
    case PRIMITIVE_FIELD_INT8: {
        const int8_t value = _stream->cut_packed_pod<int8_t>();
        if (value >= 0) {
            return (uint32_t)value;
        }
        CHECK(false) << "Can't set int8=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT16: {
        const int16_t value = _stream->cut_packed_pod<int16_t>();
        if (value >= 0) {
            return (uint32_t)value;
        }
        CHECK(false) << "Can't set int16=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT32: {
        const int32_t value = _stream->cut_packed_pod<int32_t>();
        if (value >= 0) {
            return (uint32_t)value;
        }
        CHECK(false) << "Can't set int32=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_INT64: {
        const int64_t value = _stream->cut_packed_pod<int64_t>();
        if (value >= 0 &&
            value <= (int64_t)std::numeric_limits<uint32_t>::max()) {
            return (uint32_t)value;
        }
        CHECK(false) << "Can't set int64=" << value << " to " << var;
        _stream->set_bad();
        return 0;
    }
    case PRIMITIVE_FIELD_UINT8:
        return _stream->cut_packed_pod<uint8_t>();
    case PRIMITIVE_FIELD_UINT16:
        return _stream->cut_packed_pod<uint16_t>();
    case PRIMITIVE_FIELD_UINT32:
        return _stream->cut_packed_pod<uint32_t>();
    case PRIMITIVE_FIELD_UINT64: {
        const uint64_t value = _stream->cut_packed_pod<uint64_t>();
        if (value <= std::numeric_limits<uint32_t>::max()) {
            return (uint32_t)value;
        }
        CHECK(false) << "uint64=" << value << " to " << var << " overflows";
        _stream->set_bad();
        return std::numeric_limits<uint32_t>::max();
    }
    case PRIMITIVE_FIELD_BOOL:
        return _stream->cut_packed_pod<bool>();
    case PRIMITIVE_FIELD_FLOAT:
        CHECK(false) << "Can't set float=" << _stream->cut_packed_pod<float>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    case PRIMITIVE_FIELD_DOUBLE:
        CHECK(false) << "Can't set double=" << _stream->cut_packed_pod<double>()
                     << " to " << var;
        _stream->set_bad();
        return 0;
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

bool UnparsedValue::as_bool(const char* var) {
    switch ((PrimitiveFieldType)_type) {
    case PRIMITIVE_FIELD_INT8:
        return _stream->cut_packed_pod<int8_t>();
    case PRIMITIVE_FIELD_INT16:
        return _stream->cut_packed_pod<int16_t>();
    case PRIMITIVE_FIELD_INT32:
        return _stream->cut_packed_pod<int32_t>();
    case PRIMITIVE_FIELD_INT64:
        return _stream->cut_packed_pod<int64_t>();
    case PRIMITIVE_FIELD_UINT8:
        return _stream->cut_packed_pod<uint8_t>();
    case PRIMITIVE_FIELD_UINT16:
        return _stream->cut_packed_pod<uint16_t>();
    case PRIMITIVE_FIELD_UINT32:
        return _stream->cut_packed_pod<uint32_t>();
    case PRIMITIVE_FIELD_UINT64:
        return _stream->cut_packed_pod<uint64_t>();
    case PRIMITIVE_FIELD_BOOL:
        return _stream->cut_packed_pod<bool>();
    case PRIMITIVE_FIELD_FLOAT:
        CHECK(false) << "Can't set float=" << _stream->cut_packed_pod<float>()
                     << " to " << var;
        _stream->set_bad();
        return false;
    case PRIMITIVE_FIELD_DOUBLE:
        CHECK(false) << "Can't set double=" << _stream->cut_packed_pod<double>()
                     << " to " << var;
        _stream->set_bad();
        return false;
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return false;
}

float UnparsedValue::as_float(const char* var) {
    if (_type == FIELD_DOUBLE) {
        return (float)_stream->cut_packed_pod<double>();
    } else if (_type == FIELD_FLOAT) {
        return _stream->cut_packed_pod<float>();
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

double UnparsedValue::as_double(const char* var) {
    if (_type == FIELD_DOUBLE) {
        return _stream->cut_packed_pod<double>();
    } else if (_type == FIELD_FLOAT) {
        return _stream->cut_packed_pod<float>();
    }
    CHECK(false) << "Can't set type=" << type2str(_type) << " to " << var;
    _stream->set_bad();
    return 0;
}

void UnparsedValue::as_string(std::string* out, const char* var) {
    out->resize(_size - 1);
    if (_stream->cutn(&(*out)[0], _size - 1) != _size - 1) {
        CHECK(false) << "Not enough data for " << var;
        return;
    }
    _stream->popn(1);
}

std::string UnparsedValue::as_string(const char* var) {
    std::string result;
    as_string(&result, var);
    return result;
}

void UnparsedValue::as_binary(std::string* out, const char* var) {
    out->resize(_size);
    if (_stream->cutn(&(*out)[0], _size) != _size) {
        CHECK(false) << "Not enough data for " << var;
        return;
    }
}

std::string UnparsedValue::as_binary(const char* var) {
    std::string result;
    as_binary(&result, var);
    return result;
}

}  // namespace mcpack2pb
