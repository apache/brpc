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

#ifndef BRPC_SERVER_PRIVATE_ACCESSOR_H
#define BRPC_SERVER_PRIVATE_ACCESSOR_H

#include <google/protobuf/descriptor.h>
#include "brpc/server.h"
#include "brpc/acceptor.h"
#include "brpc/details/method_status.h"
#include "brpc/builtin/bad_method_service.h"
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
        _server->_nerror_bvar << 1;
    }

    // Returns true if the `max_concurrency' limit is not reached.
    bool AddConcurrency(Controller* c) {
        if (_server->options().max_concurrency <= 0) {
            return true;
        }
        c->add_flag(Controller::FLAGS_ADDED_CONCURRENCY);
        return (butil::subtle::NoBarrier_AtomicIncrement(&_server->_concurrency, 1)
                <= _server->options().max_concurrency);
    }

    void RemoveConcurrency(const Controller* c) {
        if (c->has_flag(Controller::FLAGS_ADDED_CONCURRENCY)) {
            butil::subtle::NoBarrier_AtomicIncrement(&_server->_concurrency, -1);
        }
    }

    // Find by MethodDescriptor::full_name
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const butil::StringPiece &fullname) {
        return _server->FindMethodPropertyByFullName(fullname);
    }
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const butil::StringPiece& fullname) const {
        return _server->FindMethodPropertyByFullName(fullname);
    }
    const Server::MethodProperty*
    FindMethodPropertyByFullName(const butil::StringPiece& full_service_name,
                                 const butil::StringPiece& method_name) const {
        return _server->FindMethodPropertyByFullName(
            full_service_name, method_name);
    }
    const Server::MethodProperty* FindMethodPropertyByNameAndIndex(
        const butil::StringPiece& service_name, int method_index) const {
        return _server->FindMethodPropertyByNameAndIndex(service_name, method_index);
    }

    const Server::ServiceProperty*
    FindServicePropertyByFullName(const butil::StringPiece& fullname) const {
        return _server->FindServicePropertyByFullName(fullname);
    }

    const Server::ServiceProperty*
    FindServicePropertyByName(const butil::StringPiece& name) const {
        return _server->FindServicePropertyByName(name);
    }

    const Server::ServiceProperty*
    FindServicePropertyAdaptively(const butil::StringPiece& service_name) const {
        if (service_name.find('.') == butil::StringPiece::npos) {
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

// Reject accesses to builtin services when the server is in security mode,
// in which case they are only reachable from ServerOptions.internal_port.
// Returns true if the access was rejected, in which case `cntl` was already
// SetFailed() and the caller must stop dispatching the request immediately.
// NOTE: Call this after ControllerPrivateAccessor::set_security_mode() and
// before the method is counted by MethodStatus::OnRequested(), so that
// rejected accesses do not pollute the stats of the method. `mp` may point
// to BadMethodService which is builtin as well and lists the methods of the
// requested service, so protocols dispatching to BadMethodService must call
// this beforehand, or make sure the listing is hidden in security mode.
inline bool RejectBuiltinAccess(Controller* cntl, const Server& server,
                                const Server::MethodProperty* mp) {
    if (!cntl->is_security_mode() ||
        (!mp->is_builtin_service && !mp->params.is_tabbed)) {
        return false;
    }
    cntl->SetFailed(EPERM, "Not allowed to access builtin services, try "
                                    "ServerOptions.internal_port=%d instead if you're in "
                                    "internal network",
                    server.options().internal_port);
    return true;
}

// Count one error if release() is not called before destruction of this object.
class ScopedNonServiceError {
public:
    ScopedNonServiceError(const Server* server) : _server(server) {}
    ~ScopedNonServiceError() {
        if (_server) {
            ServerPrivateAccessor(_server).AddError();
            _server = nullptr;
        }
    }
    const Server* release() {
        const Server* tmp = _server;
        _server = nullptr;
        return tmp;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(ScopedNonServiceError);
    const Server* _server;
};

} // namespace brpc


#endif // BRPC_SERVER_PRIVATE_ACCESSOR_H
