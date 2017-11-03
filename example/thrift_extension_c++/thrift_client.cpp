#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

#include <iostream>

int main(int argc, char **argv) {
  boost::shared_ptr<apache::thrift::transport::TSocket> socket(new apache::thrift::transport::TSocket("127.0.0.1", 8019));
  boost::shared_ptr<apache::thrift::transport::TTransport> transport(new apache::thrift::transport::TFramedTransport(socket));
  boost::shared_ptr<apache::thrift::protocol::TProtocol> protocol(new apache::thrift::protocol::TBinaryProtocol(transport));

  example::EchoServiceClient client(protocol);
  transport->open();
  
  example::EchoRequest req;
  req.data = "hello";

  example::EchoResponse res;
  client.Echo(res, req);
  
  std::cout << "Res: " << res.data << std::endl;

  transport->close();

  return 0;
}
