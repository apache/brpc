// Copyright (c) 2014 Baidu, Inc.

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2015/08/27 17:12:38

#include "bvar/reducer.h"
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <stdlib.h>

class FileDumperTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
};

TEST_F(FileDumperTest, filters) {
    bvar::Adder<int> a1("a_latency");
    bvar::Adder<int> a2("a_qps");
    bvar::Adder<int> a3("a_error");
    bvar::Adder<int> a4("process_*");
    bvar::Adder<int> a5("default");
    GFLAGS_NS::SetCommandLineOption("bvar_dump_interval", "1");
    GFLAGS_NS::SetCommandLineOption("bvar_dump", "true");
    sleep(2);
}
