// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/27 15:37:11

#ifndef BRPC_POLICY_HTTP_RPC_PROTOCOL_H
#define BRPC_POLICY_HTTP_RPC_PROTOCOL_H

#include "brpc/details/http_message.h"         // HttpMessage
#include "brpc/input_messenger.h"              // InputMessenger
#include "brpc/protocol.h" 


namespace brpc {
namespace policy {

// Used in UT.
class HttpInputMessage : public ReadableProgressiveAttachment
                       , public InputMessageBase
                       , public HttpMessage {
public:
    HttpInputMessage(bool read_body_progressively = false)
        : InputMessageBase()
        , HttpMessage(read_body_progressively)
        , _is_stage2(false) {
        // add one ref for Destroy
        base::intrusive_ptr<HttpInputMessage>(this).detach();
    }

    void AddOneRefForStage2() {
        base::intrusive_ptr<HttpInputMessage>(this).detach();
        _is_stage2 = true;
    }

    void RemoveOneRefForStage2() {
        base::intrusive_ptr<HttpInputMessage>(this, false);
    }

    // True if AddOneRefForStage2() was ever called.
    bool is_stage2() const { return _is_stage2; }

    // @InputMessageBase
    void DestroyImpl() {
        RemoveOneRefForStage2();
    }

    // @ReadableProgressiveAttachment
    void ReadProgressiveAttachmentBy(ProgressiveReader* r) {
        return SetBodyReader(r);
    }
    
private:
    bool _is_stage2;
};

ParseResult ParseHttpMessage(base::IOBuf *source, Socket *socket, bool read_eof,
                             const void *arg);

void ProcessHttpRequest(InputMessageBase *msg);

// Actions to a (server) response in http
void ProcessHttpResponse(InputMessageBase* msg);

// Verify authentication information in http.
bool VerifyHttpRequest(const InputMessageBase* msg);

void SerializeHttpRequest(base::IOBuf* request_buf,
                          Controller* cntl,
                          const google::protobuf::Message* msg);

// Serialize `request' into `buf' in http. `correlation_id',
// `controller', `method' provide needed information.
void PackHttpRequest(base::IOBuf* buf,
                     SocketMessage** user_message_out,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* controller,
                     const base::IOBuf& request,
                     const Authenticator* auth);

bool ParseHttpServerAddress(base::EndPoint* out, const char* server_addr_and_port);

const std::string& GetHttpMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*);

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_HTTP_RPC_PROTOCOL_H
