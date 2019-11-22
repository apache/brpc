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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_REDIS_MESSAGE_H
#define BRPC_REDIS_MESSAGE_H

#include "butil/iobuf.h"                  // butil::IOBuf
#include "butil/strings/string_piece.h"   // butil::StringPiece
#include "butil/arena.h"                  // butil::Arena
#include "butil/logging.h"                // CHECK
#include "parse_result.h"                 // ParseError


namespace brpc {

// Different types of replies.
enum RedisMessageType {
    REDIS_MESSAGE_STRING = 1,  // Bulk String
    REDIS_MESSAGE_ARRAY = 2,
    REDIS_MESSAGE_INTEGER = 3,
    REDIS_MESSAGE_NIL = 4,
    REDIS_MESSAGE_STATUS = 5,  // Simple String
    REDIS_MESSAGE_ERROR = 6
};

const char* RedisMessageTypeToString(RedisMessageType);

// A reply from redis-server.
class RedisMessage {
public:
    // A default constructed reply is a nil.
    RedisMessage();

    // Type of the reply.
    RedisMessageType type() const { return _type; }
    
    bool is_nil() const;     // True if the reply is a (redis) nil.
    bool is_integer() const; // True if the reply is an integer.
    bool is_error() const;   // True if the reply is an error.
    bool is_string() const;  // True if the reply is a string.
    bool is_array() const;   // True if the reply is an array.

    bool set_nil_string();  // "$-1\r\n"
    bool set_array(int size, butil::Arena* arena);  // size == -1 means nil array("*-1\r\n")
    bool set_status(const std::string& str, butil::Arena* arena);
    bool set_error(const std::string& str, butil::Arena* arena);
    bool set_integer(int64_t value);
    bool set_bulk_string(const std::string& str, butil::Arena* arena);

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

    // Return number of sub replies in the array. If this reply is not an array,
    // 0 is returned (call stacks are not logged).
    size_t size() const;
    // Get the index-th sub reply. If this reply is not an array, a nil reply
    // is returned (call stacks are not logged)
    const RedisMessage& operator[](size_t index) const;
    RedisMessage& operator[](size_t index);

    // Parse from `buf' which may be incomplete and allocate needed memory
    // on `arena'.
    // Returns PARSE_OK when an intact reply is parsed and cut off from `buf'.
    // Returns PARSE_ERROR_NOT_ENOUGH_DATA if data in `buf' is not enough to parse,
    // and `buf' is guaranteed to be UNCHANGED so that you can call this
    // function on a RedisMessage object with the same buf again and again until
    // the function returns PARSE_OK. This property makes sure the parsing of
    // RedisMessage in the worst case is O(N) where N is size of the on-wire
    // reply. As a contrast, if the parsing needs `buf' to be intact,
    // the complexity in worst case may be O(N^2).
    // Returns PARSE_ERROR_ABSOLUTELY_WRONG if the parsing failed.
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf, butil::Arena* arena);

    // 
    bool SerializeToIOBuf(butil::IOBuf* buf);

    // Swap internal fields with another reply.
    void Swap(RedisMessage& other);

    // Reset to the state that this reply was just constructed.
    void Clear();

    // Print fields into ostream
    void Print(std::ostream& os) const;

    // Copy from another reply allocating on a different Arena, and allocate
    // required memory with `self_arena'.
    void CopyFromDifferentArena(const RedisMessage& other,
                                butil::Arena* self_arena);

    // Copy from another reply allocating on a same Arena.
    void CopyFromSameArena(const RedisMessage& other);

private:
    static const uint32_t npos;

    // RedisMessage does not own the memory of fields, copying must be done
    // by calling CopyFrom[Different|Same]Arena.
    DISALLOW_COPY_AND_ASSIGN(RedisMessage);

    bool set_basic_string(const std::string& str, butil::Arena* arena, RedisMessageType type);
    
    RedisMessageType _type;
    uint32_t _length;  // length of short_str/long_str, count of replies
    union {
        int64_t integer;
        char short_str[16];
        const char* long_str;
        struct {
            int32_t last_index;  // >= 0 if previous parsing suspends on replies.
            RedisMessage* replies;
        } array;
        uint64_t padding[2]; // For swapping, must cover all bytes.
    } _data;
};

// =========== inline impl. ==============

inline std::ostream& operator<<(std::ostream& os, const RedisMessage& r) {
    r.Print(os);
    return os;
}

inline RedisMessage::RedisMessage()
    : _type(REDIS_MESSAGE_NIL)
    , _length(0) {
    _data.array.last_index = -1;
    _data.array.replies = NULL;
}

inline bool RedisMessage::is_nil() const {
    return (_type == REDIS_MESSAGE_NIL) ||
        ((_type == REDIS_MESSAGE_STRING || _type == REDIS_MESSAGE_ARRAY) &&
         _length == npos);
}
inline bool RedisMessage::is_error() const { return _type == REDIS_MESSAGE_ERROR; }
inline bool RedisMessage::is_integer() const { return _type == REDIS_MESSAGE_INTEGER; }
inline bool RedisMessage::is_string() const
{ return _type == REDIS_MESSAGE_STRING || _type == REDIS_MESSAGE_STATUS; }
inline bool RedisMessage::is_array() const { return _type == REDIS_MESSAGE_ARRAY; }

