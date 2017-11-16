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

#ifndef BRPC_MEMCACHE_H
#define BRPC_MEMCACHE_H

#include <string>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"

namespace brpc {

// Request to memcache.
// Notice that you can pipeline multiple operations in one request and sent
// them to memcached server together.
// Example:
//   MemcacheRequest request;
//   request.get("my_key1");
//   request.get("my_key2");
//   request.set("my_key3", "some_value", 0, 10);
//   ...
//   MemcacheResponse response;
//   // 2 GET and 1 SET are sent to the server together.
//   channel.CallMethod(&controller, &request, &response, NULL/*done*/);
class MemcacheRequest : public ::google::protobuf::Message {
public:
    MemcacheRequest();
    virtual ~MemcacheRequest();
    MemcacheRequest(const MemcacheRequest& from);
    inline MemcacheRequest& operator=(const MemcacheRequest& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(MemcacheRequest* other);

    bool Get(const butil::StringPiece& key);

    // If the cas_value(Data Version Check) is non-zero, the requested operation
    // MUST only succeed if the item exists and has a cas_value identical to the
    // provided value.
    bool Set(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value);
    
    bool Add(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value);

    bool Replace(const butil::StringPiece& key, const butil::StringPiece& value,
                 uint32_t flags, uint32_t exptime, uint64_t cas_value);
    
    bool Append(const butil::StringPiece& key, const butil::StringPiece& value,
                uint32_t flags, uint32_t exptime, uint64_t cas_value);

    bool Prepend(const butil::StringPiece& key, const butil::StringPiece& value,
                 uint32_t flags, uint32_t exptime, uint64_t cas_value);

    bool Delete(const butil::StringPiece& key);
    bool Flush(uint32_t timeout);

    bool Increment(const butil::StringPiece& key, uint64_t delta,
                   uint64_t initial_value, uint32_t exptime);
    bool Decrement(const butil::StringPiece& key, uint64_t delta,
                   uint64_t initial_value, uint32_t exptime);
    
    bool Touch(const butil::StringPiece& key, uint32_t exptime);

    bool Version();

    int pipelined_count() const { return _pipelined_count; }

    // Protobuf methods.
    MemcacheRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const MemcacheRequest& from);
    void MergeFrom(const MemcacheRequest& from);
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
    static const MemcacheRequest& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

    butil::IOBuf& raw_buffer() { return _buf; }
    const butil::IOBuf& raw_buffer() const { return _buf; }
    
private:
    bool GetOrDelete(uint8_t command, const butil::StringPiece& key);
    bool Counter(uint8_t command, const butil::StringPiece& key, uint64_t delta,
                 uint64_t initial_value, uint32_t exptime);
    
    bool Store(uint8_t command, const butil::StringPiece& key,
               const butil::StringPiece& value,
               uint32_t flags, uint32_t exptime, uint64_t cas_value);

    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    int _pipelined_count;
    butil::IOBuf _buf;
    mutable int _cached_size_;

friend void protobuf_AddDesc_baidu_2frpc_2fmemcache_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fmemcache_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fmemcache_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fmemcache_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static MemcacheRequest* default_instance_;
};

// Response from Memcache.
// Notice that a MemcacheResponse instance may contain multiple operations
// due to pipelining. You can call pop_xxx according to your calling sequence
// of operations in corresponding MemcacheRequest.
// Example:
//   MemcacheResponse response;
//   channel.CallMethod(&controller, &request, &response, NULL/*done*/);
//   ...
//   if (!response.PopGet(&my_value1, &flags1, &cas1)) {
//       LOG(FATAL) << "Fail to pop GET: " << response.LastError();
//   } else {
//       // Use my_value1, flags1, cas1
//   }
//   if (!response.PopGet(&my_value2, &flags2, &cas2)) {
//       LOG(FATAL) << "Fail to pop GET: " << response.LastError();
//   } else {
//       // Use my_value2, flags2, cas2
//   }
//   if (!response.PopSet(&cas3)) {
//       LOG(FATAL) << "Fail to pop SET: " << response.LastError();
//   } else {
//       // the SET was successful.
//   }
class MemcacheResponse : public ::google::protobuf::Message {
public:
    // Definition of the valid response status numbers.
    // See section 3.2 Response Status
    enum Status {
        STATUS_SUCCESS = 0x00,
        STATUS_KEY_ENOENT = 0x01,
        STATUS_KEY_EEXISTS = 0x02,
        STATUS_E2BIG = 0x03,
        STATUS_EINVAL = 0x04,
        STATUS_NOT_STORED = 0x05,
        STATUS_DELTA_BADVAL = 0x06,
        STATUS_AUTH_ERROR = 0x20,
        STATUS_AUTH_CONTINUE = 0x21,
        STATUS_UNKNOWN_COMMAND = 0x81,
        STATUS_ENOMEM = 0x82
    };

    MemcacheResponse();
    virtual ~MemcacheResponse();
    MemcacheResponse(const MemcacheResponse& from);
    inline MemcacheResponse& operator=(const MemcacheResponse& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(MemcacheResponse* other);

    const std::string& LastError() const { return _err; }
   
    bool PopGet(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
    bool PopGet(std::string* value, uint32_t* flags, uint64_t* cas_value);
    bool PopSet(uint64_t* cas_value);
    bool PopAdd(uint64_t* cas_value);
    bool PopReplace(uint64_t* cas_value);
    bool PopAppend(uint64_t* cas_value);
    bool PopPrepend(uint64_t* cas_value);
    bool PopDelete();
    bool PopFlush();
    bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
    bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
    bool PopTouch();
    bool PopVersion(std::string* version);
      
    // implements Message ----------------------------------------------
  
    MemcacheResponse* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const MemcacheResponse& from);
    void MergeFrom(const MemcacheResponse& from);
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
    static const MemcacheResponse& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

    butil::IOBuf& raw_buffer() { return _buf; }
    const butil::IOBuf& raw_buffer() const { return _buf; }

    static const char* status_str(Status);

private:
    bool PopCounter(uint8_t command, uint64_t* new_value, uint64_t* cas_value);
    bool PopStore(uint8_t command, uint64_t* cas_value);

    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    std::string _err;
    butil::IOBuf _buf;
    mutable int _cached_size_;

friend void protobuf_AddDesc_baidu_2frpc_2fmemcache_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fmemcache_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fmemcache_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fmemcache_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static MemcacheResponse* default_instance_;
};

} // namespace brpc


#endif  // BRPC_MEMCACHE_H
