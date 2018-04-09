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

// utils for serilize/deserilize thrift binary message to brpc protobuf obj.

#ifndef BRPC_THRIFT_UTILS_H
#define BRPC_THRIFT_UTILS_H

#include <boost/make_shared.hpp>

#include <brpc/channel.h>
#include <brpc/thrift_framed_message.h>

#include <thrift/TDispatchProcessor.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

namespace brpc {

bool brpc_thrift_server_helper(const brpc::ThriftFramedMessage& request,
                      brpc::ThriftFramedMessage* response,
                      boost::shared_ptr<::apache::thrift::TDispatchProcessor> processor) {

    auto in_buffer =
        boost::make_shared<apache::thrift::transport::TMemoryBuffer>();
    auto in_portocol =
        boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(in_buffer);

    auto out_buffer =
        boost::make_shared<apache::thrift::transport::TMemoryBuffer>();
    auto out_portocol =
        boost::make_shared<apache::thrift::protocol::TBinaryProtocol>(out_buffer);

    // Cut the thrift buffer and parse thrift message
    size_t body_len  = request.head.body_len;
    auto thrift_buffer = static_cast<uint8_t*>(new uint8_t[body_len]);

    const size_t k = request.body.copy_to(thrift_buffer, body_len);
    if ( k != body_len) {
        delete [] thrift_buffer;
        return false;
    }

    in_buffer->resetBuffer(thrift_buffer, body_len);

    if (processor->process(in_portocol, out_portocol, NULL)) {
        response->body.append(out_buffer->getBufferAsString());
    } else {
        delete [] thrift_buffer;
        return false;
    }

    delete [] thrift_buffer;
    return true;
}

}

#endif //BRPC_THRIFT_UTILS_H
