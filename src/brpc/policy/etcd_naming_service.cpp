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

#include <gflags/gflags.h>
#include <json/json.h>
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "butil/logging.h"                               // CHECK
#include "brpc/cmd_flags.h"
#include "brpc/policy/etcd_naming_service.h"
#include "butil/strings/string_split.h"
#include "butil/third_party/etcdc/base64.h"
#include "butil/third_party/etcdc/cetcd_array.h"
#include "butil/third_party/etcdc/cetcd.h"


namespace brpc {
namespace policy {

static std::string prefix("/providers/");

int EtcdNamingService::GetServers(const char *service_name,
                                   std::vector<ServerNode>* servers) {
    
    std::string ectdServerIP = FLAGS_etcd_server;
    servers->clear();
    
    // Init ectd server addr 
    // such as : http://127.0.0.1:2379
    std::vector<std::string> addresses;
    butil::SplitString(ectdServerIP,';',&addresses);
    if(addresses.size()==0){
        LOG(WARNING) << "Not found cmd flags : etcd_server ";
        return 0;
    }
    std::vector<char *> addrVector;
    std::vector<std::string>::iterator addrIter = addresses.begin();
    for(;addrIter!=addresses.end();++addrIter){
        addrVector.push_back( strdup((*addrIter).c_str()) );
    }
    cetcd_array addrs;
    cetcd_array_init(&addrs, addrVector.size());
    std::vector<char *>::iterator adIter = addrVector.begin();
    for(;adIter!=addrVector.end();++adIter){
        cetcd_array_append(&addrs, *adIter);
    }

    // Init etcd client
    cetcd_client cli;
    cetcd_client_init(&cli, &addrs);

    cetcd_response *resp;
    resp = cetcd_lsdir(&cli, prefix.c_str(), 1, 1);
    if(resp->err) {
        LOG(WARNING) << "Failed to cetcd_lsdir '" << ectdServerIP << prefix << "', " 
            << resp->err->ecode << ", " << resp->err->message << "(" << resp->err->cause << ")";
        return 0;
    }
    std::vector<std::string> result;
    if (resp->node){
        CollectProviders(resp->node,result);
    }
    cetcd_response_release(resp);

    if(result.size()>0){
        std::vector<std::string>::iterator providerIter = result.begin();
        for(;providerIter!=result.end();++providerIter){
            std::string & value = *providerIter;
            std::string decoded;
            if (!Base64::Decode(value, &decoded)) {
                LOG(WARNING) << "Failed to decode base64 string '" << value << "'";
            }else{
                LOG(INFO) << "Found service provider : " << decoded;
                Json::Reader reader;
                Json::Value rootElement;
                reader.parse(decoded, rootElement, false);
                Json::Value& nodeElement = rootElement["node"];
                std::string ipString = nodeElement["ip"].asString();
                butil::ip_t ip;
                if (butil::str2ip(ipString.c_str(), &ip) != 0) {
                    LOG(WARNING) << "Invalid ip=" << ipString;
                    continue;
                }
                int port = nodeElement["port"].asInt();
                std::string tags("");
                Json::Value& nodeTagsElement = rootElement["tags"];
                Json::FastWriter writer;
                tags = writer.write(nodeTagsElement);
                Json::Value& nodeServicesElement = rootElement["services"];
                if (nodeServicesElement.isArray() && nodeServicesElement.size() > 0) {
                    for (size_t i = 0; i < nodeServicesElement.size(); i++) {
                        Json::Value& nodeServiceElement = nodeServicesElement[(Json::ArrayIndex)i];
                        std::string serviceName = nodeServiceElement.asString();
                        servers->push_back(ServerNode(ip, port, tags));
                    }
                }
            }
        }
    }else{
        LOG(WARNING) << "Not found service providers";
    }

    // Destroy etcd client
    cetcd_array_destroy(&addrs);
    cetcd_client_destroy(&cli);
    std::vector<char *>::iterator adIter2 = addrVector.begin();
    for(;adIter2!=addrVector.end();++adIter2){
        free(*adIter2);
    }

    return 0;
}

void EtcdNamingService::CollectProviders(cetcd_response_node *node, std::vector<std::string>& result){
    int i, count;
    cetcd_response_node *n;
    if (node) {
        if(!node->dir){
            std::string keyvalue(node->key);
            std::size_t found = keyvalue.find(prefix);
            if (found != std::string::npos){
                keyvalue = keyvalue.substr(found+prefix.length());
                result.push_back(keyvalue);
            }
        }
        if (node->nodes) {
            count = cetcd_array_size(node->nodes);
            for (i = 0; i < count; ++i) {
                n = static_cast<cetcd_response_node *>(cetcd_array_get(node->nodes, i));
                CollectProviders(n,result);
            }
        }
    }
}

void EtcdNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "etcdns";
    return;
}

NamingService* EtcdNamingService::New() const {
    return new EtcdNamingService;
}

void EtcdNamingService::Destroy() {
    delete this;
}

}  // namespace policy
} // namespace brpc

