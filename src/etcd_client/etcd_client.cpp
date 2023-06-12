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


#include "etcd_client/etcd_client.h"
#include <memory>
#include "butil/string_splitter.h"
#include "json2pb/pb_to_json.h"
#include "json2pb/json_to_pb.h"

namespace brpc {

EtcdClient::EtcdClient() {
}

EtcdClient::~EtcdClient() {
}

bool EtcdClient::Init(const std::string& url, const std::string& lb) {
  brpc::ChannelOptions options;
  options.protocol = "http";
  if (_channel.Init(url.c_str(), lb.c_str(), &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel with " << url;
    return false;
  }
  return true;
}

bool EtcdClient::Init(butil::EndPoint server_addr_and_port) {
  brpc::ChannelOptions options;
  options.protocol = "http";
  if (_channel.Init(server_addr_and_port, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel with " << server_addr_and_port;
    return false;
  }
  return true;
}

bool EtcdClient::Init(const char* server_addr_and_port) {
  brpc::ChannelOptions options;
  options.protocol = "http";
  if (_channel.Init(server_addr_and_port, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel with " << server_addr_and_port;
    return false;
  }
  return true;
}

bool EtcdClient::Init(const char* server_addr, int port) {
  brpc::ChannelOptions options;
  options.protocol = "http";
  if (_channel.Init(server_addr, port, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel with " << server_addr
        << ":" << port;
    return false;
  }
  return true;
}

enum EtcdOps {
  RANGE = 0,
  PUT,
  DELETE_RANGE,
  TXN,
  COMPACT,
  LEASE_GRANT,
  LEASE_REVOKE,
  LEASE_KEEPALIVE,
  LEASE_TTL,
  LEASE_LEASES,
};

template<typename Request, typename Response>
static bool EtcdOp(brpc::Channel* channel, EtcdOps op, const Request& request, Response* response) {
  // invoker will make sure the op is valid
  std::string path = "";
  switch (op) {
    case RANGE           : path = "/v3/kv/range";           break;
    case PUT             : path = "/v3/kv/put";             break;
    case DELETE_RANGE    : path = "/v3/kv/deleterange";     break;
    case TXN             : path = "/v3/kv/txn";             break;
    case COMPACT         : path = "/v3/kv/compaction";      break;
    case LEASE_GRANT     : path = "/v3/lease/grant";        break;
    case LEASE_REVOKE    : path = "/v3/lease/revoke";       break;
    case LEASE_KEEPALIVE : path = "/v3/lease/keepalive";    break;
    case LEASE_TTL       : path = "/v3/lease/timetolive";   break;
    case LEASE_LEASES    : path = "/v3/lease/leases";       break;
    default:
      LOG(ERROR) << "Invalid operation " << op << " with " << request.ShortDebugString();
      return false;
  }
  Controller cntl;
  brpc::URI& uri = cntl.http_request().uri();
  uri.set_path(path);
  cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
  std::string output;
  struct json2pb::Pb2JsonOptions pb2json_option;
  pb2json_option.bytes_to_base64 = true;
  if (!json2pb::ProtoMessageToJson(request, &output, pb2json_option)) {
    LOG(ERROR) << "convert PutRequest " << request.ShortDebugString() << " to json fail.";
    return false;
  }
  cntl.request_attachment().append(output);
  channel->CallMethod(NULL, &cntl, NULL, NULL, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "request " << request.ShortDebugString() << " to etcd server fail."
        << "error message : " << cntl.ErrorText();
    return false;
  }
  struct json2pb::Json2PbOptions json2pb_option;
  json2pb_option.base64_to_bytes = true;
  if (!json2pb::JsonToProtoMessage(cntl.response_attachment().to_string(), response, json2pb_option)) {
    LOG(ERROR) << "Json to proto fail for " << cntl.response_attachment().to_string();
    return false;
  }
  return true;
}

bool EtcdClient::Put(const ::etcdserverpb::PutRequest& request,
    ::etcdserverpb::PutResponse* response) {
  return EtcdOp(&_channel, EtcdOps::PUT, request, response);
}

bool EtcdClient::Range(const ::etcdserverpb::RangeRequest& request,
    ::etcdserverpb::RangeResponse* response) {
  return EtcdOp(&_channel, EtcdOps::RANGE, request, response);
}

bool EtcdClient::DeleteRange(const ::etcdserverpb::DeleteRangeRequest& request,
    ::etcdserverpb::DeleteRangeResponse* response) {
  return EtcdOp(&_channel, DELETE_RANGE, request, response);
}

bool EtcdClient::Txn(const ::etcdserverpb::TxnRequest& request,
    ::etcdserverpb::TxnResponse* response) {
  return EtcdOp(&_channel, EtcdOps::TXN, request, response);
}

bool EtcdClient::Compact(const ::etcdserverpb::CompactionRequest& request,
    ::etcdserverpb::CompactionResponse* response) {
  return EtcdOp(&_channel, EtcdOps::COMPACT, request, response);
}

bool EtcdClient::LeaseGrant(const ::etcdserverpb::LeaseGrantRequest& request,
    ::etcdserverpb::LeaseGrantResponse* response) {
  return EtcdOp(&_channel, EtcdOps::LEASE_GRANT, request, response);
}

bool EtcdClient::LeaseRevoke(const ::etcdserverpb::LeaseRevokeRequest& request,
    ::etcdserverpb::LeaseRevokeResponse* response) {
  return EtcdOp(&_channel, EtcdOps::LEASE_REVOKE, request, response);
}

bool EtcdClient::LeaseTimeToLive(const ::etcdserverpb::LeaseTimeToLiveRequest& request,
    ::etcdserverpb::LeaseTimeToLiveResponse* response) {
  return EtcdOp(&_channel, EtcdOps::LEASE_TTL, request, response);
}

bool EtcdClient::LeaseLeases(const ::etcdserverpb::LeaseLeasesRequest& request,
    ::etcdserverpb::LeaseLeasesResponse* response) {
  return EtcdOp(&_channel, EtcdOps::LEASE_LEASES, request, response);
}

bool EtcdClient::LeaseKeepAlive(const ::etcdserverpb::LeaseKeepAliveRequest& request,
    ::etcdserverpb::LeaseKeepAliveResponse* response) {
  return EtcdOp(&_channel, EtcdOps::LEASE_KEEPALIVE, request, response);
}

class ReadBody : public brpc::ProgressiveReader, public brpc::SharedObject {
 public:
  ReadBody() : _destroyed(false) {
    butil::intrusive_ptr<ReadBody>(this).detach();
  }

  butil::Status OnReadOnePart(const void* data, size_t length) {
    if (length > 0) {
      butil::IOBuf os;
      os.append(data, length);
      ::etcdserverpb::WatchResponseEx response;
      struct json2pb::Json2PbOptions option;
      option.base64_to_bytes = true;
      option.allow_remaining_bytes_after_parsing = true;
      std::string err;
      if (!json2pb::JsonToProtoMessage(os.to_string(), &response, option, &err)) {
        _watcher->OnParseError();
        LOG(WARNING) << "watch parse " << os.to_string() << " fail. "
            << "error msg " << err;
      } else {
        _watcher->OnEventResponse(response.result());
      }
    }
    return butil::Status::OK();
  }

  void OnEndOfMessage(const butil::Status& st) {
    butil::intrusive_ptr<ReadBody>(this, false);
    _destroyed = true;
    _destroying_st = st;
    return;
  }

  bool destroyed() const {
    return _destroyed;
  }

  const butil::Status& destroying_status() const {
    return _destroying_st;
  }

  void set_watcher(std::shared_ptr<Watcher> watcher) {
    _watcher = watcher;
  }

 private:
  bool _destroyed;
  butil::Status _destroying_st;
  std::shared_ptr<Watcher> _watcher;
};

bool EtcdClient::Watch(const ::etcdserverpb::WatchRequest& request, WatcherPtr watcher) {
  if (!watcher.get()) {
    LOG(ERROR) << "watcher is nullptr";
    return false;
  }
  brpc::Controller cntl;
  cntl.http_request().uri().set_path("/v3/watch");
  cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
  std::string output;
  struct json2pb::Pb2JsonOptions pb2json_option;
  pb2json_option.bytes_to_base64 = true;
  if (!json2pb::ProtoMessageToJson(request, &output, pb2json_option)) {
    LOG(ERROR) << "convert WatchRequest " << request.ShortDebugString() << " to json fail.";
    return false;
  }
  cntl.request_attachment().append(output);
  cntl.response_will_be_read_progressively();
  _channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "watch request " << request.ShortDebugString() << " to etcd server fail."
        << "error message : " << cntl.ErrorText();
    return false;
  }
  butil::intrusive_ptr<ReadBody> reader;
  reader.reset(new ReadBody);
  reader->set_watcher(watcher);
  cntl.ReadProgressiveAttachmentBy(reader.get());
  return true;
}

}  // namespace brpc

