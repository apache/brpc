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

#include <sys/types.h>                          // open
#include <sys/stat.h>                           // ^
#include <fcntl.h>                              // ^
#include <gtest/gtest.h>
#include <errno.h>
#include "butil/fd_guard.h"

namespace {

class FDGuardTest : public ::testing::Test{
protected:
    FDGuardTest(){
    };
    virtual ~FDGuardTest(){};
    virtual void SetUp() {
    };
    virtual void TearDown() {
    };
};

TEST_F(FDGuardTest, default_constructor) {
    butil::fd_guard guard;
    ASSERT_EQ(-1, guard);
}

TEST_F(FDGuardTest, destructor_closes_fd) {
    int fd = -1;
    {
        butil::fd_guard guard(open(".tmp1",  O_WRONLY|O_CREAT, 0600));
        ASSERT_GT(guard, 0);
        fd = guard;
    }
    char dummy = 0;
    ASSERT_EQ(-1L, write(fd, &dummy, 1));
    ASSERT_EQ(EBADF, errno);
}

TEST_F(FDGuardTest, reset_closes_previous_fd) {
    butil::fd_guard guard(open(".tmp1",  O_WRONLY|O_CREAT, 0600));
    ASSERT_GT(guard, 0);
    const int fd = guard;
    const int fd2 = open(".tmp2",  O_WRONLY|O_CREAT, 0600);
    guard.reset(fd2);
    char dummy = 0;
    ASSERT_EQ(-1L, write(fd, &dummy, 1));
    ASSERT_EQ(EBADF, errno);
    guard.reset(-1);
    ASSERT_EQ(-1L, write(fd2, &dummy, 1));
    ASSERT_EQ(EBADF, errno);
}
    
TEST_F(FDGuardTest, release) {
    butil::fd_guard guard(open(".tmp1",  O_WRONLY|O_CREAT, 0600));
    ASSERT_GT(guard, 0);
    const int fd = guard;
    ASSERT_EQ(fd, guard.release());
    ASSERT_EQ(-1, guard);
    close(fd);
}
}
