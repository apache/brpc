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
#include "brpc/policy/couchbase_naming_service.h"
#include "butil/base64.h"
#include "butil/string_splitter.h"
#include "butil/strings/string_number_conversions.h" 

namespace brpc {

DEFINE_string(couchbase_authorization_http_basic, "", 
              "Http basic authorization of couchbase, something like 'user:password'");

namespace {
	
const std::string kDefaultBucketStreamingUrlPrefix("/pools/default/bucketsStreaming/");
const std::string kDefaultBucketUrlPrefix("/pools/default/buckets/");
const std::string kCouchbaseNamingServiceProtocol("couchbase_channel://");

}

int CouchbaseChannel::Init(const char* listen_url, const ChannelOptions* options) {
    std::string servers;
    std::string streaming_url;
    std::string init_url;
    if (ParseListenUrl(listen_url, &servers, &streaming_url, &init_url)) {
        return InitMemcacheChannel(servers.c_str(), streaming_url, 
                                   init_url, options);
    }
    return -1;
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
            auth_str = authenticator->bucket_name() + ':' + authenticator->bucket_password();
        }
        if (!auth_str.empty()) {
            butil::Base64Encode(auth_str, &auth);
            auth = "Basic " + auth;
        }
    }
    // TODO: encrypt auth to avoid expose in log.
    std::string unique_suffix = butil::Uint64ToString(reinterpret_cast<uint64_t>(this));
    std::string ns_url = policy::CouchbaseNamingService::BuildNsUrl(
                             servers, streaming_url, init_url, auth, unique_suffix);
    _service_name = ns_url.substr(kCouchbaseNamingServiceProtocol.size());
    return _channel.Init(ns_url.c_str(), "cb_lb", options);
}

void CouchbaseChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                  google::protobuf::RpcController* controller,
                                  const google::protobuf::Message* request,
                                  google::protobuf::Message* response,
                                  google::protobuf::Closure* done) {
    const CouchbaseRequest* req = reinterpret_cast<const CouchbaseRequest*>(request);
    Controller* cntl = static_cast<Controller*>(controller);
    ClosureGuard done_guard(done);
    std::string key;
    policy::MemcacheBinaryCommand command;
    if (req->ParseRequest(&key, &command) != 0) {
        cntl->SetFailed("Failed to parse key and command from request");
        return;
    }
    if (req->read_replicas()) {
        cntl->_couchbase_context.reset(new CouchbaseContext(true, key));	    
    }
    const size_t vb_num = 
        policy::CouchbaseNamingService::GetVBucketNumber(_service_name);
    if (vb_num == 0) {
        cntl->SetFailed("No vbuckets found");
        return;
    }
    uint32_t vb_id = CouchbaseHelper::GetVBucketId(key, vb_num);
    cntl->set_request_code(CouchbaseHelper::InitRequestCode(vb_id));
    CouchbaseRequest request_with_vb;
    if (!req->BuildVBucketId(vb_id, &request_with_vb)) {
        cntl->SetFailed("Failed to set vbucket id");
        return;
    }
    done_guard.release();
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

bool CouchbaseChannel::ParseListenUrl(
    const butil::StringPiece listen_url, std::string* server, 
    std::string* streaming_uri, std::string* init_uri) {
    do {
        const size_t pos = listen_url.find("//");
        if (pos == listen_url.npos) {
            break;
        }
        const size_t host_pos = listen_url.find('/', pos + 2);        
        if (host_pos == listen_url.npos) {
            break;
        }
        butil::StringPiece sub_str = listen_url.substr(pos + 2, host_pos - pos - 2);
        server->clear();
        server->append(sub_str.data(), sub_str.length());
        butil::StringPiece uri_sub = listen_url;
        uri_sub.remove_prefix(host_pos);
        size_t uri_pos = uri_sub.find("/bucketsStreaming/");
        if (uri_pos != uri_sub.npos) {
            streaming_uri->clear();
            streaming_uri->append(uri_sub.data(), uri_sub.length()); 
            init_uri->clear();
            init_uri->append(uri_sub.data(), uri_pos);
            init_uri->append("/buckets/");
            butil::StringPiece bucket_name = uri_sub;
            bucket_name.remove_prefix(uri_pos + std::strlen("/bucketsStreaming/"));
            init_uri->append(bucket_name.data(), bucket_name.length());
            return true;
        }
        uri_pos = uri_sub.find("/buckets/");
        if (uri_pos != uri_sub.npos) {
            init_uri->clear();
            init_uri->append(uri_sub.data(), uri_sub.length()); 
            streaming_uri->clear();
            streaming_uri->append(uri_sub.data(), uri_pos);
            streaming_uri->append("/bucketsStreaming/");
            butil::StringPiece bucket_name = uri_sub; 
            bucket_name.remove_prefix(uri_pos + std::strlen("/buckets/"));
            streaming_uri->append(bucket_name.data(), bucket_name.length());
            return true;
        }
    } while (false);
    LOG(FATAL) << "Failed to parse listen url \'" <<  listen_url << "\'.";
    return false;
}

} // namespace brpc
