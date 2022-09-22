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

#include "nacos_naming_service.h"

#include <gflags/gflags.h>

#include <set>

#include "brpc/http_status_code.h"
#include "brpc/log.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/third_party/rapidjson/document.h"

namespace brpc {
namespace policy {

DEFINE_string(nacos_address, "",
              "The query string of request nacos for discovering service.");
DEFINE_string(nacos_service_discovery_path, "/nacos/v1/ns/instance/list",
              "The url path for discovering service.");
DEFINE_string(nacos_service_auth_path, "/nacos/v1/auth/login",
              "The url path for authentiction.");
DEFINE_int32(nacos_connect_timeout_ms, 200,
             "Timeout for creating connections to nacos in milliseconds");
DEFINE_string(nacos_username, "", "nacos username");
DEFINE_string(nacos_password, "", "nacos password");
DEFINE_string(nacos_load_balancer, "rr", "nacos load balancer name");

int NacosNamingService::Connect() {
    ChannelOptions opt;
    opt.protocol = PROTOCOL_HTTP;
    opt.connect_timeout_ms = FLAGS_nacos_connect_timeout_ms;
    const int ret = _channel.Init(FLAGS_nacos_address.c_str(),
                                  FLAGS_nacos_load_balancer.c_str(), &opt);
    if (ret != 0) {
        LOG(ERROR) << "Fail to init channel to nacos at "
                   << FLAGS_nacos_address;
    }
    return ret;
}

int NacosNamingService::RefreshAccessToken(const char *service_name) {
    Controller cntl;
    cntl.http_request().uri() = FLAGS_nacos_service_auth_path;
    cntl.http_request().set_method(brpc::HttpMethod::HTTP_METHOD_POST);
    cntl.http_request().set_content_type("application/x-www-form-urlencoded");

    auto &buf = cntl.request_attachment();
    buf.append("username=");
    buf.append(FLAGS_nacos_username);
    buf.append("&password=");
    buf.append(FLAGS_nacos_password);

    _channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access " << FLAGS_nacos_service_auth_path << ": "
                   << cntl.ErrorText();
        return -1;
    }

    BUTIL_RAPIDJSON_NAMESPACE::Document doc;
    if (doc.Parse(cntl.response_attachment().to_string().c_str())
            .HasParseError()) {
        LOG(ERROR) << "Failed to parse nacos auth response";
        return -1;
    }
    if (!doc.IsObject()) {
        LOG(ERROR) << "The nacos's auth response for " << service_name
                   << " is not a json object";
        return -1;
    }

    auto iter = doc.FindMember("accessToken");
    if (iter != doc.MemberEnd() && iter->value.IsString()) {
        _access_token = iter->value.GetString();
    } else {
        LOG(ERROR) << "The nacos's auth response for " << service_name
                   << " has no accessToken field";
        return -1;
    }

    auto iter_ttl = doc.FindMember("tokenTtl");
    if (iter_ttl != doc.MemberEnd() && iter_ttl->value.IsInt()) {
        _token_expire_time = time(NULL) + iter_ttl->value.GetInt() - 10;
    } else {
        _token_expire_time = 0;
    }

    return 0;
}

int NacosNamingService::GetServerNodes(const char *service_name,
                                       bool token_changed,
                                       std::vector<ServerNode> *nodes) {
    if (_nacos_url.empty() || token_changed) {
        _nacos_url = FLAGS_nacos_service_discovery_path;
        _nacos_url += "?";
        if (!_access_token.empty()) {
            _nacos_url += "accessToken=" + _access_token;
            _nacos_url += "&";
        }
        _nacos_url += service_name;
    }

    Controller cntl;
    cntl.http_request().uri() = _nacos_url;
    _channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access " << _nacos_url << ": "
                   << cntl.ErrorText();
        return -1;
    }
    if (cntl.http_response().status_code() != HTTP_STATUS_OK) {
        LOG(ERROR) << "Failed to request nacos, http status code: "
                   << cntl.http_response().status_code();
        return -1;
    }

    BUTIL_RAPIDJSON_NAMESPACE::Document doc;
    if (doc.Parse(cntl.response_attachment().to_string().c_str())
            .HasParseError()) {
        LOG(ERROR) << "Failed to parse nacos response";
        return -1;
    }
    if (!doc.IsObject()) {
        LOG(ERROR) << "The nacos's response for " << service_name
                   << " is not a json object";
        return -1;
    }

