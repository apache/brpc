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


#include <bthread/bthread.h>
#include <bthread/unstable.h>
#include "brpc/periodic_task.h"

namespace brpc {

PeriodicTask::~PeriodicTask() {
}

static void* PeriodicTaskThread(void* arg) {
    PeriodicTask* task = static_cast<PeriodicTask*>(arg);
    timespec abstime;
    if (!task->OnTriggeringTask(&abstime)) { // end
        task->OnDestroyingTask();
        return NULL;
    }
    PeriodicTaskManager::StartTaskAt(task, abstime);
    return NULL;
}

static void RunPeriodicTaskThread(void* arg) {
    bthread_t th = 0;
    int rc = bthread_start_background(
        &th, &BTHREAD_ATTR_NORMAL, PeriodicTaskThread, arg);
    if (rc != 0) {
        LOG(ERROR) << "Fail to start PeriodicTaskThread";
        static_cast<PeriodicTask*>(arg)->OnDestroyingTask();
        return;
    }
}

void PeriodicTaskManager::StartTaskAt(PeriodicTask* task, const timespec& abstime) {
    if (task == NULL) {
        LOG(ERROR) << "Param[task] is NULL";
        return;
    }
    bthread_timer_t timer_id;
    const int rc = bthread_timer_add(
        &timer_id, abstime, RunPeriodicTaskThread, task);
    if (rc != 0) {
        LOG(ERROR) << "Fail to add timer for RunPerodicTaskThread";
        task->OnDestroyingTask();
        return;
    }
}

} // namespace brpc
