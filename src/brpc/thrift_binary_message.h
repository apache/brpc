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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_THRIFT_BINARY_MESSAGE_H
#define BRPC_THRIFT_BINARY_MESSAGE_H

#include <string>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "brpc/thrift_binary_head.h"                           // thrfit_binary_head_t
#include "butil/iobuf.h"                           // IOBuf


namespace brpc {

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();

// Representing a thrift_binary request or response.
class ThriftBinaryMessage : public ::google::protobuf::Message {
public:
    thrift_binary_head_t head;
    butil::IOBuf body;
    
public:
    ThriftBinaryMessage();
    virtual ~ThriftBinaryMessage();
  
    ThriftBinaryMessage(const ThriftBinaryMessage& from);
  
    inline ThriftBinaryMessage& operator=(const ThriftBinaryMessage& from) {
        CopyFrom(from);
        return *this;
    }
  
    static const ::google::protobuf::Descriptor* descriptor();
    static const ThriftBinaryMessage& default_instance();
  
    void Swap(ThriftBinaryMessage* other);
  
    // implements Message ----------------------------------------------
  
    ThriftBinaryMessage* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const ThriftBinaryMessage& from);
    void MergeFrom(const ThriftBinaryMessage& from);
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

private:
    void SharedCtor();
    void SharedDtor();
private:
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
  
    void InitAsDefaultInstance();
    static ThriftBinaryMessage* default_instance_;
};

} // namespace brpc


#endif  // BRPC_THRIFT_BINARY_MESSAGE_H
