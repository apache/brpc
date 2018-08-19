// Copyright (c) 2014 Baidu, Inc.

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
