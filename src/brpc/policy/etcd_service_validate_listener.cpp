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
#include <string>
#include <vector>

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "butil/logging.h"                               // CHECK
#include "brpc/cmd_flags.h"
#include "brpc/policy/etcd_service_validate_listener.h"
#include "butil/strings/string_split.h"
#include "butil/base64.h"
#include "butil/third_party/rapidjson/document.h"
#include "butil/third_party/rapidjson/writer.h"
#include "butil/third_party/rapidjson/stringbuffer.h"
#include "butil/third_party/etcdc/cetcd_array.h"
#include "butil/third_party/etcdc/cetcd.h"


namespace brpc {
namespace policy {

static std::string prefix("/providers/");

void EtcdServiceValidateListener::onValidServices(
    const std::vector<std::string> &validServiceFullNames)
{
    if(validServiceFullNames.empty()){
        LOG(WARNING) << "Not found valid services";
        return;
    }
    std::string nodeIP = FLAGS_node_ip;
    int nodePort       = FLAGS_port;
    std::string ectdServerIP = FLAGS_etcd_server;
    std::string nodeTags    = FLAGS_node_tags;
    int checkIntervalSecs    = FLAGS_check_interval;
    int ttl_seconds = checkIntervalSecs + 2; 

    //ROOT
    rapidjson::Document rootdoc;
    rootdoc.SetObject();
    rapidjson::Document::AllocatorType& allocator = rootdoc.GetAllocator();
    
    //node
    rapidjson::Value nodeElement(rapidjson::kObjectType);
    rapidjson::Value ipVal(nodeIP.c_str(), allocator);
    nodeElement.AddMember("ip", ipVal, allocator);
    nodeElement.AddMember("port", nodePort, allocator);
    rootdoc.AddMember("node", nodeElement, allocator);

    //tags
    rapidjson::Value tagsElement(rapidjson::kObjectType);
    std::vector<std::string> tokens;
    butil::SplitString(nodeTags,';',&tokens);
    if(!tokens.empty()){
        std::vector<std::string>::iterator tagIter = tokens.begin();
        for(;tagIter!=tokens.end();++tagIter){
            std::string& tagText = *tagIter;
            std::vector<std::string> tokens2;
            butil::SplitString(tagText,'=',&tokens2);
            if(tokens2.size() == 2){
                rapidjson::Value key(tokens2[0].c_str(), allocator);
                rapidjson::Value value(tokens2[1].c_str(), allocator);
                tagsElement.AddMember(key, value, allocator);
            }
        }
    }
    rootdoc.AddMember("tags", tagsElement, allocator);
        
    //services
    rapidjson::Value nodeServicesElement(rapidjson::kArrayType);
    std::vector<std::string>::const_iterator iter = validServiceFullNames.cbegin();
    for(;iter!=validServiceFullNames.cend();++iter){
        const std::string& serviceFullName = *iter;
        rapidjson::Value val(serviceFullName.c_str(), allocator);
        nodeServicesElement.PushBack(val, allocator);
    }
    rootdoc.AddMember("services", nodeServicesElement, allocator);

    // Write service register information to Etcd
    //Generate string
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    rootdoc.Accept(writer);
    std::string content = buffer.GetString();

    std::string encoded;
    butil::Base64Encode(content, &encoded);
    if (encoded.empty()) {
        LOG(ERROR) << "Failed to encode input string '" << content << "'";
    }else{
        encoded = prefix + encoded;

        std::vector<std::string> addresses;
        butil::SplitString(ectdServerIP,';',&addresses);
        if(addresses.size()==0){
            LOG(WARNING) << "Not found cmd flags : etcd_server ";
            return;
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

        cetcd_client cli;
        cetcd_client_init(&cli, &addrs);

        cetcd_response *resp;
        resp = cetcd_set(&cli, encoded.c_str(), "", ttl_seconds);
        if(resp->err) {
            LOG(WARNING) << "Failed to cetcd_set '" << ectdServerIP << prefix << "', " 
                        << resp->err->ecode << ", " << resp->err->message << "(" << resp->err->cause << ")";
        }else{
            LOG(INFO) << "Success to write '" << content << "'";
        }
        cetcd_response_release(resp);

        cetcd_array_destroy(&addrs);
        cetcd_client_destroy(&cli);
        std::vector<char *>::iterator adIter2 = addrVector.begin();
        for(;adIter2!=addrVector.end();++adIter2){
            free(*adIter2);
        }
    }        
}

}  // namespace policy
} // namespace brpc