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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/text_format.h>
#include <string>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/files/scoped_file.h"
#include "butil/fd_guard.h"
#include "butil/file_util.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "echo.pb.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/policy/http2_rpc_protocol.h"
#include "json2pb/pb_to_json.h"
#include "json2pb/json_to_pb.h"
#include "brpc/details/method_status.h"
#include "etcd_client/etcd_client.h"

namespace {

class MockEtcdService : public ::test::EtcdService {
 public:
  void Range(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
            "\"cluster_id\":\"14841639068965178418\","
            "\"member_id\":\"10276657743932975437\","
            "\"revision\":\"110\","
            "\"raft_term\":\"12\""
        "},"
        "\"kvs\":[{"
            "\"key\":\"L3NlcnZpY2UvMTI3LjAuMC4x\","
            "\"create_revision\":\"109\","
            "\"mod_revision\":\"110\","
            "\"version\":\"2\","
            "\"value\":\"MTIzNA==\""
        "}],"
        "\"count\":\"1\""
    "}";
    cntl->response_attachment().append(output);
  }
  void RangeFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock Range fail");
  }
  void Put(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
      "\"header\" : {"
        "\"cluster_id\":\"14841639068965178418\","
        "\"member_id\":\"10276657743932975437\","
        "\"revision\":\"110\","
        "\"raft_term\":\"12\""
    "}}";
    cntl->response_attachment().append(output);
  }
  void PutFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock put fail");
  }
  void DeleteRange(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
            "\"cluster_id\":\"14841639068965178418\","
            "\"member_id\":\"10276657743932975437\","
            "\"revision\":\"113\","
            "\"raft_term\":\"12\""
        "},"
        "\"deleted\":\"1\""
    "}";
    cntl->response_attachment().append(output);
  }
  void DeleteRangeFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock delete range fail");
  }
  void Txn(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
       "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"122\","
           "\"raft_term\":\"12\""
        "},"
       "\"succeeded\":true,"
       "\"responses\":[{"
           "\"response_put\":{"
               "\"header\":{"
                   "\"revision\":\"122\""
                "}"
            "}"
        "}]"
    "}";
    cntl->response_attachment().append(output);
  }
  void TxnFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock txn fail");
  }
  void Compact(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
       "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"128\","
           "\"raft_term\":\"12\""
        "}"
    "}";
    cntl->response_attachment().append(output);
  }
  void CompactFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock compact fail");
  }
  void LeaseGrant(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"128\","
           "\"raft_term\":\"12\""
        "},"
        "\"ID\":\"23459870987\","
        "\"TTL\":\"2\""
    "}";
    cntl->response_attachment().append(output);
  }
  void LeaseGrantFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock lease grant fail");
  }
  void LeaseRevoke(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"128\","
           "\"raft_term\":\"12\""
        "}"
    "}";
    cntl->response_attachment().append(output);
  }
  void LeaseRevokeFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock lease revoke fail");
  }
  void LeaseKeepAlive(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"128\","
           "\"raft_term\":\"12\""
        "},"
        "\"ID\":\"23459870987\","
        "\"TTL\":\"2\""
    "}";
    cntl->response_attachment().append(output);
  }
  void LeaseKeepAliveFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock lease keep alive fail");
  }
  void LeaseTimeToLive(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
           "\"cluster_id\":\"14841639068965178418\","
           "\"member_id\":\"10276657743932975437\","
           "\"revision\":\"128\","
           "\"raft_term\":\"12\""
        "},"
        "\"ID\":\"23459870987\","
        "\"TTL\":\"2\","
        "\"keys\":[\"YWJj\", \"MTIz\"]"
    "}";
    cntl->response_attachment().append(output);
  }
  void LeaseTimeToLiveFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock lease time to live fail");
  }
  void LeaseLeases(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    std::string output = "{"
        "\"header\":{"
            "\"cluster_id\":\"14841639068965178418\","
            "\"member_id\":\"10276657743932975437\","
            "\"revision\":\"128\","
            "\"raft_term\":\"12\""
        "},"
        "\"leases\":["
            "{\"ID\":\"7587863094269227633\"},"
            "{\"ID\":\"7587863094269227635\"}"
    "]}";
    cntl->response_attachment().append(output);
  }
  void LeaseLeasesFail(::google::protobuf::RpcController* cntl_base,
           const ::test::HttpRequest* req,
           ::test::HttpResponse* res,
           ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
    cntl->SetFailed("mock lease leases fail");
  }
};

