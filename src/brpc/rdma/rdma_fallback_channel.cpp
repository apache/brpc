// Copyright (c) 2015 baidu-rpc authors.
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

// Authors: Li Zhaogeng (lizhaogeng01@baidu.com)

#include <gflags/gflags.h>
#include "butil/time.h"                                      // gettimeofday_us
#include "butil/containers/flat_map.h"
#include "bthread/bthread.h"
#include "bthread/unstable.h"
#include "brpc/builtin_service.pb.h"
#include "brpc/builtin/health_service.h"
#include "brpc/details/controller_private_accessor.h"        // RPCSender
#include "brpc/controller.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_fallback_channel.h"

namespace brpc {
namespace rdma {

#ifdef BRPC_RDMA
DEFINE_int32(rdma_health_check_interval_s, 1,
             "seconds between consecutive health-checking of unaccessible"
             " RDMA channel inside RdmaFallbackChannel");
DECLARE_bool(rdma_trace_verbose);
#endif

butil::FlatMap<butil::EndPoint, bthread_t> g_unhealth_map;
butil::Mutex g_unhealth_map_lock;
bool g_stop = false;

class SubDone;

// The sender to intercept Controller::IssueRPC
class Sender : public RPCSender,
               public google::protobuf::Closure {
friend class SubDone;
public:
    Sender(CallId cid, Controller* cntl,
           const google::protobuf::Message* request,
           google::protobuf::Message* response,
           google::protobuf::Closure* user_done,
           Channel* rdma_chan, ChannelOptions* options);
    ~Sender() { Clear(); }
    int IssueRPC(int64_t start_realtime_us);
    void Run();
    void Clear();

private:
    CallId _main_cid;
    Controller* _main_cntl;
    const google::protobuf::Message* _request;
    google::protobuf::Message* _response;
    google::protobuf::Closure* _user_done;
    butil::IOBuf _request_buf;
    Controller _sub_cntl;
    CallId _sub_call_id;
    SubDone* _sub_done;
    Channel* _rdma_chan;
    butil::EndPoint _remote_side;
    ChannelOptions _options;
};

class SubDone : public google::protobuf::Closure {
public:
    explicit SubDone(Sender* owner)
        : _owner(owner)
        , _main_cid(INVALID_BTHREAD_ID)
        , _cntl(NULL)
        , _retried(false) {
    }
    ~SubDone() { }
    void Run();

