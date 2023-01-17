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

#include <sys/resource.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/at_exit.h"
#include "butil/logging.h"
#include "multiprocess_func_list.h"

DEFINE_bool(disable_coredump, false, "Never core dump");

int main(int argc, char** argv) {
    butil::AtExitManager at_exit;
    testing::InitGoogleTest(&argc, argv);
    
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_disable_coredump) {
        rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &core_limit);
    }
#if !BRPC_WITH_GLOG
    CHECK(!GFLAGS_NS::SetCommandLineOption("crash_on_fatal_log", "true").empty());
#endif
    return RUN_ALL_TESTS();
}
