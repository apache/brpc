#ifndef BRPC_POLICY_ESP_PROTOCOL_H
#define BRPC_POLICY_ESP_PROTOCOL_H

#include <unistd.h>
#include <sys/types.h>

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

ParseResult ParseEspMessage(
        base::IOBuf* source, 
        Socket* socket, 
        bool read_eof, 
        const void *arg);

void SerializeEspRequest(
        base::IOBuf* request_buf, 
        Controller* controller,
        const google::protobuf::Message* request);

void PackEspRequest(base::IOBuf* packet_buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor*,
                    Controller* controller,
                    const base::IOBuf&,
                    const Authenticator*);

void ProcessEspResponse(InputMessageBase* msg);

} // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_ESP_PROTOCOL_H

