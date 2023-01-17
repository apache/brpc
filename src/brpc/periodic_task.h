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


#ifndef BRPC_PERIODIC_TASK_H
#define BRPC_PERIODIC_TASK_H

namespace brpc {

// Override OnTriggeringTask() with code that needs to be periodically run. If
// the task is completed, the method should return false; Otherwise the method
// should return true and set `next_abstime' to the time that the task should
// be run next time.
// Each call to OnTriggeringTask() is run in a separated bthread which can be
// suspended. To preserve states between different calls, put the states as
// fields of (subclass of) PeriodicTask.
// If any error occurs or OnTriggeringTask() returns false, the task is called
// with OnDestroyingTask() and will not be scheduled anymore.
class PeriodicTask {
public:
    virtual ~PeriodicTask();
    virtual bool OnTriggeringTask(timespec* next_abstime) = 0;
    virtual void OnDestroyingTask() = 0;
};

class PeriodicTaskManager {
public:
    static void StartTaskAt(PeriodicTask* task, const timespec& abstime);
};


} // namespace brpc

#endif  // BRPC_PERIODIC_TASK_H
