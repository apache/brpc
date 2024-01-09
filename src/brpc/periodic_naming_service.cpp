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


#include <gflags/gflags.h>
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "brpc/log.h"
#include "brpc/reloadable_flags.h"
#include "brpc/periodic_naming_service.h"

namespace brpc {

DEFINE_int32(ns_access_interval, 5,
             "Wait so many seconds before next access to naming service");
BRPC_VALIDATE_GFLAG(ns_access_interval, PositiveInteger);

int PeriodicNamingService::GetNamingServiceAccessIntervalMs() const {
    return std::max(FLAGS_ns_access_interval, 1) * 1000;
}

int PeriodicNamingService::RunNamingService(
    const char* service_name, NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    bool ever_reset = false;
    while (true) {
        servers.clear();
        const int rc = GetServers(service_name, &servers);
        if (rc == 0) {
            ever_reset = true;
            actions->ResetServers(servers);
        } else if (!ever_reset) {
            // ResetServers must be called at first time even if GetServers
            // failed, to wake up callers to `WaitForFirstBatchOfServers'
            ever_reset = true;
            servers.clear();
            actions->ResetServers(servers);
        }

        // If `bthread_stop' is called to stop the ns bthread when `brpc::Join‘ is called
        // in `GetServers' to wait for a rpc to complete. The bthread will be woken up,
        // reset `TaskMeta::interrupted' and continue to join the rpc. After the rpc is complete,
        // `bthread_usleep' will not sense the interrupt signal and sleep successfully.
        // Finally, the ns bthread will never exit. So need to check the stop status of
        // the bthread here and exit the bthread in time.
        if (bthread_stopped(bthread_self())) {
            RPC_VLOG << "Quit NamingServiceThread=" << bthread_self();
            return 0;
        }
        if (bthread_usleep(GetNamingServiceAccessIntervalMs() * 1000UL) < 0) {
            if (errno == ESTOP) {
                RPC_VLOG << "Quit NamingServiceThread=" << bthread_self();
                return 0;
            }
            PLOG(FATAL) << "Fail to sleep";
            return -1;
        }
    }
}

} // namespace brpc
