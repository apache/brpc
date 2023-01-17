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

#ifndef MCPACK2PB_MCPACK_SERIALIZER_H
#define MCPACK2PB_MCPACK_SERIALIZER_H

#include <limits>
#include <vector>
#include <google/protobuf/io/zero_copy_stream.h>
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "mcpack2pb/field_type.h"

// CAUTION: Methods in this header is not intended to be public to users of
// brpc, and subject to change at any future time.

namespace mcpack2pb {

// Send bytes into ZeroCopyOutputStream
class OutputStream {
public:
    class Area {
    public:
        Area() : _addr1(NULL)
               , _addr2(NULL)
               , _size1(0)
               , _size2(0)
               , _addional_area(NULL) {}
        Area(const butil::LinkerInitialized&) {}
        Area(const Area& rhs);
        Area& operator=(const Area& rhs);
        ~Area();
        void add(void* data, size_t n);
        void assign(const void* data) const;
    private:
        void* _addr1;
        void* _addr2;
        unsigned _size1;
        unsigned _size2;
        std::vector<butil::StringPiece>* _addional_area;
    };

    // TODO(gejun): The zero-copy stream MUST return permanent memory blocks
    // to support reserve(). E.g. StringOutputStream can't be used because
    // the string inside invalidates previous memory blocks after resizing.
    OutputStream(google::protobuf::io::ZeroCopyOutputStream* stream)
        : _good(true)
        , _fullsize(0)
        , _size(0)
        , _data(NULL)
        , _zc_stream(stream)
        , _pushed_bytes(0)
    {}

    ~OutputStream() { done(); }

    // Append n bytes.
    void append(const void* data, int n);

    // Append a pod.
    template <typename T> void append_packed_pod(const T& packed_pod);

    template <typename T> T* append_packed_pod();

    // Append a byte.
    void push_back(char c);

    // If next n bytes in the zero-copy stream is continuous, consume it
    // and return the begining address. NULL otherwise.
    void* skip_continuous(int n);

    // Consume n bytes from the stream and return an object representing the
    // consumed area.
    Area reserve(int n);

    // Change data at the area. `data' must be as long as the reserved area.
    void assign(const Area&, const void* data);

    // Go back for n bytes.
    void backup(int n);
    
    // Returns bytes pushed and cut since creation of this stream.
    size_t pushed_bytes() const { return _pushed_bytes; }

    // Returns false if error occurred during serialization.
    bool good() { return _good; }

    void set_bad() { _good = false; }

    // Optionally called to backup buffered bytes to zero-copy stream.
    void done();
    
private:
    bool _good;
    int _fullsize;
    int _size;
    void* _data;
    google::protobuf::io::ZeroCopyOutputStream* _zc_stream;
    size_t _pushed_bytes;
};

// This class is different from butil::StringPiece that it only can be
// constructed from std::string or const char* which are both ended with
// zero, so that in later serialization of the name, we can simply copy one
// extra byte to end the name with zero(required by compack) rather than
// appending the zero in a separate function call which is less efficient.
class StringWrapper {
public:
    StringWrapper(const std::string& str)
        : _data(str.c_str()), _size(str.size()) {}
    StringWrapper(const char* str) {
        if (str) {
            _data = str;
            _size = strlen(str);
        } else {
            _data = "";
            _size = 0;
        }
    }
    ~StringWrapper() { }
    const char* data() const { return _data; }
    size_t size() const { return _size; }
    bool empty() const { return !_size; }
private:
    const char* _data;
    size_t _size;
};
inline std::ostream& operator<<(std::ostream& os, const StringWrapper& sw) {
    return os << butil::StringPiece(sw.data(), sw.size());
}

class Serializer {
public:
    // Serialize into `output'.
    explicit Serializer(OutputStream* stream);
    ~Serializer();

    bool good() const { return _stream->good(); }
    void set_bad() { _stream->set_bad(); }
    size_t pushed_bytes() const { return _stream->pushed_bytes(); }

    // WARNING: Names to all methods cannot be longer that 254 bytes.

    // Append a primitive type with name.
    // Used between begin_object() and end_object().
    void add_int8(const StringWrapper& name, int8_t value);
    void add_int16(const StringWrapper& name, int16_t value);
    void add_int32(const StringWrapper& name, int32_t value);
    void add_int64(const StringWrapper& name, int64_t value);
    void add_uint8(const StringWrapper& name, uint8_t value);
    void add_uint16(const StringWrapper& name, uint16_t value);
    void add_uint32(const StringWrapper& name, uint32_t value);
    void add_uint64(const StringWrapper& name, uint64_t value);
    void add_bool(const StringWrapper& name, bool value);
    void add_float(const StringWrapper& name, float value);
    void add_double(const StringWrapper& name, double value);