class EtcdClientTest : public ::testing::Test {
 protected:
  EtcdClientTest() {
    EXPECT_EQ(0, _success_server.AddService(&_success_svc, brpc::SERVER_DOESNT_OWN_SERVICE,
      "/v3/kv/range => Range,"
      "/v3/kv/put => Put,"
      "/v3/kv/deleterange => DeleteRange,"
      "/v3/kv/txn => Txn,"
      "/v3/kv/compaction => Compact,"
      "/v3/lease/grant => LeaseGrant,"
      "/v3/lease/revoke => LeaseRevoke,"
      "/v3/lease/keepalive => LeaseKeepAlive,"
      "/v3/lease/timetolive => LeaseTimeToLive,"
      "/v3/lease/leases => LeaseLeases"));

    EXPECT_EQ(0, _success_server.Start(2389, nullptr));
    EXPECT_EQ(0, _fail_server.AddService(&_fail_svc, brpc::SERVER_DOESNT_OWN_SERVICE,
      "/v3/kv/range => RangeFail,"
      "/v3/kv/put => PutFail,"
      "/v3/kv/deleterange => DeleteRangeFail,"
      "/v3/kv/txn => TxnFail,"
      "/v3/kv/compaction => CompactFail,"
      "/v3/lease/grant => LeaseGrantFail,"
      "/v3/lease/revoke => LeaseRevokeFail,"
      "/v3/lease/keepalive => LeaseKeepAliveFail,"
      "/v3/lease/timetolive => LeaseTimeToLiveFail,"
      "/v3/lease/leases => LeaseLeasesFail"));
    EXPECT_EQ(0, _fail_server.Start(2399, nullptr));
  }
  virtual void TearDown() {
  }
  brpc::Server _success_server;
  MockEtcdService _success_svc;
  brpc::Server _fail_server;
  MockEtcdService _fail_svc;
};

TEST_F(EtcdClientTest, etcd_client_init) {
  brpc::EtcdClient client;
  EXPECT_TRUE(client.Init("localhost:2389"));
}

