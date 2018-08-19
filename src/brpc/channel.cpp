// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)
//          Rujie Jiang(jiangrujie@baidu.com)
//          Zhangyi Chen(chenzhangyi01@baidu.com)

#include <inttypes.h>
#include <google/protobuf/descriptor.h>
#include <gflags/gflags.h>
#include "butil/time.h"                              // milliseconds_from_now
#include "butil/logging.h"
#include "bthread/unstable.h"                        // bthread_timer_add
#include "brpc/socket_map.h"                         // SocketMapInsert
#include "brpc/compress.h"
#include "brpc/global.h"
#include "brpc/span.h"
#include "brpc/details/load_balancer_with_naming.h"
#include "brpc/controller.h"
#include "brpc/channel.h"
#include "brpc/details/usercode_backup_pool.h"       // TooManyUserCode
#include "brpc/policy/esp_authenticator.h"


namespace brpc {

DECLARE_bool(enable_rpcz);
DECLARE_bool(usercode_in_pthread);

ChannelOptions::ChannelOptions()
    : connect_timeout_ms(200)
    , timeout_ms(500)
    , backup_request_ms(-1)
    , max_retry(3)
    , protocol(PROTOCOL_BAIDU_STD)
    , connection_type(CONNECTION_TYPE_UNKNOWN)
    , succeed_without_server(true)
    , log_succeed_without_server(true)
    , auth(NULL)
    , retry_policy(NULL)
    , ns_filter(NULL)
{}

Channel::Channel(ProfilerLinker)
    : _server_id((SocketId)-1)
    , _serialize_request(NULL)
    , _pack_request(NULL)
    , _get_method_name(NULL)
    , _preferred_index(-1) {
}

Channel::~Channel() {
    if (_server_id != (SocketId)-1) {
        SocketMapRemove(SocketMapKey(_server_address,
                                     _options.ssl_options,
                                     _options.auth));
    }
}

int Channel::InitChannelOptions(const ChannelOptions* options) {
    if (options) {  // Override default options if user provided one.
        _options = *options;
    }
    const Protocol* protocol = FindProtocol(_options.protocol);
    if (NULL == protocol || !protocol->support_client()) {
        if (_options.protocol == PROTOCOL_UNKNOWN) {
            LOG(ERROR) << "Unknown protocol";
        } else {
            LOG(ERROR) << "Channel doesn't support protocol="
                       << _options.protocol.name();
        }
        return -1;
    }
    _serialize_request = protocol->serialize_request;
    _pack_request = protocol->pack_request;
    _get_method_name = protocol->get_method_name;

    // Check connection_type
    if (_options.connection_type == CONNECTION_TYPE_UNKNOWN) {
        // Save has_error which will be overriden in later assignments to
        // connection_type.
        const bool has_error = _options.connection_type.has_error();
        
        if (protocol->supported_connection_type & CONNECTION_TYPE_SINGLE) {
            _options.connection_type = CONNECTION_TYPE_SINGLE;
        } else if (protocol->supported_connection_type & CONNECTION_TYPE_POOLED) {
            _options.connection_type = CONNECTION_TYPE_POOLED;
        } else {
            _options.connection_type = CONNECTION_TYPE_SHORT;
        }
        if (has_error) {
            LOG(ERROR) << "Channel=" << this << " chose connection_type="
                       << _options.connection_type.name() << " for protocol="
                       << _options.protocol.name();
        }
    } else {
        if (!(_options.connection_type & protocol->supported_connection_type)) {
            LOG(ERROR) << protocol->name << " does not support connection_type="
                       << ConnectionTypeToString(_options.connection_type);
            return -1;
        }
    }

    _preferred_index = get_client_side_messenger()->FindProtocolIndex(_options.protocol);
    if (_preferred_index < 0) {
        LOG(ERROR) << "Fail to get index for protocol="
                   << _options.protocol.name();
        return -1;
    }

    if (_options.protocol == PROTOCOL_ESP) {
        if (_options.auth == NULL) {
            _options.auth = policy::global_esp_authenticator();
        }
    } else if (_options.protocol == brpc::PROTOCOL_HTTP) {
        if (_raw_server_address.compare(0, 5, "https") == 0) {
            _options.ssl_options.enable = true;
            if (_options.ssl_options.sni_name.empty()) {
                int port;
                ParseHostAndPortFromURL(_raw_server_address.c_str(),
                                        &_options.ssl_options.sni_name, &port);
            }
        }
    }

    return 0;
}

int Channel::Init(const char* server_addr_and_port,
                  const ChannelOptions* options) {
    GlobalInitializeOrDie();
    butil::EndPoint point;
    const Protocol* protocol =
        FindProtocol(options ? options->protocol : _options.protocol);
    if (protocol != NULL && protocol->parse_server_address != NULL) {
        if (!protocol->parse_server_address(&point, server_addr_and_port)) {
            LOG(ERROR) << "Fail to parse address=`" << server_addr_and_port << '\'';
            return -1;
        }
    } else {
        if (str2endpoint(server_addr_and_port, &point) != 0 &&
            hostname2endpoint(server_addr_and_port, &point) != 0) {
            // Many users called the wrong Init(). Print some log to save
            // our troubleshooting time.
            if (strstr(server_addr_and_port, "://")) {
                LOG(ERROR) << "Invalid address=`" << server_addr_and_port
                           << "'. Use Init(naming_service_name, "
                    "load_balancer_name, options) instead.";
            } else {
                LOG(ERROR) << "Invalid address=`" << server_addr_and_port << '\'';
            }
            return -1;
        }
    }
    _raw_server_address.assign(server_addr_and_port);
    return Init(point, options);
}

int Channel::Init(const char* server_addr, int port,
                  const ChannelOptions* options) {
    GlobalInitializeOrDie();
    butil::EndPoint point;
    const Protocol* protocol =
        FindProtocol(options ? options->protocol : _options.protocol);
    if (protocol != NULL && protocol->parse_server_address != NULL) {
        if (!protocol->parse_server_address(&point, server_addr)) {
            LOG(ERROR) << "Fail to parse address=`" << server_addr << '\'';
            return -1;
        }
        point.port = port;
    } else {
        if (str2endpoint(server_addr, port, &point) != 0 &&
            hostname2endpoint(server_addr, port, &point) != 0) {
            LOG(ERROR) << "Invalid address=`" << server_addr << '\'';
            return -1;
        }
    }
    _raw_server_address.assign(server_addr);
    return Init(point, options);
}

int Channel::Init(butil::EndPoint server_addr_and_port,
                  const ChannelOptions* options) {
    GlobalInitializeOrDie();
    if (InitChannelOptions(options) != 0) {
        return -1;
    }
    const int port = server_addr_and_port.port;
    if (port < 0 || port > 65535) {
        LOG(ERROR) << "Invalid port=" << port;
        return -1;
    }
    _server_address = server_addr_and_port;
    if (SocketMapInsert(SocketMapKey(server_addr_and_port,
                                     _options.ssl_options,
                                     _options.auth), &_server_id) != 0) {
        LOG(ERROR) << "Fail to insert into SocketMap";
        return -1;
    }
    return 0;
}

int Channel::Init(const char* ns_url,
                  const char* lb_name,
                  const ChannelOptions* options) {
    if (lb_name == NULL || *lb_name == '\0') {
        // Treat ns_url as server_addr_and_port
        return Init(ns_url, options);
    }
    GlobalInitializeOrDie();
    if (InitChannelOptions(options) != 0) {
        return -1;
    }
    LoadBalancerWithNaming* lb = new (std::nothrow) LoadBalancerWithNaming;
    if (NULL == lb) {
        LOG(FATAL) << "Fail to new LoadBalancerWithNaming";
        return -1;        
    }
    GetNamingServiceThreadOptions ns_opt;
    ns_opt.succeed_without_server = _options.succeed_without_server;
    ns_opt.log_succeed_without_server = _options.log_succeed_without_server;
    if (lb->Init(ns_url, lb_name, _options.ns_filter, &ns_opt) != 0) {
        LOG(ERROR) << "Fail to initialize LoadBalancerWithNaming";
        delete lb;
        return -1;
    }
    _lb.reset(lb);
    return 0;
}

static void HandleTimeout(void* arg) {
    bthread_id_t correlation_id = { (uint64_t)arg };
    bthread_id_error(correlation_id, ERPCTIMEDOUT);
}

static void HandleBackupRequest(void* arg) {
    bthread_id_t correlation_id = { (uint64_t)arg };
    bthread_id_error(correlation_id, EBACKUPREQUEST);
}

void Channel::CallMethod(const google::protobuf::MethodDescriptor* method,
                         google::protobuf::RpcController* controller_base,
                         const google::protobuf::Message* request,
                         google::protobuf::Message* response,
                         google::protobuf::Closure* done) {
    const int64_t start_send_real_us = butil::gettimeofday_us();
    Controller* cntl = static_cast<Controller*>(controller_base);
    cntl->OnRPCBegin(start_send_real_us);
    // Override max_retry first to reset the range of correlation_id
    if (cntl->max_retry() == UNSET_MAGIC_NUM) {
        cntl->set_max_retry(_options.max_retry);
    }
    if (cntl->max_retry() < 0) {
        // this is important because #max_retry decides #versions allocated
        // in correlation_id. negative max_retry causes undefined behavior.
        cntl->set_max_retry(0);
    }
    // HTTP needs this field to be set before any SetFailed()
    cntl->_request_protocol = _options.protocol;
    cntl->_preferred_index = _preferred_index;
    cntl->_retry_policy = _options.retry_policy;
    const CallId correlation_id = cntl->call_id();
    const int rc = bthread_id_lock_and_reset_range(
                    correlation_id, NULL, 2 + cntl->max_retry());
    if (rc != 0) {
        CHECK_EQ(EINVAL, rc);
        if (!cntl->FailedInline()) {
            cntl->SetFailed(EINVAL, "Fail to lock call_id=%" PRId64,
                            correlation_id.value);
        }
        LOG_IF(ERROR, cntl->is_used_by_rpc())
            << "Controller=" << cntl << " was used by another RPC before. "
            "Did you forget to Reset() it before reuse?";
        // Have to run done in-place. If the done runs in another thread,
        // Join() on this RPC is no-op and probably ends earlier than running
        // the callback and releases resources used in the callback.
        // Since this branch is only entered by wrongly-used RPC, the
        // potentially introduced deadlock(caused by locking RPC and done with
        // the same non-recursive lock) is acceptable and removable by fixing
        // user's code.
        if (done) {
            done->Run();
        }
        return;
    }
    cntl->set_used_by_rpc();

    if (cntl->_sender == NULL && IsTraceable(Span::tls_parent())) {
        const int64_t start_send_us = butil::cpuwide_time_us();
        const std::string* method_name = NULL;
        if (_get_method_name) {
            method_name = &_get_method_name(method, cntl);
        } else if (method) {
            method_name = &method->full_name();
        } else {
            const static std::string NULL_METHOD_STR = "null-method";
            method_name = &NULL_METHOD_STR;
        }
        Span* span = Span::CreateClientSpan(
            *method_name, start_send_real_us - start_send_us);
        span->set_log_id(cntl->log_id());
        span->set_base_cid(correlation_id);
        span->set_protocol(_options.protocol);
        span->set_start_send_us(start_send_us);
        cntl->_span = span;
    }
    // Override some options if they haven't been set by Controller
    if (cntl->timeout_ms() == UNSET_MAGIC_NUM) {
        cntl->set_timeout_ms(_options.timeout_ms);
    }
    // Since connection is shared extensively amongst channels and RPC,
    // overriding connect_timeout_ms does not make sense, just use the
    // one in ChannelOptions
    cntl->_connect_timeout_ms = _options.connect_timeout_ms;
    if (cntl->backup_request_ms() == UNSET_MAGIC_NUM) {
        cntl->set_backup_request_ms(_options.backup_request_ms);
    }
    if (cntl->connection_type() == CONNECTION_TYPE_UNKNOWN) {
        cntl->set_connection_type(_options.connection_type);
    }
    cntl->_response = response;
    cntl->_done = done;
    cntl->_pack_request = _pack_request;
    cntl->_method = method;
    cntl->_auth = _options.auth;

    if (SingleServer()) {
        cntl->_single_server_id = _server_id;
        cntl->_remote_side = _server_address;
    }

    // Share the lb with controller.
    cntl->_lb = _lb;

    if (FLAGS_usercode_in_pthread &&
        done != NULL &&
        TooManyUserCode()) {
        cntl->SetFailed(ELIMIT, "Too many user code to run when "
                        "-usercode_in_pthread is on");
        return cntl->HandleSendFailed();
    }
    if (cntl->FailedInline()) {
        // probably failed before RPC, not called until all necessary
        // parameters in `cntl' are set.
        return cntl->HandleSendFailed();
    }
    _serialize_request(&cntl->_request_buf, cntl, request);
    if (cntl->FailedInline()) {
        return cntl->HandleSendFailed();
    }

    if (cntl->_request_stream != INVALID_STREAM_ID) {
        // Currently we cannot handle retry and backup request correctly
        cntl->set_max_retry(0);
        cntl->set_backup_request_ms(-1);
    }

    if (cntl->backup_request_ms() >= 0 &&
        (cntl->backup_request_ms() < cntl->timeout_ms() ||
         cntl->timeout_ms() < 0)) {
        // Setup timer for backup request. When it occurs, we'll setup a
        // timer of timeout_ms before sending backup request.

        // _abstime_us is for truncating _connect_timeout_ms and resetting
        // timer when EBACKUPREQUEST occurs.
        if (cntl->timeout_ms() < 0) {
            cntl->_abstime_us = -1;
        } else {
            cntl->_abstime_us = cntl->timeout_ms() * 1000L + start_send_real_us;
        }
        const int rc = bthread_timer_add(
            &cntl->_timeout_id,
            butil::microseconds_to_timespec(
                cntl->backup_request_ms() * 1000L + start_send_real_us),
            HandleBackupRequest, (void*)correlation_id.value);
        if (BAIDU_UNLIKELY(rc != 0)) {
            cntl->SetFailed(rc, "Fail to add timer for backup request");
            return cntl->HandleSendFailed();
        }
    } else if (cntl->timeout_ms() >= 0) {
        // Setup timer for RPC timetout

        // _abstime_us is for truncating _connect_timeout_ms
        cntl->_abstime_us = cntl->timeout_ms() * 1000L + start_send_real_us;
        const int rc = bthread_timer_add(
            &cntl->_timeout_id,
            butil::microseconds_to_timespec(cntl->_abstime_us),
            HandleTimeout, (void*)correlation_id.value);
        if (BAIDU_UNLIKELY(rc != 0)) {
            cntl->SetFailed(rc, "Fail to add timer for timeout");
            return cntl->HandleSendFailed();
        }
    } else {
        cntl->_abstime_us = -1;
    }

    cntl->IssueRPC(start_send_real_us);
    if (done == NULL) {
        // MUST wait for response when sending synchronous RPC. It will
        // be woken up by callback when RPC finishes (succeeds or still
        // fails after retry)
        Join(correlation_id);
        if (cntl->_span) {
            cntl->SubmitSpan();
        }
        cntl->OnRPCEnd(butil::gettimeofday_us());
    }
}

void Channel::Describe(std::ostream& os, const DescribeOptions& opt) const {
    os << "Channel[";
    if (SingleServer()) {
        os << _server_address;
    } else {
        _lb->Describe(os, opt);
    }
    os << "]";
}

int Channel::Weight() {
    return (_lb ? _lb->Weight() : 0);
}

int Channel::CheckHealth() {
    if (_lb == NULL) {
        SocketUniquePtr ptr;
        if (Socket::Address(_server_id, &ptr) == 0) {
            return 0;
        }
        return -1;
    } else {
        SocketUniquePtr tmp_sock;
        LoadBalancer::SelectIn sel_in = { 0, false, false, 0, NULL };
        LoadBalancer::SelectOut sel_out(&tmp_sock);
        return _lb->SelectServer(sel_in, &sel_out);
    }
}

} // namespace brpc
