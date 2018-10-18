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

// Authors: Ge,Jun (gejun@baidu.com)

#include <gflags/gflags.h>
#include "butil/logging.h"
#include "butil/file_util.h"
#include "bthread/bthread.h"
#include "brpc/log.h"
#include "brpc/reloadable_flags.h"
#include "brpc/periodic_naming_service.h"
#include "butil/files/scoped_file.h"            // ScopedFILE

namespace brpc {
namespace policy {
// Defined in file_naming_service.cpp
bool SplitIntoServerAndTag(const butil::StringPiece& line,
                           butil::StringPiece* server_addr,
                           butil::StringPiece* tag);
int GetServersFromFile(const char *service_name,
                       std::vector<ServerNode>* servers);
}

DEFINE_int32(ns_access_interval, 5,
             "Wait so many seconds before next access to naming service");
BRPC_VALIDATE_GFLAG(ns_access_interval, PositiveInteger);

DEFINE_string(backup_dir_when_ns_fails, "", "When the first GetServers fails"
        ", ns will search this directory for backup file");

void SaveServersToFile(const std::string& file_path,
                             const std::vector<ServerNode>& servers) {
    size_t pos = file_path.find_last_of('/');
    if (pos != std::string::npos) {
        butil::FilePath fp(file_path.substr(0, pos));
        butil::CreateDirectoryAndGetError(fp, NULL, true);
    }
    FILE *fp = fopen(file_path.c_str(), "w");
    if (!fp) {
        LOG(ERROR) << "Fail to open `" << file_path << "' to save naming service results";
        return;
    }
    butil::EndPointStr epstr;
    for (int i = 0; i < (int)servers.size(); ++i) {
        epstr = butil::endpoint2str(servers[i].addr);
        fprintf(fp, "%s %s\n", epstr.c_str(), servers[i].tag.c_str());
    }
    fclose(fp);
}

int PeriodicNamingService::RunNamingService(
    const char* service_name, NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    bool ever_reset = false;
    std::string file_path;
    std::ostringstream os;
    Describe(os, DescribeOptions());
    if (!FLAGS_backup_dir_when_ns_fails.empty()) {
        file_path.append(FLAGS_backup_dir_when_ns_fails);
        file_path.push_back('/');
        file_path.append(os.str());
        file_path.push_back('/');
        file_path.append(service_name);
    }
    for (;;) {
        servers.clear();
        const int rc = GetServers(service_name, &servers);
        if (rc == 0) {
            ever_reset = true;
            bool server_changed = actions->ResetServers(servers);
            if (server_changed && !FLAGS_backup_dir_when_ns_fails.empty()) {
                SaveServersToFile(file_path, servers);
            }
        } else if (!ever_reset) {
            // ResetServers must be called at first time even if GetServers
            // failed, to wake up callers to `WaitForFirstBatchOfServers'
            ever_reset = true;
            servers.clear();
            if (!FLAGS_backup_dir_when_ns_fails.empty()) {
                policy::GetServersFromFile(file_path.c_str(), &servers);
            }
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
