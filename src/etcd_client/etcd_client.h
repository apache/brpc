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

#ifndef SRC_ETCD_CLIENT_ETCD_CLIENT_H_
#define SRC_ETCD_CLIENT_ETCD_CLIENT_H_
#include <string>
#include <memory>
#include "brpc/channel.h"
#include "etcd_client/proto/rpc.pb.h"
#include "butil/endpoint.h"

namespace brpc {

// The routine of Watcher will be invoked by different event read thread
// Users should ensure thread safe on the process of event
class Watcher {
 public:
  virtual void OnEventResponse(const ::etcdserverpb::WatchResponse& response) {}
  virtual void OnParseError() {}
};
typedef std::shared_ptr<Watcher> WatcherPtr;

class EtcdClient {
 public:
  EtcdClient();
  ~EtcdClient();
  bool Init(const std::string& url, const std::string& lb = "rr");
  bool Init(butil::EndPoint server_addr_and_port);
  bool Init(const char* server_addr_and_port);
  bool Init(const char* server_addr, int port);

  bool Put(const ::etcdserverpb::PutRequest& request,
      ::etcdserverpb::PutResponse* response);

  bool Range(const ::etcdserverpb::RangeRequest& request,
      ::etcdserverpb::RangeResponse* response);

  bool DeleteRange(const ::etcdserverpb::DeleteRangeRequest& request,
      ::etcdserverpb::DeleteRangeResponse* response);

  bool Txn(const ::etcdserverpb::TxnRequest& request,
      ::etcdserverpb::TxnResponse* response);

  bool Compact(const ::etcdserverpb::CompactionRequest& request,
      ::etcdserverpb::CompactionResponse* response);

  bool LeaseGrant(const ::etcdserverpb::LeaseGrantRequest& request,
      ::etcdserverpb::LeaseGrantResponse* response);

  bool LeaseRevoke(const ::etcdserverpb::LeaseRevokeRequest& request,
      ::etcdserverpb::LeaseRevokeResponse* response);

  bool LeaseTimeToLive(const ::etcdserverpb::LeaseTimeToLiveRequest& request,
      ::etcdserverpb::LeaseTimeToLiveResponse* response);

  bool LeaseLeases(const ::etcdserverpb::LeaseLeasesRequest& request,
      ::etcdserverpb::LeaseLeasesResponse* response);

  bool LeaseKeepAlive(const ::etcdserverpb::LeaseKeepAliveRequest& request,
      ::etcdserverpb::LeaseKeepAliveResponse* response);

  bool Watch(const ::etcdserverpb::WatchRequest& request, WatcherPtr watcher);

 private:
  brpc::Channel _channel;
};

}  // namespace brpc

#endif  // SRC_ETCD_CLIENT_ETCD_CLIENT_H_
