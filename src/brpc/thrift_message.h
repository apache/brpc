// Copyright (c) 2014 Baidu, Inc.
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

// Authors: wangxuefeng (wangxuefeng@didichuxing.com)

#ifndef BRPC_THRIFT_MESSAGE_H
#define BRPC_THRIFT_MESSAGE_H

#include <functional>
#include <string>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "brpc/details/thrift_utils.h"
#include "butil/iobuf.h"

#include <thrift/protocol/TBinaryProtocol.h>

namespace brpc {

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();

static const int32_t THRIFT_HEAD_VERSION_MASK = (int32_t)0xffffff00;
static const int32_t THRIFT_HEAD_VERSION_1 = (int32_t)0x80010000;
struct thrift_head_t {
    int32_t  body_len;
};

// Representing a thrift framed request or response.
class ThriftFramedMessage : public ::google::protobuf::Message {
public:
    thrift_head_t head;
    butil::IOBuf body;
    void (*thrift_raw_instance_deleter) (void*);
    uint32_t (*thrift_raw_instance_writer) (void*, void*);
    void* thrift_raw_instance;

    int32_t thrift_message_seq_id;

public:
    ThriftFramedMessage();
    virtual ~ThriftFramedMessage();
  
    ThriftFramedMessage(const ThriftFramedMessage& from);
  
    inline ThriftFramedMessage& operator=(const ThriftFramedMessage& from) {
        CopyFrom(from);
        return *this;
    }
  
    static const ::google::protobuf::Descriptor* descriptor();
    static const ThriftFramedMessage& default_instance();
  
    void Swap(ThriftFramedMessage* other);
  
    // implements Message ----------------------------------------------
  
    ThriftFramedMessage* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const ThriftFramedMessage& from);
    void MergeFrom(const ThriftFramedMessage& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return ByteSize(); }
    ::google::protobuf::Metadata GetMetadata() const;

    virtual uint32_t write(void* /*oprot*/) { return 0;}
    virtual uint32_t read(void* /*iprot*/) { return 0;}

    template<typename T>
    T* Cast() {
        thrift_raw_instance = new T;
        assert(thrift_raw_instance);

        // serialize binary thrift message to thrift struct request
        // for response, we just return the new instance and deserialize it in Closure
        if (body.size() > 0 ) {
            if (serialize_iobuf_to_thrift_message<T>(body, thrift_raw_instance, &thrift_message_seq_id)) {
            } else {
                delete static_cast<T*>(thrift_raw_instance);
                return nullptr;
            }
        }

        thrift_raw_instance_deleter = &thrift_framed_message_deleter<T>;
        thrift_raw_instance_writer = &thrift_framed_message_writer<T>;
        return static_cast<T*>(thrift_raw_instance);
    }

private:
    void SharedCtor();
    void SharedDtor();
private:
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();

    void InitAsDefaultInstance();
    static ThriftFramedMessage* default_instance_;
};

template <typename T>
class ThriftMessage : public ThriftFramedMessage {

public:
    ThriftMessage() {
        thrift_message_ = new T;
        assert(thrift_message_ != nullptr);
    }

    virtual ~ThriftMessage() { delete thrift_message_; }

    ThriftMessage<T>& operator= (const ThriftMessage<T>& other) {
        *thrift_message_ = *(other.thrift_message_);
        return *this;
    }

    virtual uint32_t write(void* oprot) {
        return thrift_message_->write(static_cast<::apache::thrift::protocol::TProtocol*>(oprot));
    }

    virtual uint32_t read(void* iprot) {
        return thrift_message_->read(static_cast<::apache::thrift::protocol::TProtocol*>(iprot));
    }

    T& raw() {
        return *thrift_message_;
    }

private:
    T* thrift_message_;
};

} // namespace brpc

#endif // BRPC_THRIFT_MESSAGE_H

