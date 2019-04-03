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

#include <vector>
#include <gflags/gflags.h>
#include "brpc/revive_policy.h"
#include "butil/scoped_lock.h"
#include "butil/synchronization/lock.h"
#include "brpc/server_id.h"
#include "brpc/socket.h"
#include "butil/fast_rand.h"
#include "butil/time.h"

namespace brpc {

DEFINE_int64(detect_available_server_interval_ms, 10, "The interval "
        "to detect available server count in DefaultRevivePolicy");

DefaultRevivePolicy::DefaultRevivePolicy(
        int64_t minimum_working_instances, int64_t hold_time_ms)
    : _reviving(false)
    , _minimum_working_instances(minimum_working_instances)
    , _last_usable(0)
    , _last_usable_change_time_ms(0)
    , _hold_time_ms(hold_time_ms)
    , _usable_cache(0)
    , _usable_cache_time_ms(0) { }

void DefaultRevivePolicy::StartReviving() {
    std::unique_lock<butil::Mutex> mu(_mutex);
    _reviving = true;
}

bool DefaultRevivePolicy::StopRevivingIfNecessary() {
    int64_t now_ms = butil::gettimeofday_ms();
    if (!_reviving) {
        return false;
    }
    {
        std::unique_lock<butil::Mutex> mu(_mutex);
        if (_last_usable_change_time_ms != 0 && _last_usable != 0 &&
                (now_ms - _last_usable_change_time_ms > _hold_time_ms)) {
            _reviving = false;
            _last_usable_change_time_ms = 0;
            mu.unlock();
            return false;
        }
    }
    return true;
}

int DefaultRevivePolicy::GetUsableServerCount(
        int64_t now_ms, const std::vector<ServerId>& server_list) {
    if (now_ms - _usable_cache_time_ms < FLAGS_detect_available_server_interval_ms) {
        return _usable_cache;
    }
    int usable = 0;
    size_t n = server_list.size();
    SocketUniquePtr ptr;
    for (size_t i = 0; i < n; ++i) {
        if (Socket::Address(server_list[i].id, &ptr) == 0
            && !ptr->IsLogOff()) {
            usable++;
        }
    }
    std::unique_lock<butil::Mutex> mu(_mutex);
    _usable_cache = usable;
    _usable_cache_time_ms = now_ms;
    return _usable_cache;
}


bool DefaultRevivePolicy::DoReject(const std::vector<ServerId>& server_list) {
    if (!_reviving) {
        return false;
    }
    int64_t now_ms = butil::gettimeofday_ms();
    int usable = GetUsableServerCount(now_ms, server_list);
    {
        std::unique_lock<butil::Mutex> mu(_mutex);
        if (_last_usable != usable) {
            _last_usable = usable;
            _last_usable_change_time_ms = now_ms;
        }
    }
    int rand = butil::fast_rand_less_than(_minimum_working_instances);
    if (rand >= usable) {
        return true;
    }
    return false;
}

} // namespace brpc
