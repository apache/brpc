// Copyright (c) 2017 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2017/11/06 10:57:08

#include "butil/popen.h"
#include "butil/errno.h"
#include "butil/strings/string_piece.h"
#include <gtest/gtest.h>

namespace {

class PopenTest : public testing::Test {
};

TEST(PopenTest, sanity) {
    std::ostringstream oss;
    int rc = butil::read_command_output(oss, "echo \"Hello World\"");
    ASSERT_EQ(0, rc) << berror(errno);
    ASSERT_EQ("Hello World\n", oss.str());

    oss.str("");
    rc = butil::read_command_output(oss, "exit 1");
    ASSERT_EQ(1, rc) << berror(errno);
    ASSERT_TRUE(oss.str().empty()) << oss.str();
    oss.str("");
    rc = butil::read_command_output(oss, "kill -9 $$");
    ASSERT_EQ(-1, rc);
    ASSERT_EQ(errno, ECHILD);
    ASSERT_TRUE(butil::StringPiece(oss.str()).ends_with("was killed by signal 9"));
    oss.str("");
    rc = butil::read_command_output(oss, "kill -15 $$");
    ASSERT_EQ(-1, rc);
    ASSERT_EQ(errno, ECHILD);
    ASSERT_TRUE(butil::StringPiece(oss.str()).ends_with("was killed by signal 15"));

    oss.str("");
    ASSERT_EQ(0, butil::read_command_output(oss, "for i in `seq 1 100000`; do echo -n '=' ; done"));
    ASSERT_EQ(100000u, oss.str().length()) << oss.str();
    std::string expected;
    expected.resize(100000, '=');
    ASSERT_EQ(expected, oss.str());
}

}
