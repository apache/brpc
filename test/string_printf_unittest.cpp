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

#include <gtest/gtest.h>
#include "butil/string_printf.h"

namespace {

class BaiduStringPrintfTest : public ::testing::Test{
protected:
    BaiduStringPrintfTest(){
    };
    virtual ~BaiduStringPrintfTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

TEST_F(BaiduStringPrintfTest, sanity) {
    ASSERT_EQ("hello 1 124 world", butil::string_printf("hello %d 124 %s", 1, "world"));
    std::string sth;
    ASSERT_EQ(0, butil::string_printf(&sth, "boolean %d", 1));
    ASSERT_EQ("boolean 1", sth);
    
    ASSERT_EQ(0, butil::string_appendf(&sth, "too simple"));
    ASSERT_EQ("boolean 1too simple", sth);
}

}
