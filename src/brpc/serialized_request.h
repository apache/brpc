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

#ifndef BRPC_SERIALIZED_REQUEST_H
#define BRPC_SERIALIZED_REQUEST_H

#include <string>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "butil/iobuf.h"


namespace brpc {

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fserialized_5frequest_2eproto();

class SerializedRequest : public ::google::protobuf::Message {
public:
    SerializedRequest();
    virtual ~SerializedRequest();
  
    SerializedRequest(const SerializedRequest& from);
  
    inline SerializedRequest& operator=(const SerializedRequest& from) {
        CopyFrom(from);
        return *this;
    }
  
    static const ::google::protobuf::Descriptor* descriptor();
    static const SerializedRequest& default_instance();
  
    void Swap(SerializedRequest* other);
  
    // implements Message ----------------------------------------------
  
    SerializedRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const SerializedRequest& from);
    void Clear();
    bool IsInitialized() const;
    int ByteSize() const;
    int GetCachedSize() const { return (int)_serialized.size(); }
    ::google::protobuf::Metadata GetMetadata() const;
    butil::IOBuf& serialized_data() { return _serialized; }
    const butil::IOBuf& serialized_data() const { return _serialized; }
    
private:
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* output) const;
    void MergeFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const SerializedRequest& from);
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;
  
private:
    butil::IOBuf _serialized;
  
friend void protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fserialized_5frequest_2eproto();
  
    void InitAsDefaultInstance();
    static SerializedRequest* default_instance_;
};

} // namespace brpc


#endif  // BRPC_SERIALIZED_REQUEST_H
