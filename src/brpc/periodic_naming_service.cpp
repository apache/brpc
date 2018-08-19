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

namespace brpc {

DEFINE_int32(ns_access_interval, 5,
             "Wait so many seconds before next access to naming service");
BRPC_VALIDATE_GFLAG(ns_access_interval, PositiveInteger);

int PeriodicNamingService::RunNamingService(
    const char* service_name, NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    bool ever_reset = false;
    for (;;) {
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

        if (bthread_usleep(std::max(FLAGS_ns_access_interval, 1) * 1000000L) < 0) {
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
