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


#include <cmath>                                  // std::pow
#include <gflags/gflags.h>
#include "butil/fast_rand.h"                      // fast_rand_double
#include "butil/time.h"                           // gettimeofday_us
#include "brpc/reloadable_flags.h"
#include "brpc/load_balancer.h"
#include "brpc/socket.h"


namespace brpc {

DEFINE_bool(show_lb_in_vars, false, "Describe LoadBalancers in vars");
DEFINE_int32(default_weight_of_wlb, 0, "Default weight value of Weighted LoadBalancer(wlb). "
             "wlb policy degradation is enabled when default_weight_of_wlb > 0 to avoid some "
             "problems when user is using wlb but forgot to set the weights of some of their "
             "downstream instances. Then these instances will be set default_weight_of_wlb as "
             "their weights. wlb policy degradation is not enabled by default.");
DEFINE_int64(lb_warmup_ms, 0,
             "When positive, a server newly added to a LoadBalancer gets "
             "about 10% of its normal traffic share at first and ramps up "
             "to 100% over this period(ms). 0 disables the warm-up");
DEFINE_double(lb_warmup_curve, 1.0,
              "Shape of the warm-up ramp: the weight multiplier is "
              "max(0.1, progress^lb_warmup_curve) where progress rises "
              "linearly from 0 to 1 over lb_warmup_ms. 1 ramps linearly, "
              "larger values keep a new server colder for longer");
BRPC_VALIDATE_GFLAG(show_lb_in_vars, PassValidate);
BRPC_VALIDATE_GFLAG(lb_warmup_ms, PassValidate);
BRPC_VALIDATE_GFLAG(lb_warmup_curve, PassValidate);

// Floor of the warm-up multiplier so that a warming server still gets a
// trickle of traffic and latency-based policies keep observing it.
static const double WARMUP_MIN_RATIO = 0.1;

double WarmupMultiplierImpl(int64_t join_time_us, int64_t now_us) {
    const int64_t warmup_us = FLAGS_lb_warmup_ms * 1000L;
    if (warmup_us <= 0 || join_time_us <= 0) {
        return 1.0;
    }
    if (now_us <= 0) {
        now_us = butil::gettimeofday_us();
    }
    const int64_t elapsed_us = now_us - join_time_us;
    if (elapsed_us >= warmup_us) {
        return 1.0;
    }
    if (elapsed_us <= 0) {
        // The clock went backwards, be conservative.
        return WARMUP_MIN_RATIO;
    }
    double progress = (double)elapsed_us / (double)warmup_us;
    if (FLAGS_lb_warmup_curve > 0 && FLAGS_lb_warmup_curve != 1.0) {
        progress = std::pow(progress, FLAGS_lb_warmup_curve);
    }
    return std::max(progress, WARMUP_MIN_RATIO);
}

bool WarmupAcceptImpl(int64_t join_time_us, int64_t now_us) {
    const double m = WarmupMultiplierImpl(join_time_us, now_us);
    return m >= 1.0 || butil::fast_rand_double() < m;
}

// For assigning unique names for lb.
static butil::static_atomic<int> g_lb_counter = BUTIL_STATIC_ATOMIC_INIT(0);

bool LoadBalancer::IsServerAvailable(SocketId id, SocketUniquePtr* out) {
    SocketUniquePtr ptr;
    bool res = Socket::Address(id, &ptr) == 0 && ptr->IsAvailable();
    if (res) {
        *out = std::move(ptr);
    }
    return res;
}

void SharedLoadBalancer::DescribeLB(std::ostream& os, void* arg) {
    (static_cast<SharedLoadBalancer*>(arg))->Describe(os, DescribeOptions());
}

void SharedLoadBalancer::ExposeLB() {
    bool changed = false;
    _st_mutex.lock();
    if (!_exposed) {
        _exposed = true;
        changed = true;
    }
    _st_mutex.unlock();
    if (changed) {
        char name[32];
        snprintf(name, sizeof(name), "_load_balancer_%d", g_lb_counter.fetch_add(
                     1, butil::memory_order_relaxed));
        _st.expose(name);
    }
}

SharedLoadBalancer::SharedLoadBalancer()
    : _lb(nullptr)
    , _weight_sum(0)
    , _exposed(false)
    , _st(DescribeLB, this) {
}

SharedLoadBalancer::~SharedLoadBalancer() {
    _st.hide();
    if (_lb) {
        _lb->Destroy();
        _lb = nullptr;
    }
}

int SharedLoadBalancer::Init(const char* lb_protocol) {
    std::string lb_name;
    butil::StringPiece lb_params;
    if (!ParseParameters(lb_protocol, &lb_name, &lb_params)) {
        LOG(FATAL) << "Fail to parse this load balancer protocol '" << lb_protocol << '\'';
        return -1;
    }
    const LoadBalancer* lb = LoadBalancerExtension()->Find(lb_name.c_str());
    if (lb == nullptr) {
        LOG(FATAL) << "Fail to find LoadBalancer by `" << lb_name << "'";
        return -1;
    }
    _lb = lb->New(lb_params);
    if (_lb == nullptr) {
        LOG(FATAL) << "Fail to new LoadBalancer";
        return -1;
    }
    if (FLAGS_show_lb_in_vars && !_exposed) {
        ExposeLB();
    }
    return 0;
}

void SharedLoadBalancer::Describe(std::ostream& os,
                                  const DescribeOptions& options) {
    if (_lb == nullptr) {
        os << "lb=NULL";
    } else {
        _lb->Describe(os, options);
    }
}

bool SharedLoadBalancer::ParseParameters(const butil::StringPiece& lb_protocol,
                                         std::string* lb_name,
                                         butil::StringPiece* lb_params) {
    lb_name->clear();
    lb_params->clear();
    if (lb_protocol.empty()) {
        return false;
    }
    const char separator = ':';
    size_t pos = lb_protocol.find(separator);
    if (pos == std::string::npos) {
        lb_name->append(lb_protocol.data(), lb_protocol.size());
    } else {
        lb_name->append(lb_protocol.data(), pos);
        if (pos < lb_protocol.size() - sizeof(separator)) {
            *lb_params = lb_protocol.substr(pos + sizeof(separator));
        }
    }

    return true;
}
																				 
} // namespace brpc
