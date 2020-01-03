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


#ifndef BRPC_NSHEAD_MESSAGE_H
#define BRPC_NSHEAD_MESSAGE_H

#include <google/protobuf/message.h>
#include "brpc/nshead.h"                     // nshead_t
#include "butil/iobuf.h"                     // IOBuf
#include "brpc/proto_base.pb.h"

namespace brpc {

// Representing a nshead request or response.
class NsheadMessage : public ::google::protobuf::Message {
public:
    nshead_t head;
    butil::IOBuf body;
    
public:
    NsheadMessage();
    virtual ~NsheadMessage();
  
    NsheadMessage(const NsheadMessage& from);
  
    inline NsheadMessage& operator=(const NsheadMessage& from) {
        CopyFrom(from);
        return *this;
    }
  
    static const ::google::protobuf::Descriptor* descriptor();
  
    void Swap(NsheadMessage* other);
  
    // implements Message ----------------------------------------------
  
    NsheadMessage* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const NsheadMessage& from);
    void MergeFrom(const NsheadMessage& from);
    void Clear();
    bool IsInitialized() const;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return ByteSize(); }

protected:
    ::google::protobuf::Metadata GetMetadata() const override;

private:
    void SharedCtor();
    void SharedDtor();
};

} // namespace brpc


#endif  // BRPC_NSHEAD_MESSAGE_H
