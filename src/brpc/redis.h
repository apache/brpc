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

#ifndef BRPC_REDIS_H
#define BRPC_REDIS_H

#include <string>
#include <google/protobuf/stubs/common.h>

#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "butil/arena.h"
#include "redis_reply.h"


namespace brpc {

// Request to redis.
// Notice that you can pipeline multiple commands in one request and sent
// them to ONE redis-server together.
// Example:
//   RedisRequest request;
//   request.AddCommand("PING");
//   RedisResponse response;
//   channel.CallMethod(&controller, &request, &response, NULL/*done*/);
//   if (!cntl.Failed()) {
//       LOG(INFO) << response.reply(0);
//   }
class RedisRequest : public ::google::protobuf::Message {
public:
    RedisRequest();
    virtual ~RedisRequest();
    RedisRequest(const RedisRequest& from);
    inline RedisRequest& operator=(const RedisRequest& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(RedisRequest* other);

    // Add a command with a va_list to this request. The conversion
    // specifiers are compatible with the ones used by hiredis, namely except
    // that %b stands for binary data, other specifiers are similar with printf.
    bool AddCommandV(const char* fmt, va_list args);

    // Concatenate components into a redis command, similarly with
    // redisCommandArgv() in hiredis.
    // Example:
    //   butil::StringPiece components[] = { "set", "key", "value" };
    //   request.AddCommandByComponents(components, arraysize(components));
    bool AddCommandByComponents(const butil::StringPiece* components, size_t n);
    
    // Add a command with variadic args to this request.
    // The reason that adding so many overloads rather than using ... is that
    // it's the only way to dispatch the AddCommand w/o args differently.
    bool AddCommand(const butil::StringPiece& command);
    
    template <typename A1>
    bool AddCommand(const char* format, A1 a1)
    { return AddCommandWithArgs(format, a1); }
    
    template <typename A1, typename A2>
    bool AddCommand(const char* format, A1 a1, A2 a2)
    { return AddCommandWithArgs(format, a1, a2); }
    
    template <typename A1, typename A2, typename A3>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3)
    { return AddCommandWithArgs(format, a1, a2, a3); }
    
    template <typename A1, typename A2, typename A3, typename A4>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4)
    { return AddCommandWithArgs(format, a1, a2, a3, a4); }
    
    template <typename A1, typename A2, typename A3, typename A4, typename A5>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    { return AddCommandWithArgs(format, a1, a2, a3, a4, a5); }

    template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
    bool AddCommand(const char* format, A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6)
    { return AddCommandWithArgs(format, a1, a2, a3, a4, a5, a6); }

    // Number of successfully added commands
    int command_size() const { return _ncommand; }

    // True if previous AddCommand[V] failed.
    bool has_error() const { return _has_error; }

    // Serialize the request into `buf'. Return true on success.
    bool SerializeTo(butil::IOBuf* buf) const;

    // Protobuf methods.
    RedisRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const RedisRequest& from);
    void MergeFrom(const RedisRequest& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return _cached_size_; }
    
    static const ::google::protobuf::Descriptor* descriptor();
    static const RedisRequest& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

    void Print(std::ostream&) const;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;
    bool AddCommandWithArgs(const char* fmt, ...);

    int _ncommand;    // # of valid commands
    bool _has_error;  // previous AddCommand had error
    butil::IOBuf _buf;  // the serialized request.
    mutable int _cached_size_;  // ByteSize

friend void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fredis_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fredis_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static RedisRequest* default_instance_;
};

// Response from Redis.
// Notice that a RedisResponse instance may contain multiple replies
// due to pipelining.
class RedisResponse : public ::google::protobuf::Message {
public:
    RedisResponse();
    virtual ~RedisResponse();
    RedisResponse(const RedisResponse& from);
    inline RedisResponse& operator=(const RedisResponse& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(RedisResponse* other);

    // Number of replies in this response.
    // (May have more than one reply due to pipeline)
    int reply_size() const { return _nreply; }

    // Get index-th reply. If index is out-of-bound, nil reply is returned.
    const RedisReply& reply(int index) const {
        if (index < reply_size()) {
            return (index == 0 ? _first_reply : _other_replies[index - 1]);
        }
        static RedisReply redis_nil;
        return redis_nil;
    }

    // Parse and consume intact replies from the buf.
    // Returns true on success, false otherwise.
    bool ConsumePartialIOBuf(butil::IOBuf& buf, int reply_count);
    
    // implements Message ----------------------------------------------
  
    RedisResponse* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const RedisResponse& from);
    void MergeFrom(const RedisResponse& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return _cached_size_; }

    static const ::google::protobuf::Descriptor* descriptor();
    static const RedisResponse& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;
    
private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    RedisReply _first_reply;
    RedisReply* _other_replies;
    butil::Arena _arena;
    int _nreply;
    mutable int _cached_size_;

friend void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fredis_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fredis_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static RedisResponse* default_instance_;
};

std::ostream& operator<<(std::ostream& os, const RedisRequest&);
std::ostream& operator<<(std::ostream& os, const RedisResponse&);

} // namespace brpc


#endif  // BRPC_REDIS_H
