// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 20:02:37 CST 2014

#include <gflags/gflags.h>
#include "base/logging.h"
#include "bthread/bthread.h"
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
    CHECK(false);
    return -1;
}

} // namespace brpc

