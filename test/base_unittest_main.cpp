// Copyright (c) 2014 Baidu, Inc.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/resource.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "base/command_line.h"
#include "base/base_switches.h"
#include "base/at_exit.h"
#include "test/test_timeouts.h"
#include "test/multiprocess_func_list.h"

// Disable coredumps by default to avoid generating a lot of coredumps
// after running death tests.
DEFINE_bool(disable_coredump, true, "Never core dump");

int main(int argc, char** argv) {
    base::AtExitManager at_exit;
    base::CommandLine::Init(argc, argv);
    TestTimeouts::Initialize();
    testing::InitGoogleTest(&argc, argv);
    
    std::string client_func =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        switches::kTestChildProcess);
    // Check to see if we are being run as a client process.
    if (!client_func.empty()) {
        return multi_process_function_list::InvokeChildProcessTest(client_func);
    }
    google::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_disable_coredump) {
        rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &core_limit);
    }
    CHECK(!google::SetCommandLineOption("crash_on_fatal_log", "true").empty());
    return RUN_ALL_TESTS();
}
