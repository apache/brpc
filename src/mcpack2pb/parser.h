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

#ifndef MCPACK2PB_MCPACK_PARSER_H
#define MCPACK2PB_MCPACK_PARSER_H

#include <limits>  // std::numeric_limits
#include <google/protobuf/io/zero_copy_stream.h>
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "mcpack2pb/field_type.h"

// CAUTION: Methods in this header is not intended to be public to users of
// brpc, and subject to change at any future time.

namespace mcpack2pb {

// Read bytes from ZeroCopyInputStream
class InputStream {
public:
    InputStream(google::protobuf::io::ZeroCopyInputStream* stream)
        : _good(true)
        , _size(0)
        , _data(NULL)
        , _zc_stream(stream)
        , _popped_bytes(0)
    {}

    ~InputStream() { }

    // Pop at-most n bytes from front side.
    // Returns bytes popped.
    size_t popn(size_t n);

    // Cut off at-most n bytes from front side and copy to `out'.
    // Returns bytes cut.
    size_t cutn(void* out, size_t n);

    template <typename T> size_t cut_packed_pod(T* packed_pod);
    template <typename T> T cut_packed_pod();

    // Cut off at-most n bytes from front side. If the data is stored in
    // continuous memory, return the reference directly, otherwise copy
    // the data into `aux' and return reference of `aux'.
    // Returns a StringPiece referencing the cut-off data.
    butil::StringPiece ref_cut(std::string* aux, size_t n);

    // Peek at the first character. If the stream is empty, 0 is returned.
    uint8_t peek1();

    // Returns bytes popped and cut since creation of this stream.
    size_t popped_bytes() const { return _popped_bytes; }

    // Returns false if error occurred in other consuming functions.
    bool good() const { return _good; }
    
    // If the error prevents parsing from going on, call this method.
    // This method is also called in other functions in this class.
    void set_bad() { _good = false; }
    
private:
    bool _good;
    int _size;
    const void* _data;
    google::protobuf::io::ZeroCopyInputStream* _zc_stream;
    size_t _popped_bytes;
};

class ObjectIterator;
class ArrayIterator;
class ISOArrayIterator;

// Represent a piece of unparsed(and unread) data of InputStream.
struct UnparsedValue {
    UnparsedValue()
        : _type(FIELD_UNKNOWN), _stream(NULL), _size(0) {}
    UnparsedValue(FieldType type, InputStream* stream, size_t size)
        : _type(type), _stream(stream), _size(size) {}
    void set(FieldType type, InputStream* stream, size_t size) {
        _type = type;
        _stream = stream;
        _size = size;
    }
    
    FieldType type() const { return _type; }
    InputStream* stream() { return _stream; }
    const InputStream* stream() const { return _stream; }
    size_t size() const { return _size; }

    // Convert to concrete value. These functions can only be called once!
    ObjectIterator as_object();
    ArrayIterator as_array();
    ISOArrayIterator as_iso_array();
    // `var' in following methods should be a description of the field being
    // parsed, for better error reporting. It has nothing to do with the result.
    int64_t as_int64(const char* var);
    uint64_t as_uint64(const char* var);
    int32_t as_int32(const char* var);
    uint32_t as_uint32(const char* var);
    bool as_bool(const char* var);
    float as_float(const char* var);
    double as_double(const char* var);
    void as_string(std::string* out, const char* var);
    std::string as_string(const char* var);
    void as_binary(std::string* out, const char* var);
    std::string as_binary(const char* var);
        
private:
friend class ObjectIterator;
friend class ArrayIterator;
    void set_end() { _type = FIELD_UNKNOWN; }
    
    FieldType _type;
    InputStream* _stream;
    size_t _size;
};

std::ostream& operator<<(std::ostream& os, const UnparsedValue& value);

// Remove the header of the wrapping object.
// Returns the value size.
size_t unbox(InputStream* stream);

// Iterator all fields in an object which should be created like this:
//   ObjectIterator it(unparsed_value);
//   for (; it != NULL; ++it) {
//     std::cout << "name=" << it->name << " value=" << it->value << std::endl;
//   }
class ObjectIterator {
public:
    struct Field {
        butil::StringPiece name;
        UnparsedValue value;
    };