    Sender* _owner;
    CallId _main_cid;
    Controller* _cntl;
    bool _retried;
};

Sender::Sender(
        CallId cid,
        Controller* cntl,
        const google::protobuf::Message* request,
        google::protobuf::Message* response,
        google::protobuf::Closure* user_done,
        Channel* rdma_chan, ChannelOptions* options)
    : _main_cid(cid)
    , _main_cntl(cntl)
    , _request(request)
    , _response(response)
    , _user_done(user_done)
    , _request_buf()
    , _sub_cntl()
    , _sub_call_id(INVALID_BTHREAD_ID)
    , _sub_done(NULL)
    , _rdma_chan(rdma_chan)
    , _remote_side(rdma_chan->_remote_side)
    , _options(*options) {
    _options.use_rdma = false;
}

int Sender::IssueRPC(int64_t start_realtime_us) {
    _sub_cntl.set_max_retry(0);  // Do not retry on RDMA channel
    if (_main_cntl->timeout_ms() > 0) {
        _sub_cntl.set_timeout_ms(_main_cntl->timeout_ms() / 2);
    }
    _sub_cntl.set_connection_type(_main_cntl->connection_type());
    _sub_cntl.set_type_of_service(_main_cntl->_tos);
    _sub_cntl.set_request_compress_type(_main_cntl->request_compress_type());
    _sub_cntl.set_log_id(_main_cntl->log_id());
    _sub_cntl.set_request_code(_main_cntl->request_code());
    _sub_cntl.request_attachment().append(_main_cntl->request_attachment());
    _rdma_chan->_serialize_request(&_request_buf, &_sub_cntl, _request);
    _sub_cntl._request_buf.clear();
    _sub_cntl._request_buf.append(_request_buf);
    _sub_done = new SubDone(this);
    _sub_done->_main_cid = _main_cid;
    _sub_done->_cntl = &_sub_cntl;
    _sub_call_id = _sub_cntl.call_id();
    _rdma_chan->CallMethod(
            _main_cntl->_method, &_sub_cntl, NULL, _response, _sub_done);
    return 0;
}

// Run when the main call is finished.
void Sender::Run() {
    const int saved_error = _main_cntl->ErrorCode();
    if (saved_error == ERPCTIMEDOUT || saved_error == ECANCELED) {
        // If the main call is timed out or canceled, we must inform the sub call.
        CHECK_EQ(0, bthread_id_unlock(_main_cid));
        bthread_id_error(_sub_call_id, saved_error);
    }
}

void Sender::Clear() {
    if (!_main_cntl) {
        return;
    }
    const CallId cid = _main_cntl->call_id();
    _main_cntl = NULL;
    if (_user_done) {
        _user_done->Run();
    }
    bthread_id_unlock_and_destroy(cid);
}

// Run when the sub call is finished.
void SubDone::Run() {
    Controller* main_cntl = NULL;
    const int rc = bthread_id_lock(_main_cid, (void**)&main_cntl);
    if (rc != 0) {
        LOG(ERROR) << "Fail to lock correlation_id="
                   << _main_cid.value << ": " << berror(rc);
        delete this;
        return;
    }
    if (main_cntl->ErrorCode() == ERPCTIMEDOUT || main_cntl->ErrorCode() == ECANCELED) {
        _owner->Clear();
        delete this;
        return;
    }

    if (_cntl->Failed()) {
        if (!_retried) {
            if (_cntl->_error_code != ERDMAUNAVAIL &&
                _cntl->_error_code != ERDMALOCAL &&
                _cntl->_error_code != ERDMAOUTCLUSTER) {
                RdmaFallbackChannel::RdmaUnavailable(_owner->_remote_side);
#ifdef BRPC_RDMA
                LOG_IF(INFO, FLAGS_rdma_trace_verbose) << "RDMA channel unavailable";
#endif
            }
            // Try TCP Channel
            butil::IOBuf req_attachment;
            req_attachment.swap(_cntl->request_attachment());
            _cntl->Reset();
            _cntl->set_max_retry(main_cntl->max_retry());
            _cntl->set_timeout_ms(main_cntl->timeout_ms());
            _cntl->set_connection_type(main_cntl->connection_type());
            _cntl->set_type_of_service(main_cntl->_tos);
            _cntl->set_request_compress_type(main_cntl->request_compress_type());
            _cntl->set_log_id(main_cntl->log_id());
            _cntl->set_request_code(main_cntl->request_code());
            _cntl->request_attachment().swap(req_attachment);
            _retried = true;
            _cntl->_request_buf.swap(_owner->_request_buf);
            _owner->_sub_call_id = _cntl->call_id();
            Channel chan;
            chan.Init(_owner->_remote_side, &_owner->_options);
            chan.CallMethod(main_cntl->_method, _cntl, NULL, _owner->_response, this);
            CHECK_EQ(0, bthread_id_unlock(_main_cid));
            return;
        }
        main_cntl->SetFailed(_cntl->_error_text);
        main_cntl->_error_code = _cntl->_error_code;
    } else {
        main_cntl->_response->GetReflection()->Swap(main_cntl->_response, _cntl->_response);
        main_cntl->response_attachment().append(_cntl->response_attachment());
    }
    main_cntl->_end_time_us = _cntl->_end_time_us;
    if (main_cntl->_timeout_id != 0) {
        bthread_timer_del(main_cntl->_timeout_id);
        main_cntl->_timeout_id = 0;
    }
    _owner->Clear();
    delete this;
}

RdmaFallbackChannel::RdmaFallbackChannel() 
    : _rdma_chan()
    , _tcp_chan()
    , _options()
    , _initialized(false)
    , _rdma_on(false) {
}

RdmaFallbackChannel::~RdmaFallbackChannel() {
    _initialized = false;
}

void RdmaFallbackChannel::GlobalStart() {
    g_unhealth_map.init(1024);
}

void RdmaFallbackChannel::GlobalStop() {
    g_stop = true;
    std::vector<bthread_t> tids;
    {
        BAIDU_SCOPED_LOCK(g_unhealth_map_lock);
        for (butil::FlatMap<butil::EndPoint, bthread_t>::iterator
                it = g_unhealth_map.begin();
                it != g_unhealth_map.end(); ++it) {
            tids.push_back(it->second);
        }
    }
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], 0);
    }
}

#define ChannelInit(chan, opt) \
    switch (type) { \
    case 0: { \
        rc = chan.Init(*server_ep, opt); \
        break; \
    } \
    case 1: { \
        rc = chan.Init(server_addr_and_port, opt); \
        break; \
    } \
    case 2: { \
        rc = chan.Init(server_addr, port, opt); \
        break; \
    } \
    default: \
        break; \
    } \

