// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Dec 23 15:53:51 2014

#ifndef BRPC_CONTROLLER_PRIVATE_ACCESSOR_H
#define BRPC_CONTROLLER_PRIVATE_ACCESSOR_H

// This is an rpc-internal file.

#include "brpc/socket.h"
#include "brpc/controller.h"
#include "brpc/stream.h"

namespace google {
namespace protobuf {
class Message;
}
}


namespace brpc {

class AuthContext;

// A wrapper to access some private methods/fields of `Controller'
// This is supposed to be used by internal RPC protocols ONLY
class ControllerPrivateAccessor {
public:
    explicit ControllerPrivateAccessor(Controller* cntl) {
        _cntl = cntl;
    }

    void OnResponse(CallId id, int saved_error) {
        const Controller::CompletionInfo info = { id, true };
        _cntl->OnVersionedRPCReturned(info, false, saved_error);
    }

    BAIDU_DEPRECATED google::protobuf::Message* response() const {
        return _cntl->response();
    }

    ConnectionType connection_type() const {
        return _cntl->_connection_type;
    }

    int current_retry_count() const {
        return _cntl->_current_call.nretry;
    }
    
    void set_socket_correlation_id(uint64_t id) {
        _cntl->_current_call.sending_sock->set_correlation_id(id);
    }

    ControllerPrivateAccessor &set_peer_id(SocketId peer_id) {
        _cntl->_current_call.peer_id = peer_id;
        return *this;
    }

    Socket* get_sending_sock() {
        return _cntl->_current_call.sending_sock.get();
    }

    void move_in_server_receiving_sock(SocketUniquePtr& ptr) {
        CHECK(_cntl->_current_call.sending_sock == NULL);
        _cntl->_current_call.sending_sock.reset(ptr.release());
    }
    
    ControllerPrivateAccessor &set_security_mode(bool security_mode) {
        _cntl->set_flag(Controller::FLAGS_SECURITY_MODE, security_mode);
        return *this;
    }

    ControllerPrivateAccessor &set_remote_side(const base::EndPoint& pt) {
        _cntl->_remote_side = pt;
        return *this;
    }

    ControllerPrivateAccessor &set_local_side(const base::EndPoint& pt) {
        _cntl->_local_side = pt;
        return *this;
    }
 
    ControllerPrivateAccessor &set_auth_context(const AuthContext* ctx) {
        _cntl->set_auth_context(ctx);
        return *this;
    }

    ControllerPrivateAccessor &set_span(Span* span) {
        _cntl->_span = span;
        return *this;
    }
    
    ControllerPrivateAccessor &set_request_protocol(ProtocolType protocol) {
        _cntl->_request_protocol = protocol;
        return *this;
    }
    
    Span* span() const { return _cntl->_span; }

    uint32_t pipelined_count() const { return _cntl->_pipelined_count; }
    void set_pipelined_count(uint32_t count) {  _cntl->_pipelined_count = count; }

    ControllerPrivateAccessor& set_server(const Server* server) {
        _cntl->_server = server;
        return *this;
    }

    // Pass the owership of |settings| to _cntl, while is going to be
    // destroyed in Controller::Reset()
    void set_remote_stream_settings(StreamSettings *settings) {
        _cntl->_remote_stream_settings = settings;
    }
    StreamSettings* remote_stream_settings() {
        return _cntl->_remote_stream_settings;
    }

    StreamId request_stream() { return _cntl->_request_stream; }
    StreamId response_stream() { return _cntl->_response_stream; }

    void set_method(const google::protobuf::MethodDescriptor* method) 
    { _cntl->_method = method; }

    void set_readable_progressive_attachment(ReadableProgressiveAttachment* s)
    { _cntl->_rpa.reset(s); }

private:
    Controller* _cntl;
};

// Inherit this class to intercept Controller::IssueRPC. This is an internal
// utility only useable by baidu-rpc developers.
class RPCSender {
public:
    virtual ~RPCSender() {}
    virtual int IssueRPC(int64_t start_realtime_us) = 0;
};

} // namespace brpc


#endif // BRPC_CONTROLLER_PRIVATE_ACCESSOR_H
