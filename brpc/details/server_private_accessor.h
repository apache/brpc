// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Jan 13 16:09:25 CST 2015

#ifndef BRPC_SERVER_PRIVATE_ACCESSOR_H
#define BRPC_SERVER_PRIVATE_ACCESSOR_H

#include <google/protobuf/descriptor.h>
#include "brpc/server.h"
#include "brpc/acceptor.h"
#include "brpc/details/method_status.h"
#include "brpc/bad_method_service.h"
#include "brpc/restful.h"


namespace brpc {

// A wrapper to access some private methods/fields of `Server'
// This is supposed to be used by internal RPC protocols ONLY
class ServerPrivateAccessor {
public:
    explicit ServerPrivateAccessor(const Server* svr) {
        CHECK(svr);
        _server = svr;
    }

    void AddError() {
        _server->_nerror << 1;
    }

    // Returns true iff the `max_concurrency' limit is not reached.
    bool AddConcurrency(Controller* c) {
        if (_server->options().max_concurrency <= 0) {
            return true;
        }
        if (base::subtle::NoBarrier_AtomicIncrement(&_server->_concurrency, 1)
            <= _server->options().max_concurrency) {
            c->add_flag(Controller::FLAGS_ADDED_CONCURRENCY);
            return true;
        }
        base::subtle::NoBarrier_AtomicIncrement(&_server->_concurrency, -1);
        return false;
    }

    // Remove the increment of AddConcurrency(). Must not be called when
    // AddConcurrency() returned false.
    void RemoveConcurrency(const Controller* c) {
        if (c->has_flag(Controller::FLAGS_ADDED_CONCURRENCY)) {
            base::subtle::NoBarrier_AtomicIncrement(&_server->_concurrency, -1);
        }
    }

    // Find by MethodDescriptor::full_name
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const base::StringPiece &fullname) {
        return _server->FindMethodPropertyByFullName(fullname);
    }
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const base::StringPiece& fullname) const {
        return _server->FindMethodPropertyByFullName(fullname);
    }
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const base::StringPiece& full_service_name,
                                 const base::StringPiece& method_name) const {
        return _server->FindMethodPropertyByFullName(
            full_service_name, method_name);
    }
    const Server::MethodProperty* FindMethodPropertyByNameAndIndex(
        const base::StringPiece& service_name, int method_index) const {
        return _server->FindMethodPropertyByNameAndIndex(service_name, method_index);
    }

    const Server::ServiceProperty*
    FindServicePropertyByFullName(const base::StringPiece& fullname) const {
        return _server->FindServicePropertyByFullName(fullname);
    }

    const Server::ServiceProperty*
    FindServicePropertyByName(const base::StringPiece& name) const {
        return _server->FindServicePropertyByName(name);
    }

    const Server::ServiceProperty*
    FindServicePropertyAdaptively(const base::StringPiece& service_name) const {
        if (service_name.find('.') == base::StringPiece::npos) {
            return _server->FindServicePropertyByName(service_name);
        } else {
            return _server->FindServicePropertyByFullName(service_name);
        }
    }

    Acceptor* acceptor() const { return _server->_am; }

    RestfulMap* global_restful_map() const
    { return _server->_global_restful_map; }
    
private:
    const Server* _server;
};

// Count one error if release() is not called before destruction of this object.
class ScopedNonServiceError {
public:
    ScopedNonServiceError(const Server* server) : _server(server) {}
    ~ScopedNonServiceError() {
        if (_server) {
            ServerPrivateAccessor(_server).AddError();
            _server = NULL;
        }
    }
    const Server* release() {
        const Server* tmp = _server;
        _server = NULL;
        return tmp;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(ScopedNonServiceError);
    const Server* _server;
};

class ScopedRemoveConcurrency {
public:
    ScopedRemoveConcurrency(const Server* server, const Controller* c)
        : _server(server), _cntl(c) {}
    ~ScopedRemoveConcurrency() {
        ServerPrivateAccessor(_server).RemoveConcurrency(_cntl);
    }
private:
    DISALLOW_COPY_AND_ASSIGN(ScopedRemoveConcurrency);
    const Server* _server;
    const Controller* _cntl;
};


} // namespace brpc


#endif // BRPC_SERVER_PRIVATE_ACCESSOR_H
