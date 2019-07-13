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
#include "butil/errno.h"

class ErrnoTest : public ::testing::Test{
protected:
    ErrnoTest(){
    };
    virtual ~ErrnoTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

#define ESTOP -114
#define EBREAK -115
#define ESTH -116
#define EOK -117
#define EMYERROR -30

BAIDU_REGISTER_ERRNO(ESTOP, "the thread is stopping")
BAIDU_REGISTER_ERRNO(EBREAK, "the thread is interrupted")
BAIDU_REGISTER_ERRNO(ESTH, "something happened")
BAIDU_REGISTER_ERRNO(EOK, "OK!")
BAIDU_REGISTER_ERRNO(EMYERROR, "my error");

TEST_F(ErrnoTest, system_errno) {
    errno = EPIPE;
    ASSERT_STREQ("Broken pipe", berror());
    ASSERT_STREQ("Interrupted system call", berror(EINTR));
}

TEST_F(ErrnoTest, customized_errno) {
    ASSERT_STREQ("the thread is stopping", berror(ESTOP));
    ASSERT_STREQ("the thread is interrupted", berror(EBREAK));
    ASSERT_STREQ("something happened", berror(ESTH));
    ASSERT_STREQ("OK!", berror(EOK));
    ASSERT_STREQ("Unknown error 1000", berror(1000));
    
    errno = ESTOP;
    printf("Something got wrong, %m\n");
    printf("Something got wrong, %s\n", berror());
}
