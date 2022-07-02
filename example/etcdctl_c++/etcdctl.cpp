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

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include <thread>
#include <memory>
#include "butil/base64.h"
#include "google/protobuf/util/json_util.h"
#include "etcd_client/proto/rpc.pb.h"
#include "etcd_client/etcd_client.h"

DEFINE_string(url, "localhost:2379", "connect to etcd server");
DEFINE_string(api, "", "etcd api");
DEFINE_string(key, "", "key");
DEFINE_string(value, "", "value");
DEFINE_string(range_end, "", "range end");


class MyWatcher : public brpc::Watcher {
 public:
  void OnEventResponse(const ::etcdserverpb::WatchResponse& response) override {
    LOG(INFO) << response.DebugString();
  }
  void OnParseError() {
    LOG(INFO) << "parser error";
  }
};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    brpc::EtcdClient client;
    LOG(INFO) << "FLAGS_url " << FLAGS_url;
    const char* url = FLAGS_url.c_str();
    client.Init(url);
    if (FLAGS_api == "watch") {
      std::shared_ptr<MyWatcher> watcher(new MyWatcher);
      ::etcdserverpb::WatchRequest request;
      auto* create_request = request.mutable_create_request();
      create_request->set_key(FLAGS_key);
      if (!FLAGS_range_end.empty()) {
        create_request->set_range_end(FLAGS_range_end);
      }
      client.Watch(request, watcher);
      while (1) {
        // ctrl c to exit
        sleep(10);
      }
    } else if (FLAGS_api == "put") {
      ::etcdserverpb::PutRequest request;
      ::etcdserverpb::PutResponse response;
      request.set_key(FLAGS_key);
      if (!FLAGS_value.empty()) {
        request.set_value(FLAGS_value);
      }
      LOG(INFO) << "Put Request: " << request.DebugString();
      client.Put(request, &response);
      LOG(INFO) << "Put response: " << response.DebugString();
    } else if (FLAGS_api == "range") {
      ::etcdserverpb::RangeRequest request;
      ::etcdserverpb::RangeResponse response;
      request.set_key(FLAGS_key);
      if (!FLAGS_range_end.empty()) {
        request.set_range_end(FLAGS_range_end);
      }
      LOG(INFO) << "Range Request: " << request.DebugString();
      client.Range(request, &response);
      LOG(INFO) << "Range response: " << response.DebugString();
    } else {
      LOG(ERROR) << "unknown api " << FLAGS_api;
    }
    return 0;
}
