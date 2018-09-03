// Copyright (c) 2018 Bilibili, Inc.
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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "brpc/grpc.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (GFLAGS_NS::SetCommandLineOption("crash_on_fatal_log", "true").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    return RUN_ALL_TESTS();
}

namespace {

class GrpcTest : public ::testing::Test {
protected:
    GrpcTest() {}

    virtual ~GrpcTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};
};

TEST_F(GrpcTest, percent_encode) {
    std::string out;
    std::string s1("abcdefg !@#$^&*()/");
    brpc::percent_encode(s1, &out);
    EXPECT_TRUE(out == s1) << s1 << " vs " << out;

    char s2_buf[] = "\0\0%\33\35 brpc";
    std::string s2(s2_buf, sizeof(s2_buf) - 1);
    std::string s2_expected_out("%00%00%25%1b%1d brpc");
    brpc::percent_encode(s2, &out);
    EXPECT_TRUE(out == s2_expected_out) << s2_expected_out << " vs " << out;
}

TEST_F(GrpcTest, percent_decode) {
    std::string out;
    std::string s1("abcdefg !@#$^&*()/");
    brpc::percent_decode(s1, &out);
    EXPECT_TRUE(out == s1) << s1 << " vs " << out;

    std::string s2("%00%00%1b%1d brpc");
    char s2_expected_out_buf[] = "\0\0\33\35 brpc";
    std::string s2_expected_out(s2_expected_out_buf, sizeof(s2_expected_out_buf) - 1);
    brpc::percent_decode(s2, &out);
    EXPECT_TRUE(out == s2_expected_out) << s2_expected_out << " vs " << out;
}

} // namespace 
