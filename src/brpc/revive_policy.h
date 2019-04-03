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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#ifndef BRPC_REVIVE_POLICY
#define BRPC_REVIVE_POLICY

#include <cstdint>
#include <butil/synchronization/lock.h>

namespace brpc {

class ServerId;

// After all servers are shutdown and health check happens, servers are
// online one by one. Once one server is up, all the request that should
// be sent to all servers, would be sent to one server, which may be a
// disastrous behaviour. In the worst case it would cause the server shutdown
// again if circuit breaker is enabled and the server cluster would never
// recover. This class controls the amount of requests that sent to the revived
// servers when recovering from all servers are shutdown.
class RevivePolicy {
public:
    // Indicate that reviving from the shutdown of all server is happening.
    virtual void StartReviving() = 0;

    // Return true if some customized policies are satisfied.
    virtual bool DoReject(const std::vector<ServerId>& server_list) = 0;

    // Stop reviving state and do not reject the request if some condition is
    // satisfied.
    // Return true if the current state is still in reviving.
    virtual bool StopRevivingIfNecessary() = 0;
};

// The default revive policy. Once no servers are available, reviving is start.
// If in reviving state, the probability that a request is accepted is q/n, in
// which q is the number of current available server, n is the number of minimum
// working instances setting by user. If q is not changed during a given time,
// hold_time_ms, then the cluster is considered recovered and all the request
// would be sent to the current available servers.
class DefaultRevivePolicy : public RevivePolicy {
public:
    DefaultRevivePolicy(int64_t minimum_working_instances, int64_t hold_time_ms);

    void StartReviving();
    bool DoReject(const std::vector<ServerId>& server_list);
    bool StopRevivingIfNecessary();

private:
    bool _reviving;
    int64_t _minimum_working_instances;
    butil::Mutex _mutex;
    int64_t _last_usable;
    int64_t _last_usable_change_time_ms;
    int64_t _hold_time_ms;
};

} // namespace brpc

#endif

