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

#include "butil/iobuf.h"
#include "brpc/channel_base.h"
#include "brpc/controller.h"

#include <thrift/TBase.h>

namespace brpc {

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fthrift_framed_5fmessage_2eproto();

class ThriftStub;

static const int16_t THRIFT_INVALID_FID = -1;
static const int16_t THRIFT_REQUEST_FID = 1;
static const int16_t THRIFT_RESPONSE_FID = 0;

// Representing a thrift framed request or response.
class ThriftFramedMessage : public ::google::protobuf::Message {
friend class ThriftStub;
public:
    butil::IOBuf body; // ~= "{ raw_instance }"
    int16_t field_id;  // must be set when body is set.
    
private:
    bool _own_raw_instance;
    ::apache::thrift::TBase* _raw_instance;

public:
    ::apache::thrift::TBase* raw_instance() const { return _raw_instance; }

    template <typename T> T* Cast();
    
    ThriftFramedMessage();

    virtual ~ThriftFramedMessage();
  
    ThriftFramedMessage(const ThriftFramedMessage& from) = delete;
  
    ThriftFramedMessage& operator=(const ThriftFramedMessage& from) = delete;
  
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

class ThriftStub {
public:
    explicit ThriftStub(ChannelBase* channel) : _channel(channel) {}

    void CallMethod(const char* method_name,
                    Controller* cntl,
                    const ::apache::thrift::TBase* raw_request,
                    ::apache::thrift::TBase* raw_response,
                    ::google::protobuf::Closure* done);

    void CallMethod(const char* method_name,
                    Controller* cntl,
                    const ThriftFramedMessage* req,
                    ThriftFramedMessage* res,
                    ::google::protobuf::Closure* done);

private:
    ChannelBase* _channel;
};

namespace policy {
// Implemented in policy/thrift_protocol.cpp
bool ReadThriftStruct(const butil::IOBuf& body,
                      ::apache::thrift::TBase* raw_msg,
                      int16_t expected_fid);
}

template <typename T>
T* ThriftFramedMessage::Cast() {
    if (_raw_instance) {
        T* p = dynamic_cast<T*>(_raw_instance);
        if (p) {
            return p;
        }
        delete p;
    }
    T* raw_msg = new T;
    _raw_instance = raw_msg;
    _own_raw_instance = true;

    if (!body.empty()) {
        if (!policy::ReadThriftStruct(body, raw_msg, field_id)) {
            LOG(ERROR) << "Fail to read xxx";
        }
    }
    return raw_msg;
}

} // namespace brpc

#endif // BRPC_THRIFT_MESSAGE_H