    // Add a primitive type without name.
    // Used between begin_xxx_array() and end_xxx_array().
    void add_int8(int8_t value);
    void add_int16(int16_t value);
    void add_int32(int32_t value);
    void add_int64(int64_t value);
    void add_uint8(uint8_t value);
    void add_uint16(uint16_t value);
    void add_uint32(uint32_t value);
    void add_uint64(uint64_t value);
    void add_bool(bool value);
    void add_float(float value);
    void add_double(double value);

    // Add multiple primitive types in one call.
    void add_multiple_int8(const int8_t* values, size_t count);
    void add_multiple_int8(const uint8_t* values, size_t count);
    void add_multiple_int16(const int16_t* values, size_t count);
    void add_multiple_int16(const uint16_t* values, size_t count);
    void add_multiple_int32(const int32_t* values, size_t count);
    void add_multiple_int32(const uint32_t* values, size_t count);
    void add_multiple_int64(const int64_t* values, size_t count);
    void add_multiple_int64(const uint64_t* values, size_t count);
    void add_multiple_uint8(const uint8_t* values, size_t count);
    void add_multiple_uint8(const int8_t* values, size_t count);
    void add_multiple_uint16(const uint16_t* values, size_t count);
    void add_multiple_uint16(const int16_t* values, size_t count);
    void add_multiple_uint32(const uint32_t* values, size_t count);
    void add_multiple_uint32(const int32_t* values, size_t count);
    void add_multiple_uint64(const uint64_t* values, size_t count);
    void add_multiple_uint64(const int64_t* values, size_t count);
    void add_multiple_bool(const bool* values, size_t count);
    void add_multiple_float(const float* values, size_t count);
    void add_multiple_double(const double* values, size_t count);

    // Append a string.
    // The serialized value ends with 0 and the length counts 0.
    void add_string(const StringWrapper& name, const StringWrapper& str);
    void add_string(const StringWrapper& str);

    // Append binary data.
    void add_binary(const StringWrapper& name, const std::string& data);
    void add_binary(const StringWrapper& name, const void* data, size_t n);
    void add_binary(const std::string& data);
    void add_binary(const void* data, size_t n);

    // Append a null.
    void add_null(const StringWrapper& name);
    void add_null();

    // Append an empty array.
    void add_empty_array(const StringWrapper& name);
    void add_empty_array();

    // Begin/end an array.
    // All items inside the array must be of same type. This is not a
    // restriction of mcpack, but we want to align storage model with protobuf
    // as much as possible.
    // If too many levels of array/object reached, this function fails and
    // leaves the output unchanged.
    void begin_mcpack_array(const StringWrapper& name, FieldType item_type);
    void begin_mcpack_array(FieldType item_type);
    void begin_compack_array(const StringWrapper& name, FieldType item_type);
    void begin_compack_array(FieldType item_type);
    // End any kind of array.
    void end_array();

    // Begin/end an object.
    // If too many levels of array/object reached, this function fails and
    // leaves the output unchanged.
    void begin_object(const StringWrapper& name);
    void begin_object();
    void end_object();
    // TODO(gejun): move to begin_object
    void end_object_iso();

public:
    struct GroupInfo {
        uint32_t item_count;
        bool isomorphic;
        uint8_t item_type;
        uint8_t type;
        uint8_t name_size;
        size_t output_offset;
        int pending_null_count;
        OutputStream::Area head_area;
        OutputStream::Area items_head_area;

        void print(std::ostream&) const;
    };

private:
    DISALLOW_COPY_AND_ASSIGN(Serializer);

    GroupInfo* push_group_info();
    GroupInfo & peek_group_info();
    void begin_object_internal(const StringWrapper& name);
    void begin_object_internal();
    void end_object_internal(bool objectisoarray);
    void begin_array_internal(FieldType item_type, bool compack);
    void begin_array_internal(const StringWrapper& name,
                              FieldType item_type, bool compack);

    OutputStream* _stream;
    int _ndepth;
    GroupInfo _group_info_fast[15];
    GroupInfo *_group_info_more;
};

}  // namespace mcpack2pb

#include "mcpack2pb/serializer-inl.h"

#endif  // MCPACK2PB_MCPACK_SERIALIZER_H
