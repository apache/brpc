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

#ifndef BRPC_URMA_RECOVERY_POLICY_H
#define BRPC_URMA_RECOVERY_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <mutex>
#include <string>

namespace brpc {
namespace urma {

enum class UrmaRecoveryMode { LEGACY, AUTO, TCP_ONLY };

enum class UrmaFailureReason {
    NONE,
    RETRY_EXHAUSTED,
    RNR,
    QP_FATAL,
    CQ_ERROR,
    CQ_POLL,
    CQ_EVENT,
    CQ_NOTIFY,
    SEND_POST,
    ACCESS_ERROR,
    TRANSPORT_ERROR,
    RECV_POST,
    COUNT
};

const char* UrmaFailureReasonName(UrmaFailureReason reason);

struct UrmaRecoveryStatus {
    UrmaRecoveryMode mode = UrmaRecoveryMode::LEGACY;
    uint64_t epoch = 0;
    size_t cooled_peers = 0;
    uint64_t failures[static_cast<size_t>(UrmaFailureReason::COUNT)] = {};
    uint64_t tcp_only_admissions = 0;
    uint64_t peer_cooldown_admissions = 0;
    uint64_t cache_evictions = 0;
    uint64_t negotiated_connections = 0;
};

// Admission policy for new URMA connections.
// An empty peer selects server admission, which only checks the mode.
class UrmaRecoveryPolicy {
public:
    typedef int64_t (*Clock)();
    explicit UrmaRecoveryPolicy(Clock clock, size_t max_peers = 1024);
    bool SetMode(UrmaRecoveryMode mode);
    UrmaRecoveryMode GetMode();
    // Positive milliseconds, at most INT32_MAX. Applies to future failures.
    bool SetCooldownMs(int64_t cooldown_ms);
    bool AllowNewConnection(const std::string& peer, uint64_t* epoch = nullptr);
    void ReportPeerFailure(
        const std::string& peer, uint64_t epoch,
        UrmaFailureReason reason = UrmaFailureReason::RETRY_EXHAUSTED);
    UrmaRecoveryStatus GetStatus();
    void RecordNegotiatedConnection(uint64_t epoch);

private:
    std::mutex _mutex;
    const Clock _clock;
    const size_t _max_peers;
    UrmaRecoveryMode _mode;
    uint64_t _epoch;
    int64_t _cooldown_us;
    std::map<std::string, int64_t> _peers;
    UrmaRecoveryStatus _status;
};

// Process-lifetime policy, shared by client and server handshake admission.
UrmaRecoveryPolicy& GlobalUrmaRecoveryPolicy();
bool SetUrmaRecoveryMode(UrmaRecoveryMode mode);
UrmaRecoveryMode GetUrmaRecoveryMode();
bool SetUrmaRecoveryCooldownMs(int64_t cooldown_ms);
UrmaRecoveryStatus GetUrmaRecoveryStatus();

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_URMA_RECOVERY_POLICY_H
