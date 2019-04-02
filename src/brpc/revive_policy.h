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
class RevivePolicy {
public:
    // TODO(zhujiashun): 

    virtual void StartRevive() = 0;
    virtual bool RejectDuringReviving(const std::vector<ServerId>& server_list) = 0;
};

class DefaultRevivePolicy : public RevivePolicy {
public:
    DefaultRevivePolicy(int64_t minimum_working_instances, int64_t hold_time_ms);

    void StartRevive() override;
    bool RejectDuringReviving(const std::vector<ServerId>& server_list) override;

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

