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

#include "repeated.pb.h"
#include <gtest/gtest.h>
#include <json2pb/pb_to_json.h>

class RepeatedFieldTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
};

TEST_F(RepeatedFieldTest, empty_array) {
    RepeatedMessage m;
    std::string json;

    ASSERT_TRUE(json2pb::ProtoMessageToJson(m, &json));
    std::cout << json << std::endl;

    m.add_strings();
    m.add_ints(1);
    m.add_msgs();
    json.clear();
    ASSERT_TRUE(json2pb::ProtoMessageToJson(m, &json));
    std::cout << json << std::endl;
}
