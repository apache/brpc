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


#ifndef BRPC_SERIALIZED_REQUEST_H
#define BRPC_SERIALIZED_REQUEST_H

#include <google/protobuf/message.h>
#include "butil/iobuf.h"
#include "brpc/proto_base.pb.h"

namespace brpc {

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
  
    void Swap(SerializedRequest* other);
  
    // implements Message ----------------------------------------------
  
    SerializedRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const SerializedRequest& from);
    void Clear();
    bool IsInitialized() const;
    int ByteSize() const;
    int GetCachedSize() const { return (int)_serialized.size(); }
    butil::IOBuf& serialized_data() { return _serialized; }
    const butil::IOBuf& serialized_data() const { return _serialized; }

protected:
    ::google::protobuf::Metadata GetMetadata() const;
    
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
};

} // namespace brpc


#endif  // BRPC_SERIALIZED_REQUEST_H