int RdmaFallbackChannel::InitInternal(const ChannelOptions* options, int type,
                                      butil::EndPoint* server_ep,
                                      const char* server_addr_and_port,
                                      const char* server_addr, int port) {
    int rc = -1;
    _options = *options;
    _rdma_on = _options.use_rdma;
    if (!_rdma_on) {
        ChannelInit(_tcp_chan, &_options);
    } else {
        ChannelInit(_rdma_chan, &_options);
        if (rc == 0) {
            _options.use_rdma = false;
            ChannelInit(_tcp_chan, &_options);
            _options.use_rdma = true;
        }
    }
    if (rc == 0) {
        _initialized = true;
    }
    return rc;
}

int RdmaFallbackChannel::Init(
    butil::EndPoint server_addr_and_port, const ChannelOptions* options) {
    return InitInternal(options, 0, &server_addr_and_port, NULL, NULL, 0);
}

int RdmaFallbackChannel::Init(
    const char* server_addr_and_port, const ChannelOptions* options) {
    return InitInternal(options, 1, NULL, server_addr_and_port, NULL, 0);
}

int RdmaFallbackChannel::Init(
    const char* server_addr, int port, const ChannelOptions* options) {
    return InitInternal(options, 2, NULL, NULL, server_addr, port);
}

void RdmaFallbackChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* ctrl_base,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* user_done) {
    Controller* cntl = static_cast<Controller*>(ctrl_base);
    if (!_initialized) {
        cntl->SetFailed(EINVAL, "RdmaFallbackChannel=%p is not initialized yet",
                        this);
        return;
    }
    if (_options.use_rdma && g_unhealth_map_lock.try_lock()) {
        // We should avoid contention here. _rdma_on may not be updated in time.
        if (g_unhealth_map.seek(_rdma_chan._remote_side)) {
            _rdma_on = false;
        } else {
            _rdma_on = true;
        }
        g_unhealth_map_lock.unlock();
    }
    if (_rdma_on) {
        const CallId cid = cntl->call_id();
        Sender* sndr = new Sender(cid,
                cntl, const_cast<google::protobuf::Message*>(request),
                response, user_done, &_rdma_chan, &_options);
        cntl->_sender = sndr;
        cntl->add_flag(Controller::FLAGS_DESTROY_CID_IN_DONE);
        _rdma_chan.CallMethod(method, cntl, request, response, sndr);
        if (user_done == NULL) {
            Join(cid);
            cntl->OnRPCEnd(butil::gettimeofday_us());
        }
    } else {
        _tcp_chan.CallMethod(method, cntl, request, response, user_done);
    }
}

void* RdmaFallbackChannel::RdmaHealthCheckThread(void* arg) {
#ifdef BRPC_RDMA
    Channel* chan = (Channel*)arg;
    while (!g_stop) {
        bthread_usleep(FLAGS_rdma_health_check_interval_s * 1000000);
        if (CheckHealth(chan) == 0) {
            LOG_IF(INFO, FLAGS_rdma_trace_verbose) << "RDMA channel available again";
            break;
        }
    }
    {
        BAIDU_SCOPED_LOCK(g_unhealth_map_lock);
        g_unhealth_map.erase(chan->_remote_side);
    }
    delete chan;
#endif
    return NULL;
}

void RdmaFallbackChannel::RdmaUnavailable(butil::EndPoint remote_side) {
#ifndef BRPC_RDMA
    {
#else
    if (FLAGS_rdma_health_check_interval_s > 0) {
#endif
        BAIDU_SCOPED_LOCK(g_unhealth_map_lock);
        if (!g_unhealth_map.seek(remote_side)) {
            if (g_unhealth_map.insert(remote_side, 0)) {
                bthread_t tid;
                Channel* chan = new Channel;
                ChannelOptions options;
                options.use_rdma = true;
                options.connection_type = "short";
                chan->Init(remote_side, &options);
                if (bthread_start_background(
                        &tid, &BTHREAD_ATTR_NORMAL,
                        RdmaHealthCheckThread, chan) != 0) {
                    delete chan;
                    g_unhealth_map.erase(remote_side);
                } else {
                    g_unhealth_map[remote_side] = tid;
                }
            }
        }
    }
}

int RdmaFallbackChannel::CheckHealth() {
    return 0;
}

int RdmaFallbackChannel::CheckHealth(Channel* chan) {
    Controller cntl;
    cntl.set_timeout_ms(500);
    RdmaHealthRequest request;
    RdmaHealthResponse response;
    RdmaHealthService_Stub stub(chan);
    stub.Check(&cntl, &request, &response, NULL);
    return cntl.ErrorCode();
}

void RdmaFallbackChannel::Describe(
    std::ostream& os, const DescribeOptions& options) const {
    os << "RdmaFallbackChannel[";
    if (_initialized) {
        os << "initialized";
    } else {
        os << "uninitialized";
    }
    os << ']';
}

}  // namespace rdma
}  // namespace brpc
