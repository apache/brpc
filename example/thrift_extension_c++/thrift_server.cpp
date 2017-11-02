#include "gen-cpp/EchoService.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/server/TNonblockingServer.h>
#include <thrift/concurrency/PosixThreadFactory.h>

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;
using namespace apache::thrift::concurrency;
using boost::shared_ptr;

using namespace example;

class EchoServiceHandler : virtual public EchoServiceIf {
public:
	EchoServiceHandler() {}

	void Echo(EchoResponse& res, const EchoRequest& req) {
		res.data = req.data + " world";
		return;
	}

};

int main(int argc, char **argv) {
	int port = 8019;

    shared_ptr<EchoServiceHandler> handler(new EchoServiceHandler());  
    shared_ptr<PosixThreadFactory> thread_factory(
        new PosixThreadFactory(PosixThreadFactory::ROUND_ROBIN,
                               PosixThreadFactory::NORMAL, 1, false) );

    shared_ptr<TProcessor> processor(new EchoServiceProcessor(handler));
    shared_ptr<TProtocolFactory> protocol_factory(new TBinaryProtocolFactory());
    shared_ptr<TTransportFactory> transport_factory(new TBufferedTransportFactory());
    shared_ptr<ThreadManager> thread_mgr(ThreadManager::newSimpleThreadManager(2));
    thread_mgr->threadFactory(thread_factory);

    thread_mgr->start();

    TNonblockingServer server(processor,
        transport_factory, transport_factory, protocol_factory,
        protocol_factory, port, thread_mgr);


    server.serve();  
	return 0;
}
