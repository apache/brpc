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


#ifndef BRPC_REDIS_REPLY_H
#define BRPC_REDIS_REPLY_H

#include "butil/iobuf.h"                  // butil::IOBuf
#include "butil/strings/string_piece.h"   // butil::StringPiece
#include "butil/arena.h"                  // butil::Arena
#include "butil/logging.h"                // CHECK
#include "parse_result.h"                 // ParseError


namespace brpc {

// Different types of replies.
enum RedisReplyType {
    REDIS_REPLY_STRING = 1,  // Bulk String
    REDIS_REPLY_ARRAY = 2,
    REDIS_REPLY_INTEGER = 3,
    REDIS_REPLY_NIL = 4,
    REDIS_REPLY_STATUS = 5,  // Simple String
    REDIS_REPLY_ERROR = 6
};

const char* RedisReplyTypeToString(RedisReplyType);

// A reply from redis-server.
class RedisReply {
public:
    // The initial value for a reply is a nil.
    // All needed memory is allocated on `arena'.
    RedisReply(butil::Arena* arena);

    // Type of the reply.
    RedisReplyType type() const { return _type; }
    
    bool is_nil() const;     // True if the reply is a (redis) nil.
    bool is_integer() const; // True if the reply is an integer.
    bool is_error() const;   // True if the reply is an error.
    bool is_string() const;  // True if the reply is a string.
    bool is_array() const;   // True if the reply is an array.

    // Set the reply to the null string.
    void SetNullString();

    // Set the reply to the null array.
    void SetNullArray();

    // Set the reply to the array with `size' elements. After calling
    // SetArray, use operator[] to visit sub replies and set their
    // value.
    void SetArray(int size);

    // Set the reply to a status.
    void SetStatus(const butil::StringPiece& str);
    void FormatStatus(const char* fmt, ...);

    // Set the reply to an error.
    void SetError(const butil::StringPiece& str);
    void FormatError(const char* fmt, ...);

    // Set this reply to integer `value'.
    void SetInteger(int64_t value);

    // Set this reply to a (bulk) string.
    void SetString(const butil::StringPiece& str);
    void FormatString(const char* fmt, ...);

    // Convert the reply into a signed 64-bit integer(according to
    // http://redis.io/topics/protocol). If the reply is not an integer,
    // call stacks are logged and 0 is returned.
    int64_t integer() const;

    // Convert the reply to an error message. If the reply is not an error
    // message, call stacks are logged and "" is returned.
    const char* error_message() const;

    // Convert the reply to a (c-style) string. If the reply is not a string,
    // call stacks are logged and "" is returned. Notice that a
    // string containing \0 is not printed fully, use data() instead.
    const char* c_str() const;
    // Convert the reply to a StringPiece. If the reply is not a string,
    // call stacks are logged and "" is returned. 
    // If you need a std::string, call .data().as_string() (which allocates mem)
    butil::StringPiece data() const;

    // Return number of sub replies in the array if this reply is an array, or
    // return the length of string if this reply is a string, otherwise 0 is
    // returned (call stacks are not logged).
    size_t size() const;
    // Get the index-th sub reply. If this reply is not an array or index is out of
    // range, a nil reply is returned (call stacks are not logged)
    const RedisReply& operator[](size_t index) const;
    RedisReply& operator[](size_t index);

    // Parse from `buf' which may be incomplete.
    // Returns PARSE_OK when an intact reply is parsed and cut off from `buf'.
    // Returns PARSE_ERROR_NOT_ENOUGH_DATA if data in `buf' is not enough to parse,
    // and `buf' is guaranteed to be UNCHANGED so that you can call this
    // function on a RedisReply object with the same buf again and again until
    // the function returns PARSE_OK. This property makes sure the parsing of
    // RedisReply in the worst case is O(N) where N is size of the on-wire
    // reply. As a contrast, if the parsing needs `buf' to be intact,
    // the complexity in worst case may be O(N^2).
    // Returns PARSE_ERROR_ABSOLUTELY_WRONG if the parsing failed.
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf);

    // Serialize to iobuf appender using redis protocol
    bool SerializeTo(butil::IOBufAppender* appender);

    // Swap internal fields with another reply.
    void Swap(RedisReply& other);

    // Reset to the state that this reply was just constructed.
    void Reset();

    // Print fields into ostream
    void Print(std::ostream& os) const;

    // Copy from another reply allocating on `_arena', which is a deep copy.
    void CopyFromDifferentArena(const RedisReply& other);

    // Copy from another reply allocating on a same Arena, which is a shallow copy.
    void CopyFromSameArena(const RedisReply& other);

private:
    static const int npos;

    // RedisReply does not own the memory of fields, copying must be done
    // by calling CopyFrom[Different|Same]Arena.
    DISALLOW_COPY_AND_ASSIGN(RedisReply);

    void FormatStringImpl(const char* fmt, va_list args, RedisReplyType type);
    void SetStringImpl(const butil::StringPiece& str, RedisReplyType type);
    
