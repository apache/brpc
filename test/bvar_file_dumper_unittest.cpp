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
    GFLAGS_NAMESPACE::SetCommandLineOption("bvar_dump_interval", "1");
    GFLAGS_NAMESPACE::SetCommandLineOption("bvar_dump", "true");
    sleep(2);
}
