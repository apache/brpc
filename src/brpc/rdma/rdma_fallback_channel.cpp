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

class SubDone : public google::protobuf::Closure {
public:
    explicit SubDone(Controller* cntl,
                     CallId cid,
                     google::protobuf::Closure* user_done,
                     ChannelOptions options, // Must make an another copy
                     int max_retry)
        : _cntl(cntl)
        , _cid(cid)
        , _user_done(user_done)
        , _options(options)
        , _max_retry(max_retry)
        , _use_tcp(false) {
    }
    ~SubDone() { }
    void Run();

    Controller* _cntl;
    CallId _cid;
    google::protobuf::Closure* _user_done;
    ChannelOptions _options;
    int _max_retry;
    bool _use_tcp;
};

void SubDone::Run() {
    Controller* cntl = NULL;
    int rc = bthread_id_lock(_cid, (void**)&cntl);
    if (rc != 0) {
        LOG(ERROR) << "Fail to lock correlation_id="
                   << _cid.value << ": " << berror(rc);
        delete this;
        return;
    }
    CHECK_EQ(_cntl, cntl);
    if (_cntl->ErrorCode() == ECANCELED) {
        if (_user_done) {
            _user_done->Run();
        }
        bthread_id_unlock_and_destroy(_cid);
        delete this;
        return;
    }

    if (_cntl->Failed() && !_use_tcp) {
        if (_cntl->_error_code != ERDMAUNAVAIL &&
            _cntl->_error_code != ERDMALOCAL &&
            _cntl->_error_code != ERDMAOUTCLUSTER) {
            RdmaFallbackChannel::RdmaUnavailable(_cntl->_remote_side);
#ifdef BRPC_RDMA
            LOG_IF(INFO, FLAGS_rdma_trace_verbose) << "RDMA channel unavailable";
#endif
        }
        _cntl->_error_code = 0;
        _cntl->_correlation_id = INVALID_BTHREAD_ID;
        _cntl->_current_call.Reset();
        _cntl->set_max_retry(_max_retry);
        _use_tcp = true;
        Channel chan;
        _options.use_rdma = false;
        chan.Init(_cntl->_remote_side, &_options);
        chan.CallMethod(_cntl->_method, _cntl, NULL, _cntl->_response, this);
        bthread_id_unlock(_cid);
    } else {
        if (_user_done) {
            _user_done->Run();
        }
        bthread_id_unlock_and_destroy(_cid);
        delete this;
    }
}

RdmaFallbackChannel::RdmaFallbackChannel() 
    : _rdma_chan()
    , _tcp_chan()
    , _options()
    , _initialized(false) {
}

RdmaFallbackChannel::~RdmaFallbackChannel() {
    _initialized = false;
}

void RdmaFallbackChannel::GlobalStart() {
    // TODO:
    // In order to avoid rehash. Make it more elegant in the future.
    g_unhealth_map.init(1048576);
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
    if (!_options.use_rdma) {
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
    bool rdma_on = false;
    if (_options.use_rdma) {
        // TODO:
        // In order to avoid performance degradation, we do not use mutex here.
        // This is not strictly correct, however it will not introduce severe
        // problems. Fix this in the future.
        if (!g_unhealth_map.seek(_rdma_chan._remote_side)
                || g_unhealth_map[_rdma_chan._remote_side] == 0) {
            rdma_on = true;
        }
    }
    if (rdma_on) {
        CallId cid = cntl->call_id();
        SubDone* sub_done = new SubDone(cntl, cid, user_done, _options, cntl->max_retry());
        cntl->set_max_retry(0);
        cntl->_correlation_id = INVALID_BTHREAD_ID;
        _rdma_chan.CallMethod(method, cntl, request, response, sub_done);
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
        g_unhealth_map[chan->_remote_side] = 0;
    }
    delete chan;
#endif
    return NULL;
}

void RdmaFallbackChannel::RdmaUnavailable(butil::EndPoint remote_side) {
#ifdef BRPC_RDMA
    if (FLAGS_rdma_health_check_interval_s > 0) {
        BAIDU_SCOPED_LOCK(g_unhealth_map_lock);
        if (!g_unhealth_map.seek(remote_side)) {
            if (!g_unhealth_map.insert(remote_side, 0)) {
                return;
            }
        }
        if (g_unhealth_map[remote_side] != 0) {
            return;
        }
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
        } else {
            g_unhealth_map[remote_side] = tid;
        }
    }
#endif
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
