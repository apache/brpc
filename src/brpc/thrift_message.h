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


#ifndef BRPC_THRIFT_MESSAGE_H
#define BRPC_THRIFT_MESSAGE_H

#include <google/protobuf/message.h>
#include "butil/iobuf.h"
#include "butil/class_name.h"
#include "brpc/channel_base.h"
#include "brpc/controller.h"
#include "brpc/proto_base.pb.h"
#include "brpc/pb_compat.h"

namespace apache {
namespace thrift {
class TBase;
namespace protocol {
class TProtocol;
}
}
}

namespace brpc {

class ThriftStub;

static const int16_t THRIFT_INVALID_FID = -1;
static const int16_t THRIFT_REQUEST_FID = 1;
static const int16_t THRIFT_RESPONSE_FID = 0;

// Problem: TBase is absent in thrift 0.9.3
// Solution: Wrap native messages with templates into instances inheriting
//   from ThriftMessageBase which can be stored and handled uniformly.
class ThriftMessageBase {
public:
    virtual ~ThriftMessageBase() {}
    virtual uint32_t Read(::apache::thrift::protocol::TProtocol* iprot) = 0;
    virtual uint32_t Write(::apache::thrift::protocol::TProtocol* oprot) const = 0;
};

// Representing a thrift framed request or response.
class ThriftFramedMessage : public ::google::protobuf::Message {
friend class ThriftStub;
public:
    butil::IOBuf body; // ~= "{ raw_instance }"
    int16_t field_id;  // must be set when body is set.
    
private:
    bool _own_raw_instance;
    ThriftMessageBase* _raw_instance;

public:
    ThriftMessageBase* raw_instance() const { return _raw_instance; }

    template <typename T> T* Cast();
    
    ThriftFramedMessage();

    virtual ~ThriftFramedMessage();
  
    ThriftFramedMessage(const ThriftFramedMessage& from) = delete;
  
    ThriftFramedMessage& operator=(const ThriftFramedMessage& from) = delete;
  
    static const ::google::protobuf::Descriptor* descriptor();
  
    void Swap(ThriftFramedMessage* other);
  
    // implements Message ----------------------------------------------
  
    ThriftFramedMessage* New() const PB_319_OVERRIDE;
#if GOOGLE_PROTOBUF_VERSION >= 3006000
    ThriftFramedMessage* New(::google::protobuf::Arena* arena) const override;
#endif
    void CopyFrom(const ::google::protobuf::Message& from) PB_321_OVERRIDE;
    void MergeFrom(const ::google::protobuf::Message& from) override;
    void CopyFrom(const ThriftFramedMessage& from);
    void MergeFrom(const ThriftFramedMessage& from);
    void Clear() override;
    bool IsInitialized() const override;
  
    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input) PB_310_OVERRIDE;
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const PB_310_OVERRIDE;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const PB_310_OVERRIDE;
    int GetCachedSize() const override { return ByteSize(); }

protected:
    ::google::protobuf::Metadata GetMetadata() const override;

private:
    void SharedCtor();
    void SharedDtor();
};

class ThriftStub {
public:
    explicit ThriftStub(ChannelBase* channel) : _channel(channel) {}

    template <typename REQUEST, typename RESPONSE>
    void CallMethod(const char* method_name,
                    Controller* cntl,
                    const REQUEST* raw_request,
                    RESPONSE* raw_response,
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
                      ThriftMessageBase* raw_msg,
                      int16_t expected_fid);
}

namespace details {

template <typename T>
class ThriftMessageWrapper final : public ThriftMessageBase {
public:
    ThriftMessageWrapper() : msg_ptr(NULL) {}
    ThriftMessageWrapper(T* msg2) : msg_ptr(msg2) {}
    virtual ~ThriftMessageWrapper() {}
    // NOTE: "T::" makes the function call work around vtable
    uint32_t Read(::apache::thrift::protocol::TProtocol* iprot) override final
    { return msg_ptr->T::read(iprot); }
    uint32_t Write(::apache::thrift::protocol::TProtocol* oprot) const override final
    { return msg_ptr->T::write(oprot); }
    T* msg_ptr;
};

template <typename T>
class ThriftMessageHolder final : public ThriftMessageBase {
public:
    virtual ~ThriftMessageHolder() {}
    // NOTE: "T::" makes the function call work around vtable
    uint32_t Read(::apache::thrift::protocol::TProtocol* iprot) override final
    { return msg.T::read(iprot); }
    uint32_t Write(::apache::thrift::protocol::TProtocol* oprot) const override final
    { return msg.T::write(oprot); }
    T msg;
};

// A wrapper closure to own additional stuffs required by ThriftStub
template <typename RESPONSE>
class ThriftDoneWrapper : public ::google::protobuf::Closure {
public:
    explicit ThriftDoneWrapper(::google::protobuf::Closure* done)
        : _done(done) {}
    void Run() override {
        _done->Run();
        delete this;
    }
private:
    ::google::protobuf::Closure* _done;
public:
    ThriftMessageWrapper<RESPONSE> raw_response_wrapper;
    ThriftFramedMessage response;
};

} // namespace details

template <typename T>
T* ThriftFramedMessage::Cast() {
    if (_raw_instance) {
        auto p = dynamic_cast<details::ThriftMessageHolder<T>*>(_raw_instance);
        if (p) {
            return &p->msg;
        }
        delete _raw_instance;
    }
    auto raw_msg_wrapper = new details::ThriftMessageHolder<T>;
    T* raw_msg = &raw_msg_wrapper->msg;
    _raw_instance = raw_msg_wrapper;
    _own_raw_instance = true;

    if (!body.empty()) {
        if (!policy::ReadThriftStruct(body, _raw_instance, field_id)) {
            LOG(ERROR) << "Fail to parse " << butil::class_name<T>();
        }
    }
    return raw_msg;
}

template <typename REQUEST, typename RESPONSE>
void ThriftStub::CallMethod(const char* method_name,
                            Controller* cntl,
                            const REQUEST* raw_request,
                            RESPONSE* raw_response,
                            ::google::protobuf::Closure* done) {
    cntl->_thrift_method_name.assign(method_name);

    details::ThriftMessageWrapper<REQUEST>
        raw_request_wrapper(const_cast<REQUEST*>(raw_request));
    ThriftFramedMessage request;
    request._raw_instance = &raw_request_wrapper;

    if (done == NULL) {
        // response is guaranteed to be unused after a synchronous RPC, no
        // need to allocate it on heap.
        ThriftFramedMessage response;
        details::ThriftMessageWrapper<RESPONSE> raw_response_wrapper(raw_response);
        response._raw_instance = &raw_response_wrapper;
        _channel->CallMethod(NULL, cntl, &request, &response, NULL);
    } else {
        // Let the new_done own the response and release it after Run().
        details::ThriftDoneWrapper<RESPONSE>* new_done =
            new details::ThriftDoneWrapper<RESPONSE>(done);
        new_done->raw_response_wrapper.msg_ptr = raw_response;
        new_done->response._raw_instance = &new_done->raw_response_wrapper;
        _channel->CallMethod(NULL, cntl, &request, &new_done->response, new_done);
    }
}

} // namespace brpc

#endif // BRPC_THRIFT_MESSAGE_H
