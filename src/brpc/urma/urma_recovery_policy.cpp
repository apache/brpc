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

#include "brpc/urma/urma_recovery_policy.h"
#include "butil/time.h"
#include "bvar/passive_status.h"

namespace brpc {
namespace urma {

const char* UrmaFailureReasonName(UrmaFailureReason reason) {
    switch (reason) {
    case UrmaFailureReason::NONE:
        return "none";
    case UrmaFailureReason::RETRY_EXHAUSTED:
        return "retry_exhausted";
    case UrmaFailureReason::RNR:
        return "rnr";
    case UrmaFailureReason::QP_FATAL:
        return "qp_fatal";
    case UrmaFailureReason::CQ_ERROR:
        return "cq_error";
    case UrmaFailureReason::CQ_POLL:
        return "cq_poll";
    case UrmaFailureReason::CQ_EVENT:
        return "cq_event";
    case UrmaFailureReason::CQ_NOTIFY:
        return "cq_notify";
    case UrmaFailureReason::SEND_POST:
        return "send_post";
    case UrmaFailureReason::ACCESS_ERROR:
        return "access_error";
    case UrmaFailureReason::TRANSPORT_ERROR:
        return "transport_error";
    case UrmaFailureReason::RECV_POST:
        return "recv_post";
    default:
        return "unknown";
    }
}

UrmaRecoveryPolicy::UrmaRecoveryPolicy(Clock clock, size_t max_peers)
    : _clock(clock)
    , _max_peers(max_peers)
    , _mode(UrmaRecoveryMode::LEGACY)
    , _epoch(1)
    , _cooldown_us(5000000) {}

bool UrmaRecoveryPolicy::SetMode(UrmaRecoveryMode mode) {
    if (mode != UrmaRecoveryMode::LEGACY && mode != UrmaRecoveryMode::AUTO &&
        mode != UrmaRecoveryMode::TCP_ONLY) {
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (_mode != mode) {
        _mode = mode;
        ++_epoch;
        _peers.clear();
    }
    return true;
}

UrmaRecoveryMode UrmaRecoveryPolicy::GetMode() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _mode;
}

bool UrmaRecoveryPolicy::SetCooldownMs(int64_t cooldown_ms) {
    if (cooldown_ms <= 0 || cooldown_ms > INT32_MAX) {
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    _cooldown_us = cooldown_ms * 1000;
    return true;
}

bool UrmaRecoveryPolicy::AllowNewConnection(const std::string& peer,
                                            uint64_t* epoch) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (epoch != nullptr) {
        *epoch = _epoch;
    }
    if (_mode == UrmaRecoveryMode::LEGACY) {
        return true;
    }
    if (_mode == UrmaRecoveryMode::TCP_ONLY) {
        ++_status.tcp_only_admissions;
        return false;
    }
    const int64_t now = _clock();
    auto it = _peers.find(peer);
    if (it == _peers.end()) {
        return true;
    }
    if (now < it->second) {
        ++_status.peer_cooldown_admissions;
        return false;
    }
    _peers.erase(it);
    return true;
}

void UrmaRecoveryPolicy::ReportPeerFailure(const std::string& peer,
                                           uint64_t epoch,
                                           UrmaFailureReason reason) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (epoch != _epoch || reason <= UrmaFailureReason::NONE ||
        reason >= UrmaFailureReason::COUNT) {
        return;
    }
    ++_status.failures[static_cast<size_t>(reason)];
    if (_mode != UrmaRecoveryMode::AUTO || peer.empty() || _max_peers == 0 ||
        reason == UrmaFailureReason::ACCESS_ERROR) {
        return;
    }
    const int64_t now = _clock();
    for (auto it = _peers.begin(); it != _peers.end();) {
        if (it->second <= now) {
            it = _peers.erase(it);
        } else {
            ++it;
        }
    }
    if (_peers.count(peer) == 0 && _peers.size() == _max_peers) {
        auto oldest = _peers.begin();
        for (auto it = _peers.begin(); it != _peers.end(); ++it) {
            if (it->second < oldest->second) {
                oldest = it;
            }
        }
        _peers.erase(oldest);
        ++_status.cache_evictions;
    }
    _peers[peer] = now + _cooldown_us;
}

UrmaRecoveryStatus UrmaRecoveryPolicy::GetStatus() {
    std::lock_guard<std::mutex> lock(_mutex);
    UrmaRecoveryStatus result = _status;
    result.mode = _mode;
    result.epoch = _epoch;
    const int64_t now = _clock();
    for (const auto& peer : _peers) {
        result.cooled_peers += peer.second > now;
    }
    return result;
}

void UrmaRecoveryPolicy::RecordNegotiatedConnection(uint64_t epoch) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (epoch == _epoch) {
        ++_status.negotiated_connections;
    }
}

static void DescribeRecovery(std::ostream& os, void* arg) {
    const auto s = static_cast<UrmaRecoveryPolicy*>(arg)->GetStatus();
    os << "{\"mode\":\""
       << (s.mode == UrmaRecoveryMode::AUTO       ? "auto"
           : s.mode == UrmaRecoveryMode::TCP_ONLY ? "tcp_only"
                                                  : "legacy")
       << "\",\"epoch\":" << s.epoch << ",\"cooled_peers\":" << s.cooled_peers
       << ",\"tcp_only_admissions\":" << s.tcp_only_admissions
       << ",\"peer_cooldown_admissions\":" << s.peer_cooldown_admissions
       << ",\"cache_evictions\":" << s.cache_evictions
       << ",\"negotiated_connections\":" << s.negotiated_connections
       << ",\"failures\":{";
    for (size_t i = 1; i < static_cast<size_t>(UrmaFailureReason::COUNT); ++i) {
        if (i > 1) {
            os << ',';
        }
        os << '"' << UrmaFailureReasonName(static_cast<UrmaFailureReason>(i))
           << "\":" << s.failures[i];
    }
    os << "}}";
}

struct RecoveryMetrics {
    RecoveryMetrics(UrmaRecoveryPolicy* policy, const char* status_name,
                    const char* fallback_name)
        : status(status_name, DescribeRecovery, policy)
        , fallbacks(
              fallback_name,
              [](void* arg) {
                  const auto s =
                      static_cast<UrmaRecoveryPolicy*>(arg)->GetStatus();
                  return s.tcp_only_admissions + s.peer_cooldown_admissions;
              },
              policy) {}
    bvar::PassiveStatus<std::string> status;
    bvar::PassiveStatus<uint64_t> fallbacks;
};

UrmaRecoveryPolicy& GlobalUrmaRecoveryPolicy() {
    static UrmaRecoveryPolicy* policy =
        new UrmaRecoveryPolicy(butil::monotonic_time_us);
    static RecoveryMetrics* metrics =
        new RecoveryMetrics(policy, "urma_recovery_status",
                            "urma_recovery_tcp_fallback_admissions");
    (void)metrics;
    return *policy;
}

bool SetUrmaRecoveryMode(UrmaRecoveryMode mode) {
    return GlobalUrmaRecoveryPolicy().SetMode(mode);
}

UrmaRecoveryMode GetUrmaRecoveryMode() {
    return GlobalUrmaRecoveryPolicy().GetMode();
}

bool SetUrmaRecoveryCooldownMs(int64_t cooldown_ms) {
    return GlobalUrmaRecoveryPolicy().SetCooldownMs(cooldown_ms);
}

UrmaRecoveryStatus GetUrmaRecoveryStatus() {
    return GlobalUrmaRecoveryPolicy().GetStatus();
}

}  // namespace urma
}  // namespace brpc
