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

// A thrift server to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>

#include <butil/logging.h>

#include "gen-cpp/EchoService.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/server/TNonblockingServer.h>
#include <thrift/concurrency/PosixThreadFactory.h>


DEFINE_int32(port, 8019, "Port of server");

class EchoServiceHandler : virtual public example::EchoServiceIf {
public:
	EchoServiceHandler() {}

	void Echo(example::EchoResponse& res, const example::EchoRequest& req) {
        // Process request, just attach a simple string.
		res.data = req.data + " world";
		return;
	}

};

int main(int argc, char *argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    boost::shared_ptr<EchoServiceHandler> handler(new EchoServiceHandler());  
    boost::shared_ptr<apache::thrift::concurrency::PosixThreadFactory> thread_factory(
        new apache::thrift::concurrency::PosixThreadFactory(
            apache::thrift::concurrency::PosixThreadFactory::ROUND_ROBIN,
            apache::thrift::concurrency::PosixThreadFactory::NORMAL, 1, false));

    boost::shared_ptr<apache::thrift::server::TProcessor> processor(
		new example::EchoServiceProcessor(handler));
    boost::shared_ptr<apache::thrift::protocol::TProtocolFactory> protocol_factory(
        new apache::thrift::protocol::TBinaryProtocolFactory());
    boost::shared_ptr<apache::thrift::transport::TTransportFactory> transport_factory(
        new apache::thrift::transport::TBufferedTransportFactory());
    boost::shared_ptr<apache::thrift::concurrency::ThreadManager> thread_mgr(
        apache::thrift::concurrency::ThreadManager::newSimpleThreadManager(2));
    thread_mgr->threadFactory(thread_factory);

    thread_mgr->start();

    apache::thrift::server::TNonblockingServer server(processor,
        transport_factory, transport_factory, protocol_factory,
        protocol_factory, FLAGS_port, thread_mgr);

    server.serve();  
	return 0;
}
