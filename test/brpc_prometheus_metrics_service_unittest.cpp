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

// Date: 2023/05/06 15:10:00

#include <gtest/gtest.h>

#include "butil/strings/string_piece.h"
#include "butil/iobuf.h"
#include "brpc/builtin/prometheus_metrics_service.h"

namespace {

class PrometheusMetricsDumperTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
};

TEST_F(PrometheusMetricsDumperTest, GetMetricsName) {
  EXPECT_EQ("", brpc::GetMetricsName(""));

  EXPECT_EQ("commit_count", brpc::GetMetricsName("commit_count"));

  EXPECT_EQ("commit_count", brpc::GetMetricsName("commit_count{region=\"1000\"}"));
}

}
