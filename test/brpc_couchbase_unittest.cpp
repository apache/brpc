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
#include <brpc/policy/couchbase_protocol.h>
#include <brpc/socket.h>
#include <butil/sys_byteorder.h>
#include <butil/iobuf.h>
#include <butil/logging.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <limits>

namespace brpc {
DECLARE_int32(idle_timeout_second);
DECLARE_uint64(max_body_size);
}

int main(int argc, char* argv[]) {
  brpc::FLAGS_idle_timeout_second = 0;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

// Unit Tests - No Server Required
class CouchbaseUnitTest : public testing::Test {};

TEST_F(CouchbaseUnitTest, RequestLengthLimits) {
  const size_t max_body = std::numeric_limits<uint32_t>::max();
  EXPECT_TRUE(brpc::policy::ValidCouchbaseRequestLengths(1, 1, 8, max_body - 10));
  EXPECT_FALSE(brpc::policy::ValidCouchbaseRequestLengths(1, 1, 8, max_body - 9));
  EXPECT_FALSE(brpc::policy::ValidCouchbaseRequestLengths(65535, 1, 0, 0));
  EXPECT_TRUE(brpc::policy::ValidCouchbaseAuthLengths(1, max_body - 9));
  EXPECT_FALSE(brpc::policy::ValidCouchbaseAuthLengths(1, max_body - 8));
  EXPECT_FALSE(brpc::policy::ValidCouchbaseAuthLengths(max_body, 0));
}

TEST_F(CouchbaseUnitTest, RejectOversizedRequestKeysWithoutChangingBuffer) {
  brpc::CouchbaseOperations::CouchbaseRequest req;
  const std::string max_key(65534, 'k');
  const std::string oversized_key(65535, 'k');
  const std::string oversized_bucket(65536, 'b');

  ASSERT_TRUE(req.getRequest(max_key));
  brpc::policy::CouchbaseRequestHeader header;
  ASSERT_EQ(sizeof(header), req.rawBuffer().copy_to(&header, sizeof(header)));
  EXPECT_EQ(65535, butil::NetToHost16(header.key_length));
  EXPECT_EQ(65535u, butil::NetToHost32(header.total_body_length));
  const size_t size = req.rawBuffer().size();
  const int count = req.pipelinedCount();
  EXPECT_FALSE(req.getRequest(oversized_key));
  EXPECT_FALSE(req.deleteRequest(oversized_key));
  EXPECT_FALSE(req.upsertRequest(oversized_key, "value", 0, 0, 0));
  EXPECT_FALSE(req.addRequest(oversized_key, "value", 0, 0, 0));
  EXPECT_FALSE(req.selectBucketRequest(oversized_bucket));
  EXPECT_EQ(size, req.rawBuffer().size());
  EXPECT_EQ(count, req.pipelinedCount());

  std::unordered_map<std::string, brpc::CouchbaseManifestManager::CollectionManifest> cache;
  brpc::CouchbaseOperations::CouchbaseRequest collection_req(&cache);
  EXPECT_FALSE(collection_req.getRequest(oversized_key, "custom"));
  EXPECT_FALSE(collection_req.deleteRequest(oversized_key, "custom"));
  EXPECT_FALSE(collection_req.upsertRequest(oversized_key, "value", 0, 0, 0, "custom"));
  EXPECT_FALSE(collection_req.addRequest(oversized_key, "value", 0, 0, 0, "custom"));
  EXPECT_FALSE(collection_req.appendRequest(oversized_key, "value", 0, 0, 0, "custom"));
  EXPECT_FALSE(collection_req.prependRequest(oversized_key, "value", 0, 0, 0, "custom"));
  EXPECT_TRUE(cache.empty());
  EXPECT_TRUE(collection_req.rawBuffer().empty());
  EXPECT_EQ(0, collection_req.pipelinedCount());

  brpc::CouchbaseOperations::CouchbaseRequest bucket_req;
  ASSERT_TRUE(bucket_req.selectBucketRequest(std::string(65535, 'b')));
  ASSERT_EQ(sizeof(header), bucket_req.rawBuffer().copy_to(&header, sizeof(header)));
  EXPECT_EQ(65535, butil::NetToHost16(header.key_length));
  EXPECT_EQ(65535u, butil::NetToHost32(header.total_body_length));
}

TEST_F(CouchbaseUnitTest, OversizedBucketReportsDeterministicError) {
  brpc::CouchbaseOperations operations;
  const brpc::CouchbaseOperations::Result result =
      operations.selectBucket(std::string(65536, 'b'));

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.value.empty());
  EXPECT_EQ("Couchbase request exceeds protocol length limits",
            result.error_message);
  EXPECT_EQ(0, result.status_code);
}

