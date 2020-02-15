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


#include "brpc/details/health_check.h"
#include "brpc/socket.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/global.h"
#include "brpc/log.h"
#include "bthread/unstable.h"
#include "bthread/bthread.h"

namespace brpc {

// Declared at socket.cpp
extern SocketVarsCollector* g_vars;

DEFINE_string(health_check_path, "", "Http path of health check call."
        "By default health check succeeds if the server is connectable."
        "If this flag is set, health check is not completed until a http "
        "call to the path succeeds within -health_check_timeout_ms(to make "
        "sure the server functions well).");
DEFINE_int32(health_check_timeout_ms, 500, "The timeout for both establishing "
        "the connection and the http call to -health_check_path over the connection");

class HealthCheckChannel : public brpc::Channel {
public:
    HealthCheckChannel() {}
    ~HealthCheckChannel() {}

    int Init(SocketId id, const ChannelOptions* options);
};

int HealthCheckChannel::Init(SocketId id, const ChannelOptions* options) {
    brpc::GlobalInitializeOrDie();
    if (InitChannelOptions(options) != 0) {
        return -1;
    }
    _server_id = id;
    return 0;
}

class OnAppHealthCheckDone : public google::protobuf::Closure {
public:
    virtual void Run();

    HealthCheckChannel channel;
    brpc::Controller cntl;
    SocketId id;
    int64_t interval_s;
    int64_t last_check_time_ms;
};

class HealthCheckManager {
public:
    static void StartCheck(SocketId id, int64_t check_interval_s);
    static void* AppCheck(void* arg);
};

void HealthCheckManager::StartCheck(SocketId id, int64_t check_interval_s) {
    SocketUniquePtr ptr;
    const int rc = Socket::AddressFailedAsWell(id, &ptr);
    if (rc < 0) {
        RPC_VLOG << "SocketId=" << id
                 << " was abandoned during health checking";
        return;
    }
    LOG(INFO) << "Checking path=" << ptr->remote_side() << FLAGS_health_check_path;
    OnAppHealthCheckDone* done = new OnAppHealthCheckDone;
    done->id = id;
    done->interval_s = check_interval_s;
    brpc::ChannelOptions options;
    options.protocol = PROTOCOL_HTTP;
    options.max_retry = 0;
    options.timeout_ms =
        std::min((int64_t)FLAGS_health_check_timeout_ms, check_interval_s * 1000);
    if (done->channel.Init(id, &options) != 0) {
        LOG(WARNING) << "Fail to init health check channel to SocketId=" << id;
        ptr->_ninflight_app_health_check.fetch_sub(
                    1, butil::memory_order_relaxed);
        delete done;
        return;
    }
    AppCheck(done);
}

void* HealthCheckManager::AppCheck(void* arg) {
    OnAppHealthCheckDone* done = static_cast<OnAppHealthCheckDone*>(arg);
    done->cntl.Reset();
    done->cntl.http_request().uri() = FLAGS_health_check_path;
    ControllerPrivateAccessor(&done->cntl).set_health_check_call();
    done->last_check_time_ms = butil::gettimeofday_ms();
    done->channel.CallMethod(NULL, &done->cntl, NULL, NULL, done);
    return NULL;
}

void OnAppHealthCheckDone::Run() {
    std::unique_ptr<OnAppHealthCheckDone> self_guard(this);
    SocketUniquePtr ptr;
    const int rc = Socket::AddressFailedAsWell(id, &ptr);
    if (rc < 0) {
        RPC_VLOG << "SocketId=" << id
                << " was abandoned during health checking";
        return;
    }
    if (!cntl.Failed() || ptr->Failed()) {
        LOG_IF(INFO, !cntl.Failed()) << "Succeeded to call "
            << ptr->remote_side() << FLAGS_health_check_path;
        // if ptr->Failed(), previous SetFailed would trigger next round
        // of hc, just return here.
        ptr->_ninflight_app_health_check.fetch_sub(
                    1, butil::memory_order_relaxed);
        return;
    }
    RPC_VLOG << "Fail to check path=" << FLAGS_health_check_path
        << ", " << cntl.ErrorText();

    int64_t sleep_time_ms =
        last_check_time_ms + interval_s * 1000 - butil::gettimeofday_ms();
    if (sleep_time_ms > 0) {
        // TODO(zhujiashun): we need to handle the case when timer fails
        // and bthread_usleep returns immediately. In most situations,
        // the possibility of this case is quite small, so currently we
        // just keep sending the hc call.
        bthread_usleep(sleep_time_ms * 1000);
    }
    HealthCheckManager::AppCheck(self_guard.release());
}

class HealthCheckTask : public PeriodicTask {
public:
    explicit HealthCheckTask(SocketId id);
    bool OnTriggeringTask(timespec* next_abstime) override;
    void OnDestroyingTask() override;

private:
    SocketId _id;
    bool _first_time;
};

HealthCheckTask::HealthCheckTask(SocketId id)
    : _id(id)
    , _first_time(true) {}

bool HealthCheckTask::OnTriggeringTask(timespec* next_abstime) {
    SocketUniquePtr ptr;
    const int rc = Socket::AddressFailedAsWell(_id, &ptr);
    CHECK(rc != 0);
    if (rc < 0) {
        RPC_VLOG << "SocketId=" << _id
                 << " was abandoned before health checking";
        return false;
    }
    // Note: Making a Socket re-addessable is hard. An alternative is
    // creating another Socket with selected internal fields to replace
    // failed Socket. Although it avoids concurrent issues with in-place
    // revive, it changes SocketId: many code need to watch SocketId 
    // and update on change, which is impractical. Another issue with
    // this method is that it has to move "selected internal fields" 
    // which may be accessed in parallel, not trivial to be moved.
    // Finally we choose a simple-enough solution: wait until the
    // reference count hits `expected_nref', which basically means no
    // one is addressing the Socket(except here). Because the Socket 
    // is not addressable, the reference count will not increase 
    // again. This solution is not perfect because the `expected_nref'
    // is implementation specific. In our case, one reference comes 
    // from SocketMapInsert(socket_map.cpp), one reference is here. 
    // Although WaitAndReset() could hang when someone is addressing
    // the failed Socket forever (also indicating bug), this is not an 
    // issue in current code. 
    if (_first_time) {  // Only check at first time.
        _first_time = false;
        if (ptr->WaitAndReset(2/*note*/) != 0) {
            LOG(INFO) << "Cancel checking " << *ptr;
            return false;
        }
    }

    // g_vars must not be NULL because it is newed at the creation of
    // first Socket. When g_vars is used, the socket is at health-checking
    // state, which means the socket must be created and then g_vars can
    // not be NULL.
    g_vars->nhealthcheck << 1;
    int hc = 0;
    if (ptr->_user) {
        hc = ptr->_user->CheckHealth(ptr.get());
    } else {
        hc = ptr->CheckHealth();
    }
    if (hc == 0) {
        if (ptr->CreatedByConnect()) {
            g_vars->channel_conn << -1;
        }
        if (!FLAGS_health_check_path.empty()) {
            ptr->_ninflight_app_health_check.fetch_add(
                    1, butil::memory_order_relaxed);
        }
        ptr->Revive();
        ptr->_hc_count = 0;
        if (!FLAGS_health_check_path.empty()) {
            HealthCheckManager::StartCheck(_id, ptr->_health_check_interval_s);
        }
        return false;
    } else if (hc == ESTOP) {
        LOG(INFO) << "Cancel checking " << *ptr;
        return false;
    }
    ++ ptr->_hc_count;
    *next_abstime = butil::seconds_from_now(ptr->_health_check_interval_s);
    return true;
}

void HealthCheckTask::OnDestroyingTask() {
    delete this;
}

void StartHealthCheck(SocketId id, int64_t delay_ms) {
    PeriodicTaskManager::StartTaskAt(new HealthCheckTask(id),
            butil::milliseconds_from_now(delay_ms));
}

} // namespace brpc
