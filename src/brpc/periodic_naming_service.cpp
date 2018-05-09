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

#include <gflags/gflags.h>
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "brpc/log.h"
#include "brpc/reloadable_flags.h"
#include "brpc/periodic_naming_service.h"
#include "brpc/periodic_task.h"
#include "brpc/shared_object.h"

namespace brpc {

DEFINE_int32(ns_access_interval, 5,
             "Wait so many seconds before next access to naming service");
BRPC_VALIDATE_GFLAG(ns_access_interval, PositiveInteger);

class AccessNamingServiceTask : public PeriodicTask
                              , public SharedObject {
friend class PeriodicNamingService;
public:
    AccessNamingServiceTask(PeriodicNamingService* owner,
                            const char* service_name,
                            NamingServiceActions* actions)
        : _owner(owner)
        , _service_name(service_name)
        , _actions(actions)
        , _scheduled_destroy(false) {}
    bool DoPeriodicTask(timespec* next_abstime);

    void CleanUp();
private:
    PeriodicNamingService* _owner;
    std::string _service_name;
    std::unique_ptr<NamingServiceActions> _actions;
    bool _scheduled_destroy;
};

void AccessNamingServiceTask::CleanUp() {
    _actions.reset(NULL);
}

bool AccessNamingServiceTask::DoPeriodicTask(timespec* next_abstime) {
    if (next_abstime == NULL) {
        // Remove the ref added for this task.
        this->RemoveRefManually();
        return true;
    }
    if (_scheduled_destroy) {
        return false;
    }
    std::vector<ServerNode> servers;
    const int rc = _owner->GetServers(_service_name.c_str(), &servers);
    if (rc == 0) {
        _actions->ResetServers(servers);
    }
    *next_abstime = butil::seconds_from_now(FLAGS_ns_access_interval);
    return true;
}

void PeriodicNamingService::RunNamingService(
    const char* service_name, NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    const int rc = GetServers(service_name, &servers);
    if (rc != 0) {
        delete actions;
        return;
    }
    actions->ResetServers(servers);
    _task = new AccessNamingServiceTask(this, service_name, actions);
    _task->AddRefManually(); // Add ref for this NS.
    _task->AddRefManually(); // Add ref for the task.
    PeriodicTaskManager::StartTaskAt(
        _task, butil::seconds_from_now(FLAGS_ns_access_interval));
}

void PeriodicNamingService::Destroy() {
    if (_task) {
        _task->CleanUp();
        _task->_scheduled_destroy = true;
        _task->RemoveRefManually();
        _task = NULL;
    }
    delete this;
}

} // namespace brpc