inline int64_t RedisMessage::integer() const {
    if (is_integer()) {
        return _data.integer;
    }
    CHECK(false) << "The reply is " << RedisMessageTypeToString(_type)
                 << ", not an integer";
    return 0;
}

inline bool RedisMessage::set_nil_string() {
    _type = REDIS_MESSAGE_STRING;
    _length = npos;
    return true;
}

inline bool RedisMessage::set_array(int size, butil::Arena* arena) {
    _type = REDIS_MESSAGE_ARRAY;
    if (size < 0) {
        _length = npos;
        return true;
    } else if (size == 0) {
        _length = 0;
        return true;
    }
    RedisMessage* subs = (RedisMessage*)arena->allocate(sizeof(RedisMessage) * size);
    if (!subs) {
        LOG(FATAL) << "Fail to allocate RedisMessage[" << size << "]";
        return false;
    }
    for (int i = 0; i < size; ++i) {
        new (&subs[i]) RedisMessage;
    }
    _length = size;
    _data.array.replies = subs;
    return true;
}

inline bool RedisMessage::set_basic_string(const std::string& str, butil::Arena* arena, RedisMessageType type) {
    size_t size = str.size();
    if (size < sizeof(_data.short_str)) {
        memcpy(_data.short_str, str.c_str(), size);
        _data.short_str[size] = '\0';
    } else {
        char* d = (char*)arena->allocate((_length/8 + 1) * 8);
        if (!d) {
            LOG(FATAL) << "Fail to allocate string[" << size << "]";
            return false;
        }
        memcpy(d, str.c_str(), size);
        d[size] = '\0';
        _data.long_str = d;
    }
    _type = type;
    _length = size;
    return true;
}

inline bool RedisMessage::set_status(const std::string& str, butil::Arena* arena) {
    return set_basic_string(str, arena, REDIS_MESSAGE_STATUS);
}

inline bool RedisMessage::set_error(const std::string& str, butil::Arena* arena) {
    return set_basic_string(str, arena, REDIS_MESSAGE_ERROR);
}

inline bool RedisMessage::set_integer(int64_t value) {
    _type = REDIS_MESSAGE_INTEGER;
    _length = 0;
    _data.integer = value;
    return true;
}

inline bool RedisMessage::set_bulk_string(const std::string& str, butil::Arena* arena) {
    return set_basic_string(str, arena, REDIS_MESSAGE_STRING);
}

inline const char* RedisMessage::c_str() const {
    if (is_string()) {
        if (_length < sizeof(_data.short_str)) { // SSO
            return _data.short_str;
        } else {
            return _data.long_str;
        }
    }
    CHECK(false) << "The reply is " << RedisMessageTypeToString(_type)
                 << ", not a string";
    return "";
}

inline butil::StringPiece RedisMessage::data() const {
    if (is_string()) {
        if (_length < sizeof(_data.short_str)) { // SSO
            return butil::StringPiece(_data.short_str, _length);
        } else {
            return butil::StringPiece(_data.long_str, _length);
        }
    }
    CHECK(false) << "The reply is " << RedisMessageTypeToString(_type)
                 << ", not a string";
    return butil::StringPiece();
}

inline const char* RedisMessage::error_message() const {
    if (is_error()) {
        if (_length < sizeof(_data.short_str)) { // SSO
            return _data.short_str;
        } else {
            return _data.long_str;
        }
    }
    CHECK(false) << "The reply is " << RedisMessageTypeToString(_type)
                 << ", not an error";
    return "";
}

inline size_t RedisMessage::size() const {
    return (is_array() ? _length : 0);
}

inline RedisMessage& RedisMessage::operator[](size_t index) {
    return const_cast<RedisMessage&>(
            const_cast<const RedisMessage*>(this)->operator[](index));
}

inline const RedisMessage& RedisMessage::operator[](size_t index) const {
    if (is_array() && index < _length) {
        return _data.array.replies[index];
    }
    static RedisMessage redis_nil;
    return redis_nil;
}

inline void RedisMessage::Swap(RedisMessage& other) {
    std::swap(_type, other._type);
    std::swap(_length, other._length);
    std::swap(_data.padding[0], other._data.padding[0]);
    std::swap(_data.padding[1], other._data.padding[1]);
}

inline void RedisMessage::Clear() {
    _type = REDIS_MESSAGE_NIL;
    _length = 0;
    _data.array.last_index = -1;
    _data.array.replies = NULL;
}

inline void RedisMessage::CopyFromSameArena(const RedisMessage& other) {
    _type = other._type;
    _length = other._length;
    _data.padding[0] = other._data.padding[0];
    _data.padding[1] = other._data.padding[1];
}

} // namespace brpc

#endif  // BRPC_REDIS_H
