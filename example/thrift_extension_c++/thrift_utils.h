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

// utils for serilize/deserilize thrift binary message to thrift obj.

#include <brpc/channel.h>

#include <boost/make_shared.hpp>

#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

template <class T>
class BrpcThriftClient {

public:
  BrpcThriftClient() {

    out_buffer_ = boost::make_shared<apache::thrift::transport::TMemoryBuffer>();
    out_ = boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(out_buffer_);

    in_buffer_ = boost::make_shared<apache::thrift::transport::TMemoryBuffer>();
    in_ = boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(in_buffer_);

    client_ = boost::make_shared<T>(in_, out_);
  }

  boost::shared_ptr<T> get_thrift_client() {
    return client_;
  }

 void call_method(brpc::Channel* channel, brpc::Controller* cntl) {

      brpc::ThriftBinaryMessage request;
      brpc::ThriftBinaryMessage response;

      in_buffer_->resetBuffer();

      butil::IOBuf buf;
      buf.append(out_buffer_->getBufferAsString());
      request.body = buf;
      
      // send the request the server
      // Because `done'(last parameter) is NULL, this function waits until
      // the response comes back or error occurs(including timedout).
      channel->CallMethod(NULL, cntl, &request, &response, NULL);
      if (!cntl->Failed()) {
          size_t body_len  = response.head.body_len;
          uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
          const size_t k = response.body.copy_to(thrift_buffer, body_len);
          if ( k != body_len) {
              free(thrift_buffer);
              cntl->SetFailed("copy response buf failed!");
              return;
          }
          in_buffer_->resetBuffer(thrift_buffer, body_len);
      }
      return;
  }

private:
    boost::shared_ptr<T> client_;
    boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> out_buffer_;
    boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> in_buffer_;
    boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> in_;
    boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> out_;

};


bool brpc_thrift_server_helper(const brpc::ThriftBinaryMessage& request,
                              brpc::ThriftBinaryMessage* response,
                              boost::shared_ptr<apache::thrift::TDispatchProcessor> processor) {
    boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> in_buffer(
        new apache::thrift::transport::TMemoryBuffer());
    boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> in(
        new apache::thrift::protocol::TBinaryProtocol(in_buffer));

    boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> out_buffer(
        new apache::thrift::transport::TMemoryBuffer());
    boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> out(
        new apache::thrift::protocol::TBinaryProtocol(out_buffer));

    // Cut the thrift buffer and parse thrift message
    size_t body_len  = request.head.body_len;
    uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);

    const size_t k = request.body.copy_to(thrift_buffer, body_len);
    if ( k != body_len) {
        free(thrift_buffer);
        return false;
    }

    in_buffer->resetBuffer(thrift_buffer, body_len);

    if (processor->process(in, out, NULL)) {
        butil::IOBuf buf;
        std::string s = out_buffer->getBufferAsString();
        buf.append(s);
        response->body = buf;
    } else {
        return false;
    }
    return true;
}

