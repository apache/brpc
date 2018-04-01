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

#include "thrift_brpc_helper_transport.h"

#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>


template <typename T>
boost::shared_ptr<T> InitThriftClient(brpc::Channel* channel,
    apache::thrift::transport::TThriftBrpcHelperTransport** transport) {
    auto thrift_brpc_transport = 
        boost::make_shared<apache::thrift::transport::TThriftBrpcHelperTransport>();

    thrift_brpc_transport->set_channel(channel);
    *transport = thrift_brpc_transport.get();

    auto out = boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(thrift_brpc_transport);
    auto in = boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(thrift_brpc_transport);

    return boost::make_shared<T>(in, out);
}

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
    //std::shared_ptr<uint8_t> thrift_buffer(new uint8_t[10],std::default_delete<uint8_t[]>()); 
    uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
    const size_t k = request.body.copy_to(thrift_buffer, body_len);
    if ( k != body_len) {
        free(thrift_buffer);
        return false;
    }

    in_buffer->resetBuffer(thrift_buffer, body_len);

    if (processor->process(in, out, NULL)) {
        response->body.append(out_buffer->getBufferAsString());
    } else {
    
        free(thrift_buffer);
        return false;
    }
    free(thrift_buffer);

    return true;
}

