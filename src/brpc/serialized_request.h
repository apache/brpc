// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Dec 16 15:00:23 CST 2015

#ifndef BRPC_SERIALIZED_REQUEST_H
#define BRPC_SERIALIZED_REQUEST_H

#include <string>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "base/iobuf.h"


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
    base::IOBuf& serialized_data() { return _serialized; }
    const base::IOBuf& serialized_data() const { return _serialized; }
    
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
    base::IOBuf _serialized;
  
friend void protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fserialized_5frequest_2eproto();
  
    void InitAsDefaultInstance();
    static SerializedRequest* default_instance_;
};

} // namespace brpc


#endif  // BRPC_SERIALIZED_REQUEST_H
