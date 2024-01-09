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

// A thrift client sending requests to server every 1 second.

#include <gflags/gflags.h>
#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

#include <butil/logging.h>

// _THRIFT_STDCXX_H_ is defined by thrift/stdcxx.h which was added since thrift 0.11.0
// but deprecated after 0.13.0
#ifndef THRIFT_STDCXX
 #if defined(_THRIFT_STDCXX_H_)
 # define THRIFT_STDCXX apache::thrift::stdcxx
 #elif defined(_THRIFT_VERSION_LOWER_THAN_0_11_0_)
 # define THRIFT_STDCXX boost
 # include <boost/make_shared.hpp>
 #else
 # define THRIFT_STDCXX std
 #endif
#endif

DEFINE_string(server, "0.0.0.0", "IP Address of server");
DEFINE_int32(port, 8019, "Port of server");

int main(int argc, char **argv) {

    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    THRIFT_STDCXX::shared_ptr<apache::thrift::transport::TSocket> socket(
        new apache::thrift::transport::TSocket(FLAGS_server, FLAGS_port));
    THRIFT_STDCXX::shared_ptr<apache::thrift::transport::TTransport> transport(
        new apache::thrift::transport::TFramedTransport(socket));
    THRIFT_STDCXX::shared_ptr<apache::thrift::protocol::TProtocol> protocol(
        new apache::thrift::protocol::TBinaryProtocol(transport));

    example::EchoServiceClient client(protocol);
    transport->open();

    example::EchoRequest req;
    req.__set_data("hello");
    req.__set_need_by_proxy(10);

    example::EchoResponse res;

    while (1) {
        try {
            client.Echo(res, req);
            LOG(INFO) << "Req=" << req << " Res=" << res;
        } catch (std::exception& e) {
            LOG(ERROR) << "Fail to rpc, " << e.what();
        }
        sleep(1);
    }
    transport->close();

    return 0;
}
