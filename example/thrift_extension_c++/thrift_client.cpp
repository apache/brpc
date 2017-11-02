#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

#include <iostream>

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

using namespace example;

int main(int argc, char **argv) {
  boost::shared_ptr<TSocket> socket(new TSocket("127.0.0.1", 8019));
  boost::shared_ptr<TTransport> transport(new TFramedTransport(socket));
  boost::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));

  EchoServiceClient client(protocol);
  transport->open();
  
  EchoRequest req;
  req.data = "hello";

  EchoResponse res;
  client.Echo(res, req);
  
  std::cout << "Res: " << res.data << std::endl;

  transport->close();

  return 0;
}
