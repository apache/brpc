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

// brpc - A framework to host and access services throughout Baidu.

// Date: Sun Jul 13 15:04:18 CST 2014

#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/extension.h"

class ExtensionTest : public ::testing::Test{
protected:
    ExtensionTest(){
    };
    virtual ~ExtensionTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

inline brpc::Extension<const int>* ConstIntExtension() {
    return brpc::Extension<const int>::instance();
}

inline brpc::Extension<int>* IntExtension() {
    return brpc::Extension<int>::instance();
}


const int g_foo = 10;
const int g_bar = 20;
    
TEST_F(ExtensionTest, basic) {
    ConstIntExtension()->Register("foo", NULL);
    ConstIntExtension()->Register("foo", &g_foo);
    ConstIntExtension()->Register("bar", &g_bar);

    int* val1 = new int(0xbeef);
    int* val2 = new int(0xdead);
    ASSERT_EQ(0, IntExtension()->Register("hello", val1));
    ASSERT_EQ(-1, IntExtension()->Register("hello", val1));
    IntExtension()->Register("there", val2);
    ASSERT_EQ(val1, IntExtension()->Find("hello"));
    ASSERT_EQ(val2, IntExtension()->Find("there"));
    std::ostringstream os;
    IntExtension()->List(os, ':');
    ASSERT_EQ("hello:there", os.str());

    os.str("");
    ConstIntExtension()->List(os, ':');
    ASSERT_EQ("foo:bar", os.str());
}
