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

// Values bvar produces: integers, doubles (DoubleToString may omit the
// integer part, as in ".5") and exponent form.
TEST_F(PrometheusMetricsDumperTest, IsDumpableToPrometheus_numbers) {
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("0"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("42"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("-5"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("+7"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("3.14"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("-0.5"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus(".5"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("-.5"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("123."));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e10"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("1E10"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e+10"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e-10"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("1.5e-3"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus(".5e2"));
}

// Prometheus accepts these specials, case-insensitively; unlike
// butil::StringToDouble, whose behavior on them is undefined.
TEST_F(PrometheusMetricsDumperTest, IsDumpableToPrometheus_specials) {
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("+Inf"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("-Inf"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("inf"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("INF"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("NaN"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("nan"));
  EXPECT_TRUE(brpc::IsDumpableToPrometheus("nAn"));
}

// A quoted string, a json object/array (Window<Histogram>, compound
// PassiveStatus) and -- what sniffing only the first char let through --
// the bare `true'/'false' of a bool gflag.
TEST_F(PrometheusMetricsDumperTest, IsDumpableToPrometheus_non_numbers) {
  EXPECT_FALSE(brpc::IsDumpableToPrometheus(""));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("true"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("false"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("True"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("\"running\""));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("{\"count\":4}"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("[1,2,3]"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("null"));
  // The spelled-out "Infinity" is not prometheus sample-value grammar either,
  // no matter what Go's strconv.ParseFloat thinks of it.
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("Infinity"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("-infinity"));
}

// Malformed numbers that a prefix parser such as strtod would accept.
TEST_F(PrometheusMetricsDumperTest, IsDumpableToPrometheus_malformed) {
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("+"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("-"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("."));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("e5"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e+"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1.5.6"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("12abc"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1,2"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus(" 1"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1 "));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("0x10"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("infx"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("in"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("na"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("--1"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1..2"));
  EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e2e3"));
}

}
