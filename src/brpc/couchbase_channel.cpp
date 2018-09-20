// Copyright (c) 2018 Iqiyi, Inc.
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

// Authors: Daojin Cai (caidaojin@qiyi.com)

#include "brpc/couchbase_channel.h"
#include "brpc/couchbase.h"
#include "brpc/couchbase_helper.h"
#include "brpc/couchbase_retry_policy.h"
#include "brpc/load_balancer.h"
#include "brpc/policy/couchbase_authenticator.h"
#include "brpc/uri.h" 
#include "butil/base64.h"
#include "butil/string_splitter.h"
#include "butil/strings/string_number_conversions.h" 
#include "butil/string_printf.h"


namespace brpc {

DEFINE_string(couchbase_authorization_http_basic, "", 
              "Http basic authorization of couchbase, something like 'user:password'");

namespace {
	
const std::string kDefaultBucketStreamingUrlPrefix("/pools/default/bucketsStreaming/");
const std::string kDefaultBucketUrlPrefix("/pools/default/buckets/");

}

int CouchbaseChannel::Init(const char* listen_uri, const ChannelOptions* options) {
    std::string servers;
    std::string streaming_url;
    std::string init_url;
    if (!ParseListenUri(listen_uri, &servers, &streaming_url, &init_url)) {
        LOG(ERROR) << "Failed to parse listen url \'" << listen_uri << "\'.";
    }
    return InitMemcacheChannel(servers.c_str(), streaming_url, 
                               init_url, options);
}

int CouchbaseChannel::Init(const char* servers, const char* bucket_name, 
                           const ChannelOptions* options) {
    std::string streaming_url = kDefaultBucketStreamingUrlPrefix + bucket_name;
    std::string init_url = kDefaultBucketUrlPrefix + bucket_name;
    return InitMemcacheChannel(servers, streaming_url, init_url, options);
} 

int CouchbaseChannel::InitMemcacheChannel(
    const char* servers, const std::string& streaming_url,
    const std::string& init_url, const ChannelOptions* options) {
    ChannelOptions inner_options;
    if (options != nullptr) {
        if (options->protocol != PROTOCOL_UNKNOWN && 
            options->protocol != PROTOCOL_MEMCACHE) {
            LOG(FATAL) << "Failed to init channel due to invalid protocol " 
                       <<  options->protocol.name() << '.';
            return -1;
        }
        inner_options = *options;
    }
    // Retry times is 1 at least.
    inner_options.protocol = PROTOCOL_MEMCACHE;
    if (inner_options.max_retry <= 0) {
        inner_options.max_retry = 1;
    }
    inner_options.retry_policy = CouchbaseRetryPolicy::Instance();
    std::string auth;
    // If not define auth string directly, get auth string from 'options'.
    // This auth is used by CouchbaseNamingService to retrieves vbucket mapping.
    if (!FLAGS_couchbase_authorization_http_basic.empty()) {
        butil::Base64Encode(FLAGS_couchbase_authorization_http_basic, &auth);
        auth = "Basic " + auth;
    } else { 
        std::string auth_str;
        const policy::CouchbaseAuthenticator* authenticator = reinterpret_cast<
            const policy::CouchbaseAuthenticator*>(options->auth);
        if (authenticator != nullptr) {
            auth_str = authenticator->bucket_name() + ':' 
                       + authenticator->bucket_password();
        }
        if (!auth_str.empty()) {
            butil::Base64Encode(auth_str, &auth);
            auth = "Basic " + auth;
        }
    }
		
    // TODO: encrypt auth to avoid expose in log.
    _ns = new (std::nothrow) policy::CouchbaseNamingService(servers, init_url, 
                                                            streaming_url, auth);
    if (_ns == nullptr) {
        LOG(FATAL) << "Failed to init CouchbaseNamingService.";
        return -1;
    }
    return _channel.Init(_ns, "cb_lb", &inner_options);
}

void CouchbaseChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                  google::protobuf::RpcController* controller,
                                  const google::protobuf::Message* request,
                                  google::protobuf::Message* response,
                                  google::protobuf::Closure* done) {
    const CouchbaseRequest* req = reinterpret_cast<const CouchbaseRequest*>(request);
    Controller* cntl = static_cast<Controller*>(controller);
    CouchbaseRequest request_with_vb;
    do {
        std::string key;
        policy::MemcacheBinaryCommand command;
        if (req->ParseRequest(&key, &command) != 0) {
            cntl->SetFailed("Failed to parse key and command from request");
            break;
        }
        if (req->read_replicas()) {
            cntl->set_couchbase_key_read_replicas(key);	    
        }
        size_t vb_num = 0;
        if (!CouchbaseHelper::GetVBucketMapInfo(
            _channel._lb, nullptr, &vb_num, nullptr) || vb_num == 0) {
            cntl->SetFailed("No vbuckets found");
            break;
        }
        uint32_t vb_id = CouchbaseHelper::GetVBucketId(key, vb_num);
        cntl->set_request_code(CouchbaseHelper::InitRequestCode(vb_id));
        if (!req->BuildVBucketId(vb_id, &request_with_vb)) {
            cntl->SetFailed("Failed to set vbucket id");
            break;
        }
    } while(false);
    _channel.CallMethod(method, controller, &request_with_vb, response, done);
    return;
}

int CouchbaseChannel::CheckHealth() {
    size_t server_num = 0;
    if (!CouchbaseHelper::GetVBucketMapInfo(
        _channel._lb, &server_num, nullptr, nullptr) || server_num == 0) {
        return -1;
    }
    for (size_t i = 0; i != server_num; ++i) {
        SocketUniquePtr tmp_sock;
        // CouchbaseLoadBalancer check real server health status
        // according to request code 0xffffffff. Only all servers are health, 
        // the couchbase channel is considered health.
        uint64_t code = CouchbaseHelper::InitRequestCode(0xffffffff);
        code = CouchbaseHelper::AddReasonToRequestCode(code, i);
        LoadBalancer::SelectIn sel_in = { 0, false, true, code, NULL };
        LoadBalancer::SelectOut sel_out(&tmp_sock);
        if (_channel._lb->SelectServer(sel_in, &sel_out) != 0) {
            return -1;
        }
    }
    return 0;   
}

bool CouchbaseChannel::ParseListenUri(
    const char* listen_uri, std::string* server, 
    std::string* streaming_uri, std::string* init_uri) {
    URI uri;
    if (uri.SetHttpURL(listen_uri) != 0) {
        return false;
    }
    server->clear();
    butil::string_appendf(server, "%s:%d", uri.host().c_str(), uri.port());
    const std::string& path = uri.path();
    size_t pos = path.find("/bucketsStreaming/");
    if (pos != std::string::npos) {
        streaming_uri->clear();
        *streaming_uri = path;
        *init_uri = path;
        // remove "Streaming". "/bucketsStreaming/" -> "/buckets/"
        init_uri->erase(pos + 8, 9);
        return true;
    } 
    pos = path.find("/buckets/");
    if (pos != std::string::npos) {
        init_uri->clear();
        *streaming_uri = path;
        *init_uri = path;
        // insert "Streaming". "/buckets/" -> "/bucketsStreaming/"
        streaming_uri->insert(pos + 8, "Streaming");
        return true;
    } 
    return false;
}

} // namespace brpc
