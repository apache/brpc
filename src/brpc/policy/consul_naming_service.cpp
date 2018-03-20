// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Yaofu Zhang (zhangyaofu@qiyi.com)

#include <gflags/gflags.h>
#include <string>                                       // std::string
#include <set>                                          // std::set
#include "butil/third_party/rapidjson/document.h"
#include "butil/time/time.h"
#include "bthread/bthread.h"
#include "brpc/log.h"
#include "brpc/channel.h"
#include "brpc/policy/file_naming_service.h"
#include "brpc/policy/consul_naming_service.h"


namespace brpc {
namespace policy {

DEFINE_string(consul_agent_addr, "http://127.0.0.1:8500",
              "The query string of request consul for discovering service.");
DEFINE_string(consul_service_discovery_url,
              "/v1/health/service/",
              "The url of consul for discovering service.");
DEFINE_string(consul_url_parameter, "?stale&passing",
              "The query string of request consul for discovering service.");
DEFINE_int32(consul_connect_timeout_ms, 200,
             "Timeout for creating connections to consul in milliseconds");
DEFINE_int32(consul_blocking_query_wait_secs, 600,
             "Maximum duration for the blocking request in secs.");
DEFINE_bool(consul_enable_degrade_to_file_naming_service, false,
            "Use local backup file when consul cannot connect");
DEFINE_string(consul_file_naming_service_dir, "",
    "When it degraded to file naming service, the file with name of the "
    "service name will be searched in this dir to use.");
DEFINE_int32(consul_retry_interval_ms, 5,
             "Wait so many milliseconds before retry when error happens");

constexpr char kConsulIndex[] = "X-Consul-Index";

int ConsulNamingService::DegradeToFilenamingServiceIfNeed(const char* service_name,
                                                          std::vector<ServerNode>* servers) {
    if (FLAGS_consul_enable_degrade_to_file_naming_service && !_backup_file_loaded) {
        _backup_file_loaded = true;
        const std::string file(FLAGS_consul_file_naming_service_dir + service_name);
        LOG(INFO) << "Load server list from " << file;
        FileNamingService fns;
        return fns.GetServers(file.c_str(), servers);
    }
    return -1;
}

int ConsulNamingService::GetServers(const char* service_name,
                                    std::vector<ServerNode>* servers) {
    if (!_consul_connected) {
        ChannelOptions opt;
        opt.protocol = PROTOCOL_HTTP;
        opt.connect_timeout_ms = FLAGS_consul_connect_timeout_ms;
        opt.timeout_ms = (FLAGS_consul_blocking_query_wait_secs + 10) * butil::Time::kMillisecondsPerSecond;
        if (_channel.Init(FLAGS_consul_agent_addr.c_str(), "rr", &opt) != 0) {
            LOG(ERROR) << "Fail to init channel to consul at " << FLAGS_consul_agent_addr;
            return DegradeToFilenamingServiceIfNeed(service_name, servers);
        }
        _consul_connected = true;
    }

    if (_consul_url.empty()) {
        _consul_url.append(FLAGS_consul_service_discovery_url);
        _consul_url.append(service_name);
        _consul_url.append(FLAGS_consul_url_parameter);
    }

    servers->clear();
    std::string consul_url(_consul_url);
    if (!_consul_index.empty()) {
        consul_url.append("&index=");
        consul_url.append(_consul_index);
        consul_url.append("&wait=");
        consul_url.append(std::to_string(FLAGS_consul_blocking_query_wait_secs));
        consul_url.push_back('s');
    }

    Controller cntl;
    cntl.http_request().uri() = consul_url;
    _channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to init channel to consul at " << FLAGS_consul_agent_addr;
        return DegradeToFilenamingServiceIfNeed(service_name, servers);
    }

    const std::string* index = cntl.http_response().GetHeader(kConsulIndex);
    if (index != nullptr) {
        if (*index == _consul_index) {
            LOG_EVERY_N(ERROR, 100) << "There is no service changed for the list of "
                                    << service_name
                                    << ", consul_index: " << _consul_index;
            return -1;
        }
    } else {
        LOG(ERROR) << "Failed to parse consul index of " << service_name << ".";
        return -1;
    }

    // Sort/unique the inserted vector is faster, but may have a different order
    // of addresses from the file. To make assertions in tests easier, we use
    // set to de-duplicate and keep the order.
    std::set<ServerNode> presence;

    rapidjson::Document services;
    services.Parse(cntl.response_attachment().to_string().c_str());
    if (!services.IsArray()) {
        return -1;
    }

    for (rapidjson::SizeType i = 0; i < services.Size(); ++i) {
        if (!services[i].HasMember("Service")) {
            continue;
        }
        if (!services[i]["Service"].HasMember("Address") ||
            !services[i]["Service"]["Address"].IsString() ||
            !services[i]["Service"].HasMember("Port") ||
            !services[i]["Service"]["Port"].IsUint()) {
            continue;
        }
        butil::EndPoint end_point;
        if (str2endpoint(services[i]["Service"]["Address"].GetString(),
                         services[i]["Service"]["Port"].GetUint(),
                         &end_point) != 0) {
            LOG(ERROR) << "Invalid address=`" << services[i]["Service"]["Address"].GetString() << '\''
                       << " , port= " << services[i]["Service"]["Port"].GetUint();
            continue;
        }
        ServerNode node;
        node.addr = end_point;
        // Tags in consul is an array, here we just use the first one.
        if (services[i]["Service"].HasMember("Tags") &&
            services[i]["Service"]["Tags"].IsArray() &&
            services[i]["Service"]["Tags"].Size() > 0 &&
            services[i]["Service"]["Tags"][0].IsString()) {
            node.tag = services[i]["Service"]["Tags"][0].GetString();
        }

        if (presence.insert(node).second) {
            servers->push_back(node);
        } else {
            RPC_VLOG << "Duplicated server=" << node;
        }
    }

    _consul_index = *index;

    RPC_VLOG << "Got " << servers->size()
             << (servers->size() > 1 ? " servers" : " server")
             << " from " << service_name;
    return 0;
}

int ConsulNamingService::RunNamingService(const char* service_name,
                                          NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    bool ever_reset = false;
    for (;;) {
        servers.clear();
        const int rc = GetServers(service_name, &servers);
        if (rc == 0) {
            ever_reset = true;
            actions->ResetServers(servers);
        } else {
            if (!ever_reset) {
                // ResetServers must be called at first time even if GetServers
                // failed, to wake up callers to `WaitForFirstBatchOfServers'
                ever_reset = true;
                servers.clear();
                actions->ResetServers(servers);
            }
            if (bthread_usleep(std::max(FLAGS_consul_retry_interval_ms, 1) * butil::Time::kMillisecondsPerSecond) < 0) {
                if (errno == ESTOP) {
                    RPC_VLOG << "Quit NamingServiceThread=" << bthread_self();
                    return 0;
                }
                PLOG(FATAL) << "Fail to sleep";
                return -1;
            }
        }
    }
    CHECK(false);
    return -1;
}


void ConsulNamingService::Describe(std::ostream& os,
                                   const DescribeOptions&) const {
    os << "consul";
    return;
}

NamingService* ConsulNamingService::New() const {
    return new ConsulNamingService;
}

void ConsulNamingService::Destroy() {
    delete this;
}

}  // namespace policy
} // namespace brpc
