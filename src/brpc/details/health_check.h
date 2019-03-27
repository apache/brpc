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
//          Jiashun Zhu(zhujiashun@baidu.com)

#ifndef _HEALTH_CHECK_H
#define _HEALTH_CHECK_H

#include "brpc/socket_id.h"
#include "brpc/periodic_task.h"
#include "bvar/bvar.h"

namespace brpc {

class HealthCheckTask : public PeriodicTask {
public:
    explicit HealthCheckTask(SocketId id, bvar::Adder<int64_t>* nhealthcheck);
    bool OnTriggeringTask(timespec* next_abstime) override;
    void OnDestroyingTask() override;

private:
    SocketId _id;
    bool _first_time;
    bvar::Adder<int64_t>* _nhealthcheck;
};

} // namespace brpc

#endif
