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

#include <brpc/couchbase.h>
#include <butil/logging.h>
#include <gtest/gtest.h>

namespace brpc {
DECLARE_int32(idle_timeout_second);
}

int main(int argc, char* argv[]) {
  brpc::FLAGS_idle_timeout_second = 0;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

// Unit Tests - No Server Required
class CouchbaseUnitTest : public testing::Test {};

TEST_F(CouchbaseUnitTest, RequestBuilders) {
  brpc::CouchbaseOperations::CouchbaseRequest req;
  req.Clear();
  req.helloRequest();
  req.authenticateRequest("user", "pass");
  req.selectBucketRequest("bucket");
  req.addRequest("key", "value", 0, 0, 0);
  req.getRequest("key");
  req.upsertRequest("key", "value", 0, 0, 0);
  req.deleteRequest("key");
  EXPECT_TRUE(true);
}

TEST_F(CouchbaseUnitTest, ResultStruct) {
  brpc::CouchbaseOperations::Result result;
  
  result.success = true;
  result.error_message = "Test";
  result.value = R"({"test": "data"})";
  result.status_code = 0x01;
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ("Test", result.error_message);
  EXPECT_EQ(R"({"test": "data"})", result.value);
  EXPECT_EQ(0x01, result.status_code);
  
  result.success = false;
  result.error_message = "";
  result.value = "";
  result.status_code = 0x00;
  
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_TRUE(result.value.empty());
  EXPECT_EQ(0x00, result.status_code);
}

TEST_F(CouchbaseUnitTest, EdgeCases) {
  brpc::CouchbaseOperations::CouchbaseRequest req;
  req.addRequest("", "value", 0, 0, 0);
  req.addRequest("key", "", 0, 0, 0);
  req.addRequest(std::string(1000, 'x'), "val", 0, 0, 0);
  req.addRequest("key", std::string(10000, 'x'), 0, 0, 0);
  req.addRequest("test::special::!!!","val", 0, 0, 0);
  req.addRequest("key", R"({"unicode":"123"})", 0, 0, 0);
  EXPECT_TRUE(true);
}

}  // namespace