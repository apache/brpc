// Copyright (c) 2015 Baidu, Inc.
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
    // A default constructed reply is a nil.
    RedisReply();

    // Type of the reply.
    RedisReplyType type() const { return _type; }
    
    bool is_nil() const;     // True if the reply is a (redis) nil.
    bool is_integer() const; // True if the reply is an integer.
    bool is_error() const;   // True if the reply is an error.
    bool is_string() const;  // True if the reply is a string.
    bool is_array() const;   // True if the reply is an array.

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
    const RedisReply& operator[](size_t index) const;

    // Parse from `buf' which may be incomplete and allocate needed memory
    // on `arena'.
    // Returns PARSE_OK when an intact reply is parsed and cut off from `buf'.
    // Returns PARSE_ERROR_NOT_ENOUGH_DATA if data in `buf' is not enough to parse,
    // and `buf' is guaranteed to be UNCHANGED so that you can call this
    // function on a RedisReply object with the same buf again and again until
    // the function returns PARSE_OK. This property makes sure the parsing of
    // RedisReply in the worst case is O(N) where N is size of the on-wire
    // reply. As a contrast, if the parsing needs `buf' to be intact,
    // the complexity in worst case may be O(N^2).
    // Returns PARSE_ERROR_ABSOLUTELY_WRONG if the parsing failed.
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf, butil::Arena* arena);

    // Swap internal fields with another reply.
    void Swap(RedisReply& other);

    // Reset to the state that this reply was just constructed.
    void Clear();

    // Print fields into ostream
    void Print(std::ostream& os) const;

    // Copy from another reply allocating on a different Arena, and allocate
    // required memory with `self_arena'.
    void CopyFromDifferentArena(const RedisReply& other,
                                butil::Arena* self_arena);

    // Copy from another reply allocating on a same Arena.
    void CopyFromSameArena(const RedisReply& other);

private:
    // RedisReply does not own the memory of fields, copying must be done
    // by calling CopyFrom[Different|Same]Arena.
    DISALLOW_COPY_AND_ASSIGN(RedisReply);
    
    RedisReplyType _type;
    uint32_t _length;  // length of short_str/long_str, count of replies
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
};

// =========== inline impl. ==============

inline std::ostream& operator<<(std::ostream& os, const RedisReply& r) {
    r.Print(os);
    return os;
}

inline RedisReply::RedisReply()
    : _type(REDIS_REPLY_NIL)
    , _length(0) {
    _data.array.last_index = -1;
    _data.array.replies = NULL;
}

inline bool RedisReply::is_nil() const { return _type == REDIS_REPLY_NIL; }
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

inline const char* RedisReply::c_str() const {
    if (is_string()) {
        if (_length < sizeof(_data.short_str)) { // SSO
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
        if (_length < sizeof(_data.short_str)) { // SSO
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
        if (_length < sizeof(_data.short_str)) { // SSO
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
    return (is_array() ? _length : 0);
}

inline const RedisReply& RedisReply::operator[](size_t index) const {
    if (is_array() && index < _length) {
        return _data.array.replies[index];
    }
    static RedisReply redis_nil;
    return redis_nil;
}

inline void RedisReply::Swap(RedisReply& other) {
    std::swap(_type, other._type);
    std::swap(_length, other._length);
    std::swap(_data.padding[0], other._data.padding[0]);
    std::swap(_data.padding[1], other._data.padding[1]);
}

inline void RedisReply::Clear() {
    _type = REDIS_REPLY_NIL;
    _length = 0;
    _data.array.last_index = -1;
    _data.array.replies = NULL;
}

inline void RedisReply::CopyFromSameArena(const RedisReply& other) {
    _type = other._type;
    _length = other._length;
    _data.padding[0] = other._data.padding[0];
    _data.padding[1] = other._data.padding[1];
}

} // namespace brpc

#endif  // BRPC_REDIS_H