TEST_F(EtcdClientTest, etcd_op_success) {
  brpc::EtcdClient client;
  EXPECT_TRUE(client.Init("localhost:2389"));
  {
    // test Put
    etcdserverpb::PutRequest request;
    request.set_key("/service/127.0.0.1");
    request.set_value("1");
    etcdserverpb::PutResponse response;
    EXPECT_TRUE(client.Put(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 110);
    EXPECT_EQ(response.header().raft_term(), 12u);
  }
  {
    // test Range
    etcdserverpb::RangeRequest request;
    request.set_key("/service/127.0.0.1");
    etcdserverpb::RangeResponse response;
    EXPECT_TRUE(client.Range(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 110);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.kvs_size(), 1);
    EXPECT_EQ(response.kvs(0).key(), "/service/127.0.0.1");
    EXPECT_EQ(response.kvs(0).value(), "1234");
  }
  {
    // test DeleteRange
    etcdserverpb::DeleteRangeRequest request;
    request.set_key("/service/127.0.0.1");
    etcdserverpb::DeleteRangeResponse response;
    EXPECT_TRUE(client.DeleteRange(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 113);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.deleted(), 1);
  }
  {
    // test Txn
    etcdserverpb::TxnRequest request;
    auto* compare = request.add_compare();
    compare->set_result(::etcdserverpb::Compare::EQUAL);
    compare->set_target(::etcdserverpb::Compare::VALUE);
    compare->set_key("/service/127.0.0.1");
    compare->set_value("1234");
    auto* put = request.add_success()->mutable_request_put();
    put->set_key("/service/127.0.0.1");
    put->set_value("9876");
    etcdserverpb::TxnResponse response;
    EXPECT_TRUE(client.Txn(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 122);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.succeeded(), 1);
  }
  {
    // test Compaction
    etcdserverpb::CompactionRequest request;
    request.set_revision(126);
    etcdserverpb::CompactionResponse response;
    EXPECT_TRUE(client.Compact(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
  }
  {
    // test LeaseGrant
    etcdserverpb::LeaseGrantRequest request;
    request.set_ttl(2);
    request.set_id(23459870987);
    etcdserverpb::LeaseGrantResponse response;
    EXPECT_TRUE(client.LeaseGrant(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.id(), 23459870987);
    EXPECT_EQ(response.ttl(), 2u);
  }
  {
    // test  revoke
    etcdserverpb::LeaseRevokeRequest request;
    request.set_id(23459870987);
    etcdserverpb::LeaseRevokeResponse response;
    EXPECT_TRUE(client.LeaseRevoke(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
  }
  {
    // test keepalive
    etcdserverpb::LeaseKeepAliveRequest request;
    request.set_id(23459870987);
    etcdserverpb::LeaseKeepAliveResponse response;
    EXPECT_TRUE(client.LeaseKeepAlive(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.id(), 23459870987);
    EXPECT_EQ(response.ttl(), 2u);
  }
  {
    // test time to live
    etcdserverpb::LeaseTimeToLiveRequest request;
    request.set_id(23459870987);
    request.set_keys(true);
    etcdserverpb::LeaseTimeToLiveResponse response;
    EXPECT_TRUE(client.LeaseTimeToLive(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.id(), 23459870987);
    EXPECT_EQ(response.ttl(), 2u);
    EXPECT_EQ(response.keys_size(), 2);
    EXPECT_EQ(response.keys(0), "abc");
    EXPECT_EQ(response.keys(1), "123");
  }
  {
    // test lease leases
    etcdserverpb::LeaseLeasesRequest request;
    etcdserverpb::LeaseLeasesResponse response;
    EXPECT_TRUE(client.LeaseLeases(request, &response));
    EXPECT_EQ(response.header().cluster_id(), 14841639068965178418u);
    EXPECT_EQ(response.header().member_id(), 10276657743932975437u);
    EXPECT_EQ(response.header().revision(), 128);
    EXPECT_EQ(response.header().raft_term(), 12u);
    EXPECT_EQ(response.leases(0).id(), 7587863094269227633);
    EXPECT_EQ(response.leases(1).id(), 7587863094269227635);
  }
}

TEST_F(EtcdClientTest, etcd_op_fail) {
  brpc::EtcdClient client;
  EXPECT_TRUE(client.Init("localhost:2399"));
  {
    // Put fail
    etcdserverpb::PutRequest request;
    request.set_key("/service/127.0.0.1");
    request.set_value("1");
    etcdserverpb::PutResponse response;
    EXPECT_FALSE(client.Put(request, &response));
  }
  {
    // Range fail
    etcdserverpb::RangeRequest request;
    request.set_key("/service/127.0.0.1");
    etcdserverpb::RangeResponse response;
    EXPECT_FALSE(client.Range(request, &response));
  }
  {
    // DeleteRange fail
    etcdserverpb::DeleteRangeRequest request;
    request.set_key("/service/127.0.0.1");
    etcdserverpb::DeleteRangeResponse response;
    EXPECT_FALSE(client.DeleteRange(request, &response));
  }
  {
    // Txn fail
    etcdserverpb::TxnRequest request;
    auto* compare = request.add_compare();
    compare->set_result(::etcdserverpb::Compare::EQUAL);
    compare->set_target(::etcdserverpb::Compare::VALUE);
    compare->set_key("/service/127.0.0.1");
    compare->set_value("1234");
    auto* put = request.add_success()->mutable_request_put();
    put->set_key("/service/127.0.0.1");
    put->set_value("9876");
    etcdserverpb::TxnResponse response;
    EXPECT_FALSE(client.Txn(request, &response));
  }
  {
    // Compact fail
    etcdserverpb::CompactionRequest request;
    request.set_revision(126);
    etcdserverpb::CompactionResponse response;
    EXPECT_FALSE(client.Compact(request, &response));
  }
  {
    // LeaseGrant fail
    etcdserverpb::LeaseGrantRequest request;
    request.set_ttl(2);
    request.set_id(23459870987);
    etcdserverpb::LeaseGrantResponse response;
    EXPECT_FALSE(client.LeaseGrant(request, &response));
  }
  {
    // LeaseRevoke fail
    etcdserverpb::LeaseRevokeRequest request;
    request.set_id(23459870987);
    etcdserverpb::LeaseRevokeResponse response;
    EXPECT_FALSE(client.LeaseRevoke(request, &response));
  }
  {
    // test keepalive
    etcdserverpb::LeaseKeepAliveRequest request;
    request.set_id(23459870987);
    etcdserverpb::LeaseKeepAliveResponse response;
    EXPECT_FALSE(client.LeaseKeepAlive(request, &response));
  }
  {
    // test time to live
    etcdserverpb::LeaseTimeToLiveRequest request;
    request.set_id(23459870987);
    etcdserverpb::LeaseTimeToLiveResponse response;
    EXPECT_FALSE(client.LeaseTimeToLive(request, &response));
  }
  {
    etcdserverpb::LeaseLeasesRequest request;
    etcdserverpb::LeaseLeasesResponse response;
    EXPECT_FALSE(client.LeaseLeases(request, &response));
  }
  {
    // test lease leases
    etcdserverpb::LeaseLeasesRequest request;
    etcdserverpb::LeaseLeasesResponse response;
    EXPECT_FALSE(client.LeaseLeases(request, &response));
  }
}
}  // namespace