TEST_F(CouchbaseUnitTest, ManifestJsonNesting) {
  brpc::CouchbaseManifestManager manager;
  brpc::CouchbaseManifestManager::CollectionManifest manifest;
  const std::string prefix = "{\"uid\":\"1\",\"scopes\":[],\"extra\":";
  auto make_json = [&](size_t depth) {
    return prefix + std::string(depth, '[') + "0"
        + std::string(depth, ']') + "}";
  };
  // The root object plus 99 arrays reaches the accepted depth of 100.
  // Keep explicit expectations to independently check this boundary.
  EXPECT_TRUE(manager.jsonToCollectionManifest(make_json(99), &manifest));
  EXPECT_FALSE(manager.jsonToCollectionManifest(make_json(100), &manifest));
  EXPECT_FALSE(manager.jsonToCollectionManifest(make_json(20000), &manifest));
  EXPECT_TRUE(manager.jsonToCollectionManifest(
      "{\"uid\":\"1\",\"scopes\":[],\"extra\":\"[\\\"{]\"}", &manifest));
  std::string with_nul = "{\"uid\":\"1\",\"scopes\":[]}";
  with_nul.push_back('\0');
  with_nul += "[]";
  EXPECT_FALSE(manager.jsonToCollectionManifest(with_nul, &manifest));
}

TEST_F(CouchbaseUnitTest, RejectOversizedResponseBeforeBufferingBody) {
  GFLAGS_NAMESPACE::FlagSaver flag_saver;
  brpc::FLAGS_max_body_size = 1024;

  brpc::SocketId id;
  brpc::SocketOptions options;
  ASSERT_EQ(0, brpc::Socket::Create(options, &id));
  brpc::SocketUniquePtr socket;
  ASSERT_EQ(0, brpc::Socket::Address(id, &socket));
  ASSERT_EQ(0, socket->ReleaseAdditionalReference());

  brpc::policy::CouchbaseResponseHeader couchbase_header = {};
  couchbase_header.magic = brpc::policy::CB_MAGIC_RESPONSE;
  couchbase_header.total_body_length = butil::HostToNet32(1025);
  butil::IOBuf couchbase_buf;
  couchbase_buf.append(&couchbase_header, sizeof(couchbase_header));
  EXPECT_EQ(brpc::PARSE_ERROR_TOO_BIG_DATA,
            brpc::policy::ParseCouchbaseMessage(
                &couchbase_buf, socket.get(), false, nullptr).error());

  socket.reset();
  EXPECT_EQ(-1, brpc::Socket::Status(id));
}

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

// The header fields are attacker-controlled, extras_length + key_length may
// exceed total_body_length. popManifest/popCollectionId must not treat the
// underflowed remainder as a value size and drain the pipelined buffer.
TEST_F(CouchbaseUnitTest, CollectionResponseWithInconsistentBodyLength) {
  const std::string next_response(64, 'A');

  brpc::policy::CouchbaseResponseHeader header = {};
  header.magic = brpc::policy::CB_MAGIC_RESPONSE;
  header.command = brpc::policy::CB_GET_COLLECTIONS_MANIFEST;
  header.extras_length = 1;
  header.total_body_length = 0;

  brpc::CouchbaseOperations::CouchbaseResponse res;
  res.rawBuffer().append(&header, sizeof(header));
  res.rawBuffer().append(next_response);
  std::string manifest = "untouched";
  EXPECT_FALSE(res.popManifest(&manifest));
  EXPECT_EQ("untouched", manifest);
  EXPECT_EQ(sizeof(header) + next_response.size(), res.rawBuffer().size());

  header.command = brpc::policy::CB_COLLECTIONS_GET_CID;
  header.status = 1;
  brpc::CouchbaseOperations::CouchbaseResponse res2;
  res2.rawBuffer().append(&header, sizeof(header));
  res2.rawBuffer().append(next_response);
  uint8_t collection_id = 0;
  EXPECT_FALSE(res2.popCollectionId(&collection_id));
  EXPECT_EQ(sizeof(header) + next_response.size(), res2.rawBuffer().size());
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
