// Copyright (c) 2017 Baidu, Inc.
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

// utils for serialize/parse thrift binary message to brpc protobuf obj.

#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL

#ifndef BRPC_THRIFT_UTILS_H
#define BRPC_THRIFT_UTILS_H

#include "butil/iobuf.h"

#include <thrift/TDispatchProcessor.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

// _THRIFT_STDCXX_H_ is defined by thrift/stdcxx.h which was added since thrift 0.11.0
// TDispatcherProcessor.h above uses shared_ptr and should include stdcxx.h
#ifndef THRIFT_STDCXX
 #if defined(_THRIFT_STDCXX_H_)
 # define THRIFT_STDCXX apache::thrift::stdcxx
 #else
 # define THRIFT_STDCXX boost
 #endif
#endif

namespace brpc {

template <typename T>
void thrift_framed_message_deleter(void* p) {
   delete static_cast<T*>(p);
}

template <typename T>
uint32_t thrift_framed_message_writer(void* p, void* prot) {
    T* writer = static_cast<T*>(p);
    return writer->write(static_cast<::apache::thrift::protocol::TProtocol*>(prot));
}

template<typename T>
bool serialize_iobuf_to_thrift_message(butil::IOBuf& body,
    void* thrift_raw_instance, std::string* method_name, int32_t* thrift_message_seq_id) {

    auto in_buffer =
        THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
    auto in_portocol =
        THRIFT_STDCXX::make_shared<apache::thrift::protocol::TBinaryProtocol>(in_buffer);
    
    // Cut the thrift buffer and parse thrift message
    size_t body_len  = body.size();
    std::unique_ptr<uint8_t[]> thrift_buffer(new uint8_t[body_len]);
    
    const size_t k = body.copy_to(thrift_buffer.get(), body_len);
    if ( k != body_len) {
        return false;
    }
    
    in_buffer->resetBuffer(thrift_buffer.get(), body_len);
    
    // The following code was taken and modified from thrift auto generated code
    
    std::string fname;
    ::apache::thrift::protocol::TMessageType mtype;
    
    in_portocol->readMessageBegin(*method_name, mtype, *thrift_message_seq_id);
    
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
    // End thrift auto generated code
    return true;
}

}

#endif //BRPC_THRIFT_UTILS_H

#endif //ENABLE_THRIFT_FRAMED_PROTOCOL