    auto it_hosts = doc.FindMember("hosts");
    if (it_hosts == doc.MemberEnd()) {
        LOG(ERROR) << "The nacos's response for " << service_name
                   << " has no hosts member";
        return -1;
    }
    auto &hosts = it_hosts->value;
    if (!hosts.IsArray()) {
        LOG(ERROR) << "hosts member in nacos response is not an array";
        return -1;
    }

    std::set<ServerNode> presence;
    for (auto it = hosts.Begin(); it != hosts.End(); ++it) {
        auto &host = *it;
        if (!host.IsObject()) {
            LOG(ERROR) << "host member in nacos response is not an object";
            continue;
        }

        auto it_ip = host.FindMember("ip");
        if (it_ip == host.MemberEnd() || !it_ip->value.IsString()) {
            LOG(ERROR) << "host in nacos response has not ip";
            continue;
        }
        auto &ip = it_ip->value;

        auto it_port = host.FindMember("port");
        if (it_port == host.MemberEnd() || !it_port->value.IsInt()) {
            LOG(ERROR) << "host in nacos response has not port";
            continue;
        }
        auto &port = it_port->value;

        auto it_enabled = host.FindMember("enabled");
        if (it_enabled == host.MemberEnd() || !(it_enabled->value.IsBool()) ||
            !(it_enabled->value.GetBool())) {
            LOG(INFO) << "nacos " << ip.GetString() << ":" << port.GetInt()
                      << " is not enable";
            continue;
        }

        auto it_healthy = host.FindMember("healthy");
        if (it_healthy == host.MemberEnd() || !(it_healthy->value.IsBool()) ||
            !(it_healthy->value.GetBool())) {
            LOG(INFO) << "nacos " << ip.GetString() << ":" << port.GetInt()
                      << " is not healthy";
            continue;
        }

        butil::EndPoint end_point;
        if (str2endpoint(ip.GetString(), port.GetUint(), &end_point) != 0) {
            LOG(ERROR) << "ncos service with illegal address or port: "
                       << ip.GetString() << ":" << port.GetUint();
            continue;
        }

        ServerNode node(end_point);
        auto it_weight = host.FindMember("weight");
        if (it_weight != host.MemberEnd() && it_weight->value.IsNumber()) {
            node.tag =
                std::to_string(static_cast<long>(it_weight->value.GetDouble()));
        }

        presence.insert(node);
    }

    nodes->reserve(presence.size());
    nodes->assign(presence.begin(), presence.end());

    if (nodes->empty() && hosts.Size() != 0) {
        LOG(ERROR) << "All service about " << service_name
                   << " from nacos is invalid, refuse to update servers";
        return -1;
    }

    RPC_VLOG << "Got " << nodes->size()
             << (nodes->size() > 1 ? " servers" : " server") << " from "
             << service_name;

    auto it_cache = doc.FindMember("cacheMillis");
    if (it_cache != doc.MemberEnd() && it_cache->value.IsInt64()) {
        _cache_ms = it_cache->value.GetInt64();
    }

    return 0;
}

NacosNamingService::NacosNamingService()
    : _nacos_connected(false), _cache_ms(-1), _token_expire_time(0) {}

int NacosNamingService::GetNamingServiceAccessIntervalMs() const {
    if (0 < _cache_ms) {
        return _cache_ms;
    }
    return PeriodicNamingService::GetNamingServiceAccessIntervalMs();
}

int NacosNamingService::GetServers(const char *service_name,
                                   std::vector<ServerNode> *servers) {
    if (!_nacos_connected) {
        const int ret = Connect();
        if (0 == ret) {
            _nacos_connected = true;
        } else {
            return ret;
        }
    }

    const bool authentiction_enabled =
        !FLAGS_nacos_username.empty() && !FLAGS_nacos_password.empty();
    const bool has_invalid_access_token =
        _access_token.empty() ||
        (0 < _token_expire_time && _token_expire_time <= time(NULL));
    bool token_changed = false;

    if (authentiction_enabled && has_invalid_access_token) {
        const int ret = RefreshAccessToken(service_name);
        if (ret == 0) {
            token_changed = true;
        } else {
            return ret;
        }
    }

    servers->clear();
    return GetServerNodes(service_name, token_changed, servers);
}

void NacosNamingService::Describe(std::ostream &os,
                                  const DescribeOptions &) const {
    os << "nacos";
    return;
}

NamingService *NacosNamingService::New() const {
    return new NacosNamingService;
}

void NacosNamingService::Destroy() { delete this; }

}  // namespace policy
}  // namespace brpc