    // Parse `n' bytes from `stream' as fields of an object.
    ObjectIterator(InputStream* stream, size_t n) { init(stream, n); }
    explicit ObjectIterator(UnparsedValue& value)
    { init(value.stream(), value.size()); }
    ~ObjectIterator() {}

    Field* operator->() { return &_current_field; }
    Field& operator*() { return _current_field; }
    void operator++();
    operator void*() const { return (void*)_current_field.value.type(); }
    
    // Number of fields in the object.
    uint32_t field_count() const { return _field_count; }

private:
    void init(InputStream* stream, size_t n);
    void set_bad() {
        set_end();
        _stream->set_bad();
    }
    void set_end() { _current_field.value._type = FIELD_UNKNOWN; }
    size_t left_size() const
    { return _expected_popped_end - _expected_popped_bytes; }
    
    Field _current_field;
    uint32_t _field_count;
    std::string _name_backup_string;
    InputStream* _stream;
    size_t _expected_popped_bytes;
    size_t _expected_popped_end;
};

// Iterator all items in a (mcpack) array which should be created like this:
//   ArrayIterator it(unparsed_value);
//   for (; it != NULL; ++it) {
//     std::cout << " item=" << *it << std::endl;
//   }
class ArrayIterator {
public:
    typedef UnparsedValue Field;

    ArrayIterator(InputStream* stream, size_t size) { init(stream, size); }
    explicit ArrayIterator(UnparsedValue& value)
    { init(value.stream(), value.size()); }
    ~ArrayIterator() {}

    Field* operator->() { return &_current_field; }
    Field& operator*() { return _current_field; }
    void operator++();
    operator void*() const { return (void*)_current_field.type(); }

    // Number of items in the array.
    uint32_t item_count() const { return _item_count; }
    
private:
    void init(InputStream* stream, size_t n);
    void set_bad() {
        set_end();
        _stream->set_bad();
    }
    void set_end() { _current_field._type = FIELD_UNKNOWN; }
    size_t left_size() const
    { return _expected_popped_end - _expected_popped_bytes; }
    
    Field _current_field;
    uint32_t _item_count;
    InputStream* _stream;
    size_t _expected_popped_bytes;
    size_t _expected_popped_end;
};

// Iterator all items in an isomorphic array which should be created like this:
//   ISOArrayIterator it(unparsed_value);
class ISOArrayIterator {
public:
    ISOArrayIterator(InputStream* stream, size_t size) { init(stream, size); }
    explicit ISOArrayIterator(UnparsedValue& value)
    { init(value.stream(), value.size()); }
    ~ISOArrayIterator() {}
    void operator++();
    operator void*() const { return (void*)(uintptr_t)_item_type; }

    template <typename T> T as_integer() const;
    template <typename T> T as_fp() const;
    int64_t as_int64() const { return as_integer<int64_t>(); }
    uint64_t as_uint64() const { return as_integer<uint64_t>(); }
    int32_t as_int32() const { return as_integer<int32_t>(); }
    uint32_t as_uint32() const { return as_integer<uint32_t>(); }
    bool as_bool() const { return as_integer<bool>(); }
    float as_float() const { return as_fp<float>(); }
    double as_double() const { return as_fp<double>(); }

    PrimitiveFieldType item_type() const { return _item_type; }

    // Number of items in the array.
    uint32_t item_count() const { return _item_count; }

private:
    void init(InputStream* stream, size_t n);
    void set_bad() {
        set_end();
        _stream->set_bad();
    }
    void set_end() { _item_type = (PrimitiveFieldType)0; }

    int _buf_index;
    int _buf_count;
    InputStream* _stream;
    PrimitiveFieldType _item_type;
    uint32_t _item_size;
    uint32_t _item_count;
    uint32_t _left_item_count;
    char _item_buf[1024];
};

}  // namespace mcpack2pb

#include "mcpack2pb/parser-inl.h"

#endif  // MCPACK2PB_MCPACK_PARSER_H