    RedisReplyType _type;
    int _length;  // length of short_str/long_str, count of replies
    union {
        int64_t integer;
        char short_str[16];
        const char* long_str;
        struct {
            int32_t last_index;  // >= 0 if previous parsing suspends on replies.
            RedisReply* replies;
        } array;
        uint64_t padding[2]; // For swapping, must cover all bytes.
    } _data;
    butil::Arena* _arena;
};

// =========== inline impl. ==============

inline std::ostream& operator<<(std::ostream& os, const RedisReply& r) {
    r.Print(os);
    return os;
}

inline void RedisReply::Reset() {
    _type = REDIS_REPLY_NIL;
    _length = 0;
    _data.array.last_index = -1;
    _data.array.replies = NULL;
    // _arena should not be reset because further memory allocation needs it.
}

inline RedisReply::RedisReply(butil::Arena* arena)
    : _arena(arena) {
    Reset();
}

inline bool RedisReply::is_nil() const {
    return (_type == REDIS_REPLY_NIL || _length == npos);
}
inline bool RedisReply::is_error() const { return _type == REDIS_REPLY_ERROR; }
inline bool RedisReply::is_integer() const { return _type == REDIS_REPLY_INTEGER; }
inline bool RedisReply::is_string() const
{ return _type == REDIS_REPLY_STRING || _type == REDIS_REPLY_STATUS; }
inline bool RedisReply::is_array() const { return _type == REDIS_REPLY_ARRAY; }

inline int64_t RedisReply::integer() const {
    if (is_integer()) {
        return _data.integer;
    }
    CHECK(false) << "The reply is " << RedisReplyTypeToString(_type)
                 << ", not an integer";
    return 0;
}

inline void RedisReply::SetNullArray() {
    if (_type != REDIS_REPLY_NIL) {
        Reset();
    }
    _type = REDIS_REPLY_ARRAY;
    _length = npos;
}

inline void RedisReply::SetNullString() {
    if (_type != REDIS_REPLY_NIL) {
        Reset();
    }
    _type = REDIS_REPLY_STRING;
    _length = npos;
}

inline void RedisReply::SetStatus(const butil::StringPiece& str) {
    return SetStringImpl(str, REDIS_REPLY_STATUS);
}
inline void RedisReply::FormatStatus(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    FormatStringImpl(fmt, ap, REDIS_REPLY_STATUS);
    va_end(ap);
}

inline void RedisReply::SetError(const butil::StringPiece& str) {
    return SetStringImpl(str, REDIS_REPLY_ERROR);
}
inline void RedisReply::FormatError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    FormatStringImpl(fmt, ap, REDIS_REPLY_ERROR);
    va_end(ap);
}

inline void RedisReply::SetInteger(int64_t value) {
    if (_type != REDIS_REPLY_NIL) {
        Reset();
    }
    _type = REDIS_REPLY_INTEGER;
    _length = 0;
    _data.integer = value;
}

inline void RedisReply::SetString(const butil::StringPiece& str) {
    return SetStringImpl(str, REDIS_REPLY_STRING);
}
inline void RedisReply::FormatString(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    FormatStringImpl(fmt, ap, REDIS_REPLY_STRING);
    va_end(ap);
}

inline const char* RedisReply::c_str() const {
    if (is_string()) {
        if (_length < (int)sizeof(_data.short_str)) { // SSO
            return _data.short_str;
        } else {
            return _data.long_str;
        }
    }
    CHECK(false) << "The reply is " << RedisReplyTypeToString(_type)
                 << ", not a string";
    return "";
}

inline butil::StringPiece RedisReply::data() const {
    if (is_string()) {
        if (_length < (int)sizeof(_data.short_str)) { // SSO
            return butil::StringPiece(_data.short_str, _length);
        } else {
            return butil::StringPiece(_data.long_str, _length);
        }
    }
    CHECK(false) << "The reply is " << RedisReplyTypeToString(_type)
                 << ", not a string";
    return butil::StringPiece();
}

inline const char* RedisReply::error_message() const {
    if (is_error()) {
        if (_length < (int)sizeof(_data.short_str)) { // SSO
            return _data.short_str;
        } else {
            return _data.long_str;
        }
    }
    CHECK(false) << "The reply is " << RedisReplyTypeToString(_type)
                 << ", not an error";
    return "";
}

inline size_t RedisReply::size() const {
    return _length;
}

inline RedisReply& RedisReply::operator[](size_t index) {
    return const_cast<RedisReply&>(
            const_cast<const RedisReply*>(this)->operator[](index));
}

inline const RedisReply& RedisReply::operator[](size_t index) const {
    if (is_array() && index < (size_t)_length) {
        return _data.array.replies[index];
    }
    static RedisReply redis_nil(NULL);
    return redis_nil;
}

inline void RedisReply::Swap(RedisReply& other) {
    std::swap(_type, other._type);
    std::swap(_length, other._length);
    std::swap(_data.padding[0], other._data.padding[0]);
    std::swap(_data.padding[1], other._data.padding[1]);
    std::swap(_arena, other._arena);
}

inline void RedisReply::CopyFromSameArena(const RedisReply& other) {
    _type = other._type;
    _length = other._length;
    _data.padding[0] = other._data.padding[0];
    _data.padding[1] = other._data.padding[1];
    _arena = other._arena;
}

} // namespace brpc

#endif  // BRPC_REDIS_H
