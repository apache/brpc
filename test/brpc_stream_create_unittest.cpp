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

#include "brpc/controller.h"
#include "brpc/stream.h"

class StreamCreateBugTest : public testing::Test {};

TEST_F(StreamCreateBugTest, create_twice_on_same_controller_returns_error) {
    brpc::Controller cntl;

    brpc::StreamId first_stream = brpc::INVALID_STREAM_ID;
    ASSERT_EQ(0, brpc::StreamCreate(&first_stream, cntl, NULL));

    brpc::StreamId second_stream = brpc::INVALID_STREAM_ID;
    ASSERT_EQ(-1, brpc::StreamCreate(&second_stream, cntl, NULL));
    ASSERT_EQ(brpc::INVALID_STREAM_ID, second_stream);
}
