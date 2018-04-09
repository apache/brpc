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

#ifndef BRPC_THRIFT_BINARY_MESSAGE_H
#define BRPC_THRIFT_BINARY_MESSAGE_H

#include <functional>
#include <string>

#include <boost/make_shared.hpp>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "brpc/thrift_binary_head.h"               // thrfit_binary_head_t
#include "butil/iobuf.h"                           // IOBuf

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

namespace brpc {

template <typename T>
void thrift_framed_message_deleter(void* p) {
   delete static_cast<T*>(p);
}

template <typename T>
uint32_t thrift_framed_message_writer(void* p, ::apache::thrift::protocol::TProtocol* prot) {
    T* writer = static_cast<T*>(p);
    return writer->write(prot);
    
}

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();

// Representing a thrift_binary request or response.
class ThriftFramedMessage : public ::google::protobuf::Message {
public:
    thrift_binary_head_t head;
    butil::IOBuf body;
    std::function< void (void*) > thrift_raw_instance_deleter;
    std::function<uint32_t (void*, ::apache::thrift::protocol::TProtocol*) > thrift_raw_instance_writer; 
    void* thrift_raw_instance;

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

    virtual uint32_t write(::apache::thrift::protocol::TProtocol* oprot) { return 0;}
    virtual uint32_t read(::apache::thrift::protocol::TProtocol* iprot) { return 0;}

    template<typename T>
    T* cast() {

        thrift_raw_instance = new T;

        // serilize binary thrift message to thrift struct request
        // for response, we just return the new instance and deserialize it in Closure
        if (body.size() > 0) {
            auto in_buffer =
                boost::make_shared<apache::thrift::transport::TMemoryBuffer>();
            auto in_portocol =
                boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(in_buffer);

            // Cut the thrift buffer and parse thrift message
            size_t body_len  = head.body_len;
            auto thrift_buffer = static_cast<uint8_t*>(new uint8_t[body_len]);

            const size_t k = body.copy_to(thrift_buffer, body_len);
            if ( k != body_len) {
                delete [] thrift_buffer;
                return false;
            }

            in_buffer->resetBuffer(thrift_buffer, body_len);

            // The following code was taken and modified from thrift auto generated code

            int32_t rseqid = 0;
            std::string fname;
            ::apache::thrift::protocol::TMessageType mtype;

            in_portocol->readMessageBegin(fname, mtype, rseqid);

            apache::thrift::protocol::TInputRecursionTracker tracker(*in_portocol);
            uint32_t xfer = 0;
            ::apache::thrift::protocol::TType ftype;
            int16_t fid;

            xfer += in_portocol->readStructBegin(fname);

            using ::apache::thrift::protocol::TProtocolException;
            
            while (true)
            {
              xfer += in_portocol->readFieldBegin(fname, ftype, fid);
              if (ftype == ::apache::thrift::protocol::T_STOP) {
                break;
              }
              switch (fid)
              {
                case 1:
                  if (ftype == ::apache::thrift::protocol::T_STRUCT) {
                    xfer += static_cast<T*>(thrift_raw_instance)->read(in_portocol.get());
                  } else {
                    xfer += in_portocol->skip(ftype);
                  }
                  break;
                default:
                  xfer += in_portocol->skip(ftype);
                  break;
              }
              xfer += in_portocol->readFieldEnd();
            }
            
            xfer += in_portocol->readStructEnd();

            in_portocol->readMessageEnd();
            in_portocol->getTransport()->readEnd();
            // End thrfit auto generated code

            delete [] thrift_buffer;
            
        }

        thrift_raw_instance_deleter = &thrift_framed_message_deleter<T>;
        thrift_raw_instance_writer = &thrift_framed_message_writer<T>;
        return static_cast<T*>(thrift_raw_instance);
    }

private:
    void SharedCtor();
    void SharedDtor();
private:
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();

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

    virtual uint32_t write(::apache::thrift::protocol::TProtocol* oprot) {
        return thrift_message_->write(oprot);
    }

    virtual uint32_t read(::apache::thrift::protocol::TProtocol* iprot) {
        return thrift_message_->read(iprot);
    }

    T& raw(){
        return *thrift_message_;
    }

private:
    T* thrift_message_;
};

} // namespace brpc

#endif  // BRPC_THRIFT_BINARY_MESSAGE_H
