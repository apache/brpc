// Copyright (c) 2018 BiliBili, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#include <gflags/gflags.h>
#include "butil/third_party/rapidjson/document.h"
#include "butil/string_printf.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/discovery_naming_service.h"

namespace brpc {
namespace policy {

DEFINE_string(discovery_api_addr, "http://api.bilibili.co/discovery/nodes",
              "The address of discovery api");
DEFINE_int32(discovery_timeout_ms, 3000, "Timeout for discovery requests");
DEFINE_string(discovery_env, "prod", "Environment of services");
DEFINE_string(discovery_status, "1", "Status of services. 1 for ready, 2 for not ready, 3 for all");

int DiscoveryNamingService::parse_nodes_result(
        const butil::IOBuf& buf, std::string* server_addr) {
    BUTIL_RAPIDJSON_NAMESPACE::Document nodes;
    const std::string response = buf.to_string();
    nodes.Parse(response.c_str());
    if (!nodes.HasMember("data")) {
        LOG(ERROR) << "No data field in discovery nodes response";
        return -1;
    }
    const BUTIL_RAPIDJSON_NAMESPACE::Value& data = nodes["data"];
    if (!data.IsArray()) {
        LOG(ERROR) << "data field is not an array";
        return -1;
    }
    for (BUTIL_RAPIDJSON_NAMESPACE::SizeType i = 0; i < data.Size(); ++i) {
        const BUTIL_RAPIDJSON_NAMESPACE::Value& addr_item = data[i];
        if (!addr_item.HasMember("addr") ||
                !addr_item["addr"].IsString() ||
                !addr_item.HasMember("status") ||
                !addr_item["status"].IsUint() ||
                addr_item["status"].GetUint() != 0) {
            continue;
        }
        server_addr->assign(addr_item["addr"].GetString(),
                            addr_item["addr"].GetStringLength());
        // Currently, we just use the first successful result
        break;
    }
    return 0;
}

int DiscoveryNamingService::parse_fetchs_result(
        const butil::IOBuf& buf,
        const char* service_name,
        std::vector<ServerNode>* servers) {
    BUTIL_RAPIDJSON_NAMESPACE::Document d;
    const std::string response = buf.to_string();
    d.Parse(response.c_str());
    if (!d.HasMember("data")) {
        LOG(ERROR) << "No data field in discovery fetchs response";
        return -1;
    }
    const BUTIL_RAPIDJSON_NAMESPACE::Value& data = d["data"];
    if (!data.HasMember(service_name)) {
        LOG(ERROR) << "No " << service_name << " field in discovery response";
        return -1;
    }
    const BUTIL_RAPIDJSON_NAMESPACE::Value& services = data[service_name];
    if (!services.HasMember("instances")) {
        LOG(ERROR) << "Fail to find instances";
        return -1;
    }
    const BUTIL_RAPIDJSON_NAMESPACE::Value& instances = services["instances"];
    if (!instances.IsArray()) {
        LOG(ERROR) << "Fail to parse instances as an array";
        return -1;
    }

    for (BUTIL_RAPIDJSON_NAMESPACE::SizeType i = 0; i < instances.Size(); ++i) {
        if (!instances[i].HasMember("addrs") || !instances[i]["addrs"].IsArray()) {
            LOG(ERROR) << "Fail to find addrs or addrs is not an array";
            return -1;
        }
        const BUTIL_RAPIDJSON_NAMESPACE::Value& addrs = instances[i]["addrs"];
        for (BUTIL_RAPIDJSON_NAMESPACE::SizeType j = 0; j < addrs.Size(); ++j) {
            if (!addrs[j].IsString()) {
                continue;
            }
            // The result returned by discovery include protocol prefix, such as
            // http://172.22.35.68:6686, which should be removed.
            butil::StringPiece addr(addrs[j].GetString(), addrs[j].GetStringLength());
            butil::StringPiece::size_type pos = addr.find("://");
            if (pos != butil::StringPiece::npos) {
                addr.remove_prefix(pos + 3);
            }
            ServerNode node;
            if (str2endpoint(addr.data(), &node.addr) != 0) {
                LOG(ERROR) << "Invalid address=`" << addr << '\'';
                continue;
            }
            servers->push_back(node);
        }
    }
    return 0;
}

int DiscoveryNamingService::GetServers(const char* service_name,
                                       std::vector<ServerNode>* servers) {
    if (!_is_initialized) {
        Channel api_channel;
        ChannelOptions channel_options;
        channel_options.protocol = PROTOCOL_HTTP;
        channel_options.timeout_ms = FLAGS_discovery_timeout_ms;
        channel_options.connect_timeout_ms = FLAGS_discovery_timeout_ms / 3;
        if (api_channel.Init(FLAGS_discovery_api_addr.c_str(), "", &channel_options) != 0) {
            LOG(ERROR) << "Fail to init channel to " << FLAGS_discovery_api_addr;
            return -1;
        }
        Controller cntl;
        cntl.http_request().uri() = FLAGS_discovery_api_addr;
        api_channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to access " << cntl.http_request().uri()
                       << ": " << cntl.ErrorText();
            return -1;
        }
        std::string discovery_addr;
        if (parse_nodes_result(cntl.response_attachment(), &discovery_addr) != 0) {
            return -1;
        }

        if (_channel.Init(discovery_addr.c_str(), "", &channel_options) != 0) {
            LOG(ERROR) << "Fail to init channel to " << discovery_addr;
            return -1;
        }
        _is_initialized = true;
    }
    
    servers->clear();
    Controller cntl;
    // TODO(zhujiashun): pass zone from service_name
    cntl.http_request().uri() = butil::string_printf(
            "/discovery/fetchs?appid=%s&env=%s&status=%s", service_name,
            FLAGS_discovery_env.c_str(), FLAGS_discovery_status.c_str());
    _channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to make /discovery/fetchs request: " << cntl.ErrorText();
        return -1;
    }
    return parse_fetchs_result(cntl.response_attachment(), service_name, servers);
}

void DiscoveryNamingService::Describe(std::ostream& os,
                                      const DescribeOptions&) const {
    os << "discovery";
    return;
}

NamingService* DiscoveryNamingService::New() const {
    return new DiscoveryNamingService;
}

void DiscoveryNamingService::Destroy() {
    delete this;
}

} // namespace policy
} // namespace brpc
