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
#include "brpc/policy/couchbase_authenticator.h"
#include "brpc/policy/couchbase_naming_service.h"
#include "brpc/progressive_reader.h"
#include "bthread/bthread.h"
#include "butil/atomicops.h"
#include "butil/base64.h"
#include "butil/string_splitter.h"
#include "butil/strings/string_number_conversions.h"
#include "butil/third_party/libvbucket/hash.h"
#include "butil/third_party/libvbucket/vbucket.h"

namespace brpc {

DEFINE_string(couchbase_authorization_http_basic, "", 
              "Http basic authorization of couchbase");
DEFINE_string(couchbase_bucket_name, "", 
              "couchbase bucket name to access");
DEFINE_int32(couchbase_listen_retry_times, 5,
             "Retry times to create couchbase vbucket map monitoring connection."
             "Listen thread will sleep a while when reach this times.");
DEFINE_int32(couchbase_listen_interval_ms, 1000, 
             "Listen thread sleep for the number of milliseconds after creating"
             "vbucket map monitoring connection failure.");
DEFINE_bool(couchbase_disable_retry_during_rebalance, false, 
            "A swith indicating whether to open retry during rebalance status");
DEFINE_bool(couchbase_disable_retry_during_active, false,
            "A swith indicating whether to open retry during active status");

// Define error_code about retry during rebalance.
enum RetryReason {                                                           
    // No need retry, dummy value.
    DEFAULT_DUMMY = 0,
    // No need retry, Rpc failed except cases include in SERVER_DOWN.
    RPC_FAILED = 1,
    // Server is down, need retry other servers during rebalance.
    SERVER_DOWN = 2,
    // Server is not mapped to the bucket, need retry other servers during rebalance.
    RPC_SUCCESS_BUT_WRONG_SERVER = 3,
    // Server is mapped to the bucket, retry the same server.
    RPC_SUCCESS_BUT_RESPONSE_FAULT = 4, 
    // No need retry, response is ok.
    RESPONSE_OK = 5,
};

enum ServerType {
    // Master server choosed.
    MASTER_SERVER = 0x00,
    // Detected server choosed during rebalance
    DETECTED_SERVER = 0x01,
    // Replica server choosed for replicas read.
    REPLICA_SERVER = 0x02,
};

namespace {

const butil::StringPiece kSeparator("\n\n\n\n", 4);
const std::string kBucketStreamingUrlPrefix("/pools/default/bucketsStreaming/");
const std::string kBucketUrlPrefix("/pools/default/buckets/");
// The maximum number of vbuckets that couchbase support 
const size_t kCouchbaseMaxVBuckets = 65536;

}

class LoadBalancerWithNaming;
class VBucketContext;

// Get master server address(addr:port) of a vbucket.
// Return pointer to server if found, otherwise nullptr.
// If 'index' is not null, set server index to it.
const std::string* GetMaster(
    const VBucketServerMap* vb_map, const size_t vb_index, int* index = nullptr);

// Get forward master server address(addr:port) of a vbucket.
// Return pointer to server if found, otherwise nullptr.
// If 'index' is not null, set index of found server to it.
const std::string* GetForwardMaster(
    const VBucketServerMap* vb_map, const size_t vb_index, int* index = nullptr);

// Get replicas server address(addr:port) of a vbucket.
// Return pointer to server if found, otherwise nullptr.
// 'offset': one-based index of vbucket. 1 indicating the first replica server. 
const std::string* GetReplica(const VBucketServerMap* vb_map, 
                              const size_t vb_index, const size_t offset = 1);

// Get vbucket id of key belonged to. 
size_t Hash(const butil::StringPiece& key, const size_t vbuckets_num);

class CouchbaseServerListener;

class VBucketMapReader : public ProgressiveReader {
public:
    VBucketMapReader(CouchbaseServerListener* listener) : _listener(listener) {}
    ~VBucketMapReader() = default;

    virtual butil::Status OnReadOnePart(const void* data, size_t length);
    
    virtual void OnEndOfMessage(const butil::Status& status);

    void Attach() { _attach = true; }
    void Detach() { _attach = false; }
    bool IsAttached() { return _attach; }
    // The couchbase channel has been distructed.
    void Destroy() { _listener = nullptr; }

public:
    VBucketMapReader(const VBucketMapReader&) = delete;
    VBucketMapReader& operator=(const VBucketMapReader&) = delete;    
    
    // It is monitoring vbucket map if it is true.
    bool _attach = false;
    CouchbaseServerListener* _listener;
    std::string _buf;
    butil::Mutex _mutex;
};

// TODO: Inherit from SharedObject. Couchbase channels to connect same server 
// can share the same listener.
class CouchbaseServerListener {
public:
    CouchbaseServerListener(const char* server_list, const char* bucket_name, 
                            CouchbaseChannel* channel)
        : _streaming_url(kBucketStreamingUrlPrefix + bucket_name), 
          _cb_channel(channel),
          _reader(new VBucketMapReader(this)) {
        Init(server_list, kBucketUrlPrefix + bucket_name);
    }
    CouchbaseServerListener(const char* listen_url, CouchbaseChannel* channel)
        : _cb_channel(channel),
          _reader(new VBucketMapReader(this)) {
        std::string init_url;
        std::string server;
        if (!policy::CouchbaseNamingService::ParseListenUrl(
            listen_url, &server, &_streaming_url, &init_url)) {
            return;
        }
        Init(server.c_str(), init_url);
    }
    ~CouchbaseServerListener();
   
    void UpdateVBucketMap(butil::VBUCKET_CONFIG_HANDLE vbucket);
    
    void CreateListener();
    
private:
    CouchbaseServerListener(const CouchbaseServerListener&) = delete;
    CouchbaseServerListener& operator=(const CouchbaseServerListener&) = delete;

    void Init(const char* server_list, const std::string& init_url);

    bool InitVBucketMap(const std::string& url);

    static void* ListenThread(void* arg);

    bthread_t _listen_bth;
    // REST/JSON url to monitor vbucket map.
    std::string _streaming_url;
    std::string _auth;
    std::string _listen_port;
    std::string _service_name;
    CouchbaseChannel* _cb_channel;
    // Monitor couchbase vbuckets map on this channel. 
    // TODO: Add/removed server due to rebalance/failover.
    Channel _listen_channel;
    // If _reader is not attached to listen socket, it will be released in
    // CouchbaseServerListener desconstruction. Otherwise, it will be released
    // by itself.
    VBucketMapReader* _reader;
};

butil::Status VBucketMapReader::OnReadOnePart(const void* data, size_t length) {
    BAIDU_SCOPED_LOCK(_mutex);
    // If '_listener' is desconstructed, return error status directly.    
    if (_listener == nullptr) {
        return butil::Status(-1, "Couchbase channel is destroyed");
    }

    _buf.append(static_cast<const char*>(data), length);
    size_t pos = 0;
    size_t new_pos = _buf.find(kSeparator.data(), pos, kSeparator.size());
    while (new_pos != std::string::npos) {
        std::string complete = _buf.substr(pos, new_pos - pos);
        butil::VBUCKET_CONFIG_HANDLE vb = 
            butil::vbucket_config_parse_string(complete.c_str());
        if (vb != nullptr) {
            _listener->UpdateVBucketMap(vb);
            butil::vbucket_config_destroy(vb);
        }
        pos = new_pos + kSeparator.size();
        new_pos = _buf.find(kSeparator.data(), pos, kSeparator.size());
    } 
    if (pos < _buf.size()) {
        _buf = _buf.substr(pos);
    } else {
        _buf.clear();
    }

    return butil::Status::OK();
}

void VBucketMapReader::OnEndOfMessage(const butil::Status& status) {
    {    
        BAIDU_SCOPED_LOCK(_mutex);
        if (_listener != nullptr) {
            _buf.clear();
            Detach();
            _listener->CreateListener();
            return;
        }
    }
    
    // If '_listener' is desconstructed, release this object.    
    std::unique_ptr<VBucketMapReader> release(this);
}

CouchbaseServerListener::~CouchbaseServerListener() {
    std::unique_lock<butil::Mutex> mu(_reader->_mutex);
    bthread_stop(_listen_bth);
    bthread_join(_listen_bth, nullptr);
    if (!_reader->IsAttached()) {
        mu.unlock();
        std::unique_ptr<VBucketMapReader> p(_reader);
    } else {
        _reader->Destroy();
    }
    policy::CouchbaseNamingService::ClearNamingServiceData(_service_name);
}

void CouchbaseServerListener::Init(const char* server_list, 
                                   const std::string& init_url) {
    if (!FLAGS_couchbase_authorization_http_basic.empty()) {
        butil::Base64Encode(FLAGS_couchbase_authorization_http_basic, &_auth);
        _auth = "Basic " + _auth;
    } else { 
        std::string auth_str = _cb_channel->GetAuthentication();
        if (!auth_str.empty()) {
            butil::Base64Encode(auth_str, &_auth);
            _auth = "Basic " + _auth;
        }
    }
    ChannelOptions options;
    options.protocol = PROTOCOL_HTTP;
    std::string ns_servers;
    butil::StringPiece servers(server_list);
    if (servers.find("//") == servers.npos) {
        ns_servers.append("couchbase_list://");
    }
    ns_servers.append(server_list);
    std::string unique_id = "_" + butil::Uint64ToString(reinterpret_cast<uint64_t>(this));
    ns_servers += unique_id;
    _service_name = server_list + unique_id;
    CHECK(_listen_channel.Init(ns_servers.c_str(), "rr", &options) == 0) 
        << "Failed to init listen channel.";
    if (!InitVBucketMap(init_url)) {
        LOG(ERROR) << "Failed to init vbucket map.";
    }
    CreateListener();
}
 
bool CouchbaseServerListener::InitVBucketMap(const std::string& uri) {
    Controller cntl;
    for (int i = 0; i != FLAGS_couchbase_listen_retry_times; ++i) {
        cntl.Reset();
        if (!_auth.empty()) {
            cntl.http_request().SetHeader("Authorization", _auth);
        }
        cntl.http_request().uri() = uri;
        _listen_channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
        if (cntl.Failed()) {
            LOG(ERROR) << "Failed to get vbucket map: " << cntl.ErrorText();
            continue;
        }
        std::string str = cntl.response_attachment().to_string();
        butil::VBUCKET_CONFIG_HANDLE vb = 
            butil::vbucket_config_parse_string(str.c_str());
        if (vb != nullptr) {
            _listen_port = butil::IntToString(cntl.remote_side().port);
            UpdateVBucketMap(vb);
            butil::vbucket_config_destroy(vb);
            return true;
        }
    }
    return false;
}

void* CouchbaseServerListener::ListenThread(void* arg) {
    CouchbaseServerListener* listener = 
        static_cast<CouchbaseServerListener*>(arg);
    while (true) {
        listener->_reader->Detach();  
        Controller cntl;
        int i = 0;
        for (; i != FLAGS_couchbase_listen_retry_times; ++i) {
            if (!listener->_auth.empty()) {
                cntl.http_request().SetHeader("Authorization", listener->_auth);
            }
            cntl.http_request().uri() = listener->_streaming_url;
            cntl.response_will_be_read_progressively();
            listener->_listen_channel.CallMethod(nullptr, &cntl, 
                                                 nullptr, nullptr, nullptr);
            if (cntl.Failed()) {
                LOG(ERROR) << "Failed to create vbucket map reader: " 
                           << cntl.ErrorText();
                cntl.Reset();
                continue;
            }
            break;
        }
       
        if (i == FLAGS_couchbase_listen_retry_times) { 
            if (bthread_usleep(FLAGS_couchbase_listen_interval_ms * 1000) < 0) {
                if (errno == ESTOP) {
                    LOG(INFO) << "ListenThread is stopped.";
                    break;
                }
                LOG(ERROR) << "Failed to sleep.";
            }
            continue;
        }  
        // Set listen port if init failure in InitVBucketMap. 
        if (listener->_listen_port.empty()) {
            listener->_listen_port = butil::IntToString(cntl.remote_side().port);
        } 
        listener->_reader->Attach();  
        cntl.ReadProgressiveAttachmentBy(listener->_reader);  
        break;
    }
    
    return nullptr;
}

void CouchbaseServerListener::CreateListener() {
    // TODO: keep one listen thread waiting on futex.
    CHECK(bthread_start_urgent(
        &_listen_bth, nullptr, ListenThread, this) == 0)
        << "Failed to start listen thread.";  
}

void CouchbaseServerListener::UpdateVBucketMap(
    butil::VBUCKET_CONFIG_HANDLE vb_conf) {
    if (vb_conf == nullptr) {
        LOG(ERROR) << "Null VBUCKET_CONFIG_HANDLE.";
        return;
    }

    // TODO: ketama distribution
    if (butil::vbucket_config_get_distribution_type(vb_conf) 
        != butil::VBUCKET_DISTRIBUTION_VBUCKET) {
        LOG(FATAL) << "Only support vbucket distribution.";
        return;
    }

    const VBucketServerMap* vb_map = _cb_channel->vbucket_map();
    const size_t vb_num = butil::vbucket_config_get_num_vbuckets(vb_conf);
    const size_t replicas_num = butil::vbucket_config_get_num_replicas(vb_conf);
    const size_t server_num = butil::vbucket_config_get_num_servers(vb_conf);
    std::vector<std::vector<int>> vbuckets(vb_num);
    std::vector<std::vector<int>> fvbuckets;
    std::vector<std::string> servers(server_num);
    std::vector<std::string> added_servers;
    std::vector<std::string> removed_servers;
    if (butil::vbucket_config_has_forward_vbuckets(vb_conf)) {
        fvbuckets.resize(vb_num);
    }
    for (size_t i = 0; i != vb_num; ++i) {
        if (butil::vbucket_config_has_forward_vbuckets(vb_conf)) {
            fvbuckets[i].resize(replicas_num + 1, -1);
        }
        vbuckets[i].resize(replicas_num + 1, -1);
        vbuckets[i][0] = butil::vbucket_get_master(vb_conf, i);
        if (butil::vbucket_config_has_forward_vbuckets(vb_conf)) {
            fvbuckets[i][0] = butil::fvbucket_get_master(vb_conf, i);
        } 
        for (size_t j = 0; j < replicas_num; ++j) {
            vbuckets[i][j+1] = butil::vbucket_get_replica(vb_conf, i, j);
            if (butil::vbucket_config_has_forward_vbuckets(vb_conf)) {
                fvbuckets[i][j+1] = butil::fvbucket_get_replica(vb_conf, i, j);
            } 
        }
    }
    std::vector<size_t> keeping_servers;
    for (size_t i = 0; i != server_num; ++i) {
        servers[i] = butil::vbucket_config_get_server(vb_conf, i);
        const auto iter = vb_map->_channel_map.find(servers[i]);
        if (iter == vb_map->_channel_map.end()) {
            added_servers.emplace_back(servers[i]);
        } else {
            keeping_servers.emplace_back(i);
        }
    }
    for (size_t i = 0; i != vb_map->_servers.size(); ++i) {
        size_t j = 0;
        for (; j != keeping_servers.size(); ++j) {
            if (vb_map->_servers[i] == servers[keeping_servers[j]]) {
                break;
            }
        }
        if (j == keeping_servers.size()) {
            removed_servers.emplace_back(vb_map->_servers[i]);
        }
    }
    // Reset new server list of listen channel. 
    if (!added_servers.empty() || !removed_servers.empty()) {
        std::string server_list;
        for (const auto& server : servers) {
            const size_t pos = server.find(':');
            server_list.append(server.data(), pos);
            server_list += ":" + _listen_port + ",";
        }
        server_list.pop_back();
        policy::CouchbaseNamingService::ResetCouchbaseListenerServers(
            _service_name, server_list);
    }

    bool curr_rebalance = _cb_channel->IsInRebalancing(vb_map);
    bool update_rebalance = !fvbuckets.empty();
    uint64_t version = vb_map->_version;
    _cb_channel->UpdateVBucketServerMap(replicas_num, vbuckets, fvbuckets, 
                                        servers, added_servers, removed_servers);
    if (!curr_rebalance && update_rebalance) {
        LOG(ERROR) << "Couchbase enters into rebalance status from version " 
                   << ++version;
    }
    if (curr_rebalance && !update_rebalance) {
        DetectedVBucketMap& detect = *_cb_channel->_detected_vbucket_map;
        for (size_t vb_index = 0; vb_index != vb_num; ++vb_index) {
            detect[vb_index]._verified.store(false, butil::memory_order_relaxed);
            detect[vb_index]._index.store(-1, butil::memory_order_relaxed); 
        }
        LOG(ERROR) << "Couchbase quit rebalance status from version " 
                   << ++version;
    }
}

class VBucketContext {
public:
    VBucketContext() = default;
    ~VBucketContext() = default;

    bool Init(const VBucketServerMap* vb_map, const size_t vb_index, 
              const int server_type, const int server_index, 
              const CouchbaseRequest* request, const std::string& key, 
              const policy::MemcacheBinaryCommand command);

    VBucketStatus Update(const VBucketServerMap* vb_map, 
                         const size_t vb_index);

    const CouchbaseRequest* GetReplicasReadRequest();

public:
    size_t _retried_count = 0;
    uint64_t _version = 0;
    int _server_type = MASTER_SERVER;
    int _server_index = 0;
    size_t _vbucket_index;
    policy::MemcacheBinaryCommand _command;
    std::string _forward_master;
    std::string _master;
    std::string _key; 
    CouchbaseRequest _request;
    CouchbaseRequest _replica_request;
};

bool VBucketContext::Init(const VBucketServerMap* vb_map, 
                          const size_t vb_index, 
                          const int server_type,
                          const int server_index,
                          const CouchbaseRequest* request,
                          const std::string& key, 
                          const policy::MemcacheBinaryCommand command) {
    if (vb_map->_version == 0) {
        return false;
    }
    _version = vb_map->_version;
    _vbucket_index = vb_index;
    _server_type = server_type;
    _server_index = server_index;
    _command = command;
    _key = key;
    const std::string* fm = GetForwardMaster(vb_map, vb_index);
    if (fm != nullptr) {
        _forward_master = *fm;
    }
    const std::string* master = GetMaster(vb_map, vb_index);
    _master = *master;
    if (!request->BuildVBucketId(vb_index, &_request)) {
        return false;
    }
    return true;
}

VBucketStatus VBucketContext::Update(
    const VBucketServerMap* vb_map, const size_t vb_index) {
    VBucketStatus change = NO_CHANGE;
    if (_version == vb_map->_version) {
        change = NO_CHANGE;
        return change;
    }
    _version = vb_map->_version;
    const std::string* fm = GetForwardMaster(vb_map, vb_index); 
    const std::string* master = GetMaster(vb_map, vb_index);    
    if (_forward_master.empty()) {
        if (fm == nullptr) {
            if (_master == *master) {
                change = MASTER_KEEPING_WITHOUT_F;
            } else {
                change = MASTER_CHANGE_WITHOUT_F;
            }
        } else {
            change = FORWARD_CREATE;
        }
    } else {
        if (fm == nullptr) {
            change = FORWARD_FINISH;
        } else {
            if (_forward_master == *fm) {
                change = FORWARD_KEEPING;
            } else {
                change = FORWARD_CHANGE;
            }
        }
    }
    if (fm != nullptr) {
        _forward_master = *fm;
    }
    _master = *master;
    return change;
} 

const CouchbaseRequest* VBucketContext::GetReplicasReadRequest() {
    if (!_replica_request.IsInitialized()) {
        _replica_request.ReplicasGet(_key, _vbucket_index);
    }
    return &_replica_request;
}

class CouchbaseDone : public google::protobuf::Closure { 
friend class CouchbaseChannel;
public:
    CouchbaseDone(CouchbaseChannel* cb_channel, brpc::Controller* cntl, 
                  CouchbaseResponse* response, google::protobuf::Closure* done)
        : _cb_channel(cb_channel), _done(done), 
          _cntl(cntl), _response(response) { }

    void Run();

private:
    CouchbaseChannel* _cb_channel;
    google::protobuf::Closure* _done;   
    brpc::Controller* _cntl;
    CouchbaseResponse* _response;
    VBucketContext _vb_context;
};

void CouchbaseDone::Run() {
    std::unique_ptr<CouchbaseDone> self_guard(this);
    ClosureGuard done_guard(_done);
    if (FLAGS_couchbase_disable_retry_during_rebalance) {
        return;
    }
    int reason = 0;
    std::string error_text;
    bool retry = _cb_channel->IsNeedRetry(_cntl, _vb_context, 
                                          _response, &reason, &error_text);
    _cb_channel->UpdateDetectedMasterIfNeeded(reason, _vb_context);
    if (!retry) {
        return;
    }
    int64_t remain_ms = _cntl->timeout_ms() - _cntl->latency_us() / 1000;
    if (remain_ms <= 0) {
        _cntl->SetFailed(ERPCTIMEDOUT, "reach timeout, finish retry");
        return;
    }
    if (reason != SERVER_DOWN) {
        _cntl->SetFailed(error_text);
    }
    Controller retry_cntl;
    retry_cntl.set_timeout_ms(remain_ms);
    // TODO: Inherit other fields except of timeout_ms of _cntl.    
    retry_cntl.set_log_id(_cntl->log_id());
    retry_cntl.set_max_retry(0);
    while (true) {
        // TODO: _cntl cancel
        if (!_cb_channel->DoRetry(reason, &retry_cntl, 
                                  _response, &_vb_context)) {
            break;
        }
        reason = 0;
        retry = _cb_channel->IsNeedRetry(&retry_cntl, _vb_context, 
                                         _response, &reason, &error_text);
        _cb_channel->UpdateDetectedMasterIfNeeded(reason, _vb_context);
        if (!retry) {
            break;
        }
        remain_ms = retry_cntl.timeout_ms() - retry_cntl.latency_us() / 1000;
        if (remain_ms <= 0) {
            retry_cntl.SetFailed(ERPCTIMEDOUT, "reach timeout, finish retry");
            break;
        }
        _cntl->SetFailed(error_text);
        retry_cntl.Reset();
        retry_cntl.set_timeout_ms(remain_ms);
        // TODO: Inherit other fields except of timeout_ms of _cntl.    
        retry_cntl.set_log_id(_cntl->log_id());
        retry_cntl.set_max_retry(0);
    }
    // Fetch result from retry_cntl to _cntl. They share the same response.
    if (retry_cntl.Failed()) {
        _cntl->SetFailed(retry_cntl.ErrorText());
    }
    _cntl->_error_code = retry_cntl.ErrorCode();
    _cntl->_remote_side = retry_cntl.remote_side();
    _cntl->OnRPCEnd(butil::gettimeofday_us());
}

CouchbaseChannel::CouchbaseChannel() {}

CouchbaseChannel::~CouchbaseChannel() {
    _listener.reset(nullptr);
}

int CouchbaseChannel::Init(const char* listen_url, const ChannelOptions* options) {
    if (options != nullptr) {
        if (options->protocol != PROTOCOL_UNKNOWN && 
            options->protocol != PROTOCOL_MEMCACHE) {
            LOG(FATAL) << "Failed to init channel due to invalid protocol " 
                       <<  options->protocol.name() << '.';
            return -1;
        }
        _common_options = *options;
    }
    _common_options.protocol = PROTOCOL_MEMCACHE;
    _detected_vbucket_map.reset(
        new std::vector<DetectedMaster>(kCouchbaseMaxVBuckets));
    auto ptr = new CouchbaseServerListener(listen_url, this);
    if (ptr == nullptr) {
        LOG(FATAL) << "Failed to init CouchbaseChannel to " << listen_url << '.';
        return -1;
    }
    _listener.reset(ptr);
    return 0;
}

int CouchbaseChannel::Init(const char* server_addr, const char* bucket_name, 
                           const ChannelOptions* options) {
    if (options != nullptr) {
        if (options->protocol != PROTOCOL_UNKNOWN && 
            options->protocol != PROTOCOL_MEMCACHE) {
            LOG(FATAL) << "Failed to init channel due to invalid protocol " 
                       <<  options->protocol.name() << '.';
            return -1;
        }
        _common_options = *options;
    }
    _common_options.protocol = PROTOCOL_MEMCACHE;
    _detected_vbucket_map.reset(
        new std::vector<DetectedMaster>(kCouchbaseMaxVBuckets));
    auto ptr = new CouchbaseServerListener(server_addr, bucket_name, this);
    if (ptr == nullptr) {
        LOG(FATAL) << "Failed to init CouchbaseChannel to " << server_addr << '.';
        return -1;
    }
    _listener.reset(ptr);
    return 0;
} 

void CouchbaseChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                  google::protobuf::RpcController* controller,
                                  const google::protobuf::Message* request,
                                  google::protobuf::Message* response,
                                  google::protobuf::Closure* done) {
    const CouchbaseRequest* req = reinterpret_cast<const CouchbaseRequest*>(request);
    Controller* cntl = static_cast<Controller*>(controller);
    Channel* channel = nullptr;
    ClosureGuard done_guard(done);
    std::string key;
    policy::MemcacheBinaryCommand command;
    if (req->ParseRequest(&key, &command) != 0) {
        cntl->SetFailed("failed to parse key and command from request");
        return;
    }
    const CallId call_id = cntl->call_id();
    {
        butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
        if(_vbucket_map.Read(&vb_map) != 0) {
            cntl->SetFailed(ENOMEM, "failed to read vbucket map");
            return;
        }
        if (vb_map->_version == 0) {
            cntl->SetFailed(ENODATA, "vbucket map is not initialize");
            return;
        } 
        ServerType type = MASTER_SERVER; 
        int index = -1;
        const size_t vb_index = Hash(key, vb_map->_vbucket.size());
        if (!IsInRebalancing(vb_map.get()) || 
            FLAGS_couchbase_disable_retry_during_rebalance) {
            const std::string* server = GetMaster(vb_map.get(), vb_index, &index);
            channel = GetMappedChannel(server, vb_map.get());
        } else {
            // Close the default retry policy. CouchbaeChannel decide to how to retry.
            cntl->set_max_retry(0);
            index = GetDetectedMaster(vb_map.get(), vb_index);
            if (index >= 0) {
                type = DETECTED_SERVER; 
                channel = GetMappedChannel(&vb_map->_servers[index], vb_map.get());
            } else {
                const std::string* server = GetMaster(vb_map.get(), vb_index, &index);
                channel = GetMappedChannel(server, vb_map.get());
            }
        }
        if (channel == nullptr) {
            cntl->SetFailed(ENODATA,"failed to get mapped channel");
            return;
        }
        CouchbaseDone* cb_done = new CouchbaseDone(
            this, cntl, static_cast<CouchbaseResponse*>(response), done);
        if (!cb_done->_vb_context.Init(vb_map.get(), vb_index, type, 
                                       index, req, key, command)) {
            cntl->SetFailed(ENOMEM, "failed to init couchbase context");
            return;
        }
        done_guard.release();
        channel->CallMethod(nullptr, cntl, &cb_done->_vb_context._request, 
                            response, cb_done);
    }
    if (done == nullptr) {
        Join(call_id);
    }
    return;
}

Channel* CouchbaseChannel::SelectBackupChannel(
    const VBucketServerMap* vb_map, const size_t vb_index, 
    const int reason, VBucketContext* context) {
    VBucketStatus change = VBucketStatus::NO_CHANGE;
    if (vb_map->_version != context->_version) {
        change = context->Update(vb_map, vb_index);
    }
    const std::string* server = GetNextRetryServer(change, reason, vb_map, 
                                                   vb_index, context);
    return server ? GetMappedChannel(server, vb_map) : nullptr;
}

const VBucketServerMap* CouchbaseChannel::vbucket_map() {
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
    if(_vbucket_map.Read(&vbucket_map) != 0) {
        LOG(ERROR) << "Failed to read vbucket map.";
        return nullptr;
    }
    return vbucket_map.get();
}

Channel* CouchbaseChannel::GetMappedChannel(const std::string* server,
                                            const VBucketServerMap* vb_map) {
    if (server == nullptr || server->empty()) {
        return nullptr;
    }
    auto iter = vb_map->_channel_map.find(*server);
    if (iter != vb_map->_channel_map.end()) {
        return iter->second.get();
    }
    return nullptr;
}

bool CouchbaseChannel::IsNeedRetry(
    const Controller* cntl, const VBucketContext& context,
    CouchbaseResponse* response, int* reason, std::string* error_text) {
    *reason = DEFAULT_DUMMY;
    error_text->clear();
    const int error_code = cntl->ErrorCode();
    if (error_code != 0) {
        if (error_code == EHOSTDOWN || error_code == ELOGOFF || 
            error_code == EFAILEDSOCKET || error_code == EEOF || 
            error_code == ECLOSE || error_code == ECONNRESET) {
            *reason = SERVER_DOWN;
            error_text->append(cntl->ErrorText());
            error_text->append(";");
        } else {
            *reason = RPC_FAILED;
        }
    } else {
        CouchbaseResponse::Status status = CouchbaseResponse::STATUS_SUCCESS;
        const size_t vb_index = context._vbucket_index;
        if (response->GetStatus(&status)) {
            if (status != CouchbaseResponse::STATUS_SUCCESS) { 
                *reason = status == CouchbaseResponse::STATUS_NOT_MY_VBUCKET
                          ? RPC_SUCCESS_BUT_WRONG_SERVER 
                          : RPC_SUCCESS_BUT_RESPONSE_FAULT;
                error_text->append(CouchbaseResponse::status_str(status));
                error_text->append(
                    "(vbucket_id=" + butil::IntToString(vb_index) + ") latency=" 
                    + butil::Int64ToString(cntl->latency_us()) + "us @");
                error_text->append(butil::endpoint2str(cntl->remote_side()).c_str());
                error_text->append(";");
            } else {
                *reason = RESPONSE_OK;
            }
        }
    } 
    if (IsInRebalancing(vbucket_map())) {
        return *reason == SERVER_DOWN || 
               *reason == RPC_SUCCESS_BUT_WRONG_SERVER || 
               *reason == RPC_SUCCESS_BUT_RESPONSE_FAULT; 
    } else if(!FLAGS_couchbase_disable_retry_during_active) {
        return *reason == RPC_SUCCESS_BUT_WRONG_SERVER || 
               (*reason == SERVER_DOWN && context._request.read_replicas()); 
    }
    return false;
}

bool CouchbaseChannel::DoRetry(const int reason, Controller* cntl, 
    CouchbaseResponse* response, VBucketContext* vb_ctx) {
    {
        butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
        if(_vbucket_map.Read(&vb_map) != 0) {
            cntl->SetFailed(ENOMEM, "failed to read vbucket map");
            return false;
        }  
        if (++(vb_ctx->_retried_count) >= vb_map->_servers.size()) {
            cntl->SetFailed("Reach the max couchbase retry count");
            return false;
        }
        const size_t vb_index = vb_ctx->_vbucket_index;
        Channel* channel = SelectBackupChannel(vb_map.get(), vb_index,
                                               reason, vb_ctx);
        if (channel == nullptr) {
            cntl->SetFailed(ENODATA, "no buckup server found");
            return false;
        }
        const CouchbaseRequest* request = &(vb_ctx->_request);
        if (vb_ctx->_server_type == REPLICA_SERVER) {
            request = vb_ctx->GetReplicasReadRequest();
        }
        response->Clear();
        channel->CallMethod(nullptr, cntl, request, response, DoNothing());
    }
    Join(cntl->call_id());
    return true;
}

const std::string* CouchbaseChannel::GetNextRetryServer(
    const VBucketStatus change, const int reason, const VBucketServerMap* vb_map, 
    const size_t vb_index, VBucketContext* context) {
    int curr_index = context->_server_index;
    const int server_num = vb_map->_servers.size();
    if (IsInRebalancing(vb_map)) {
        // keep current server to retry if it is right server of the vbucket.
        if (reason != RPC_SUCCESS_BUT_WRONG_SERVER 
            && reason != SERVER_DOWN) {
           if (curr_index < server_num) {
               return &(vb_map->_servers[curr_index]);
           }
        }
        int next_index = GetDetectedMaster(vb_map, vb_index);
        if(next_index >= 0) {
            context->_server_type = DETECTED_SERVER;
            context->_server_index = next_index; 
            return &(vb_map->_servers[next_index]);
        }
        int dummy_index = -1;
        // Retry forward master as first if having forward master. Otherwise, 
        // probe other servers.
        if(!GetForwardMaster(vb_map, vb_index, &next_index)) {
            next_index = (curr_index + 1) % server_num;
        }
        (*_detected_vbucket_map)[vb_index]._index.compare_exchange_strong(
            dummy_index, next_index, butil::memory_order_release);
        context->_server_type = DETECTED_SERVER;
        context->_server_index = next_index;
        return &(vb_map->_servers[next_index]);
    } else {
        if (change == FORWARD_FINISH || change == MASTER_CHANGE_WITHOUT_F) {
            context->_server_type = MASTER_SERVER;
            return GetMaster(vb_map, vb_index, &context->_server_index);
        } else {
            if (reason == SERVER_DOWN && context->_request.read_replicas()) {
                context->_server_type = REPLICA_SERVER;
                return GetReplica(vb_map, vb_index);
            }
            if (reason == RPC_SUCCESS_BUT_WRONG_SERVER) { 
                context->_server_type = DETECTED_SERVER;
                context->_server_index = (curr_index + 1) % server_num;
                // TODO: need update detect server.
                return &(vb_map->_servers[context->_server_index]);
            }
        } 
    }
    return nullptr; 
}

void CouchbaseChannel::UpdateDetectedMasterIfNeeded(
    const int reason, const VBucketContext& context) {
    if (context._server_type == REPLICA_SERVER) {
        return;
    }
    if (reason == DEFAULT_DUMMY || reason == RPC_FAILED) {
        return;
    }
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
    if(_vbucket_map.Read(&vb_map) != 0) {
        LOG(ERROR) << "Failed to read vbucket map.";
        return;
    }
    if (!IsInRebalancing(vb_map.get())) {
        return;
    }
    const int server_num = vb_map->_servers.size();
    int curr_index = context._server_index;
    if (curr_index >= server_num) {
        return;
    } 
    const size_t vb_index = context._vbucket_index; 
    DetectedMaster& detect_master = (*_detected_vbucket_map)[vb_index];
    butil::atomic<bool>& is_verified = detect_master._verified;
    butil::atomic<int>& index = detect_master._index;
    if (reason != SERVER_DOWN && reason != RPC_SUCCESS_BUT_WRONG_SERVER) {
        if (context._server_type == MASTER_SERVER) {
            return;
        }
        // We detected the right new master for vbucket no matter the
        // response status is success or not. Record for following request
        // during rebalancing.
        if (curr_index != index.load(butil::memory_order_acquire)) {
            index.store(curr_index, butil::memory_order_relaxed);
        }
        if (!is_verified.load(butil::memory_order_acquire)) {
            is_verified.store(true, butil::memory_order_relaxed); 
        }
    } else { 
        // Server is down or it is a wrong server of the vbucket. Go on probing 
        // other servers.
        int dummy_index = -1;
        int next_index = -1;
        // Detect forward master as the first if having forwad master, 
        // otherwise, probe other servers. 
        if (dummy_index == index.load(butil::memory_order_acquire)) {
            if(!GetForwardMaster(vb_map.get(), vb_index, &next_index)) {
                next_index = (curr_index + 1) % server_num;
            }
            index.compare_exchange_strong(dummy_index, next_index,
                                          butil::memory_order_release, 
                                          butil::memory_order_relaxed);
            if (is_verified.load(butil::memory_order_acquire)) {
                is_verified.store(false, butil::memory_order_relaxed); 
            }
        } else {
            next_index = (curr_index + 1) % server_num;
            if (is_verified.load(butil::memory_order_acquire)) {
                // Verified master server is invalid. Reset to detect again.
                if (index.compare_exchange_strong(curr_index, -1, 
                                                  butil::memory_order_relaxed, 
                                                  butil::memory_order_relaxed)) {
                   is_verified.store(false, butil::memory_order_relaxed); 
               }
            } else {
                // Probe next servers.
                index.compare_exchange_strong(curr_index, next_index, 
                                              butil::memory_order_release, 
                                              butil::memory_order_relaxed);
            } 
        }
    }
}

int CouchbaseChannel::GetDetectedMaster(const VBucketServerMap* vb_map, 
                                        const size_t vb_index) {
    butil::atomic<int>& detected_index = (*_detected_vbucket_map)[vb_index]._index;
    const int server_num = vb_map->_servers.size();
    int curr_index = detected_index.load(butil::memory_order_acquire);
    if (curr_index >= 0 && curr_index < server_num) {
        return curr_index;
    }
    if (curr_index >= server_num) {
        detected_index.compare_exchange_strong(
            curr_index, -1, butil::memory_order_relaxed);
    } 
    return -1;
}

bool CouchbaseChannel::UpdateVBucketServerMap(
   const size_t num_replicas,
   std::vector<std::vector<int>>& vbucket,
   std::vector<std::vector<int>>& fvbucket,
   std::vector<std::string>& servers,
   const std::vector<std::string>& added_servers,
   const std::vector<std::string>& removed_servers) {
    auto fn = std::bind(Update, 
                        std::placeholders::_1, 
                        &_common_options,
                        num_replicas,
                        vbucket,
                        fvbucket, 
                        servers,
                        added_servers,
                        removed_servers);
    return _vbucket_map.Modify(fn);
}

bool CouchbaseChannel::Update(VBucketServerMap& vbucket_map, 
                              const ChannelOptions* options,
                              const size_t num_replicas,
                              std::vector<std::vector<int>>& vbucket,
                              std::vector<std::vector<int>>& fvbucket,
                              std::vector<std::string>& servers,
                              const std::vector<std::string>& added_servers,
                              const std::vector<std::string>& removed_servers) {
    bool ret = true;
    ++(vbucket_map._version);
    vbucket_map._num_replicas = num_replicas;
    vbucket_map._vbucket.swap(vbucket);
    vbucket_map._fvbucket.swap(fvbucket);
    vbucket_map._servers.swap(servers);
    if (!added_servers.empty()) {
        for (const auto& server : added_servers) {
            std::unique_ptr<Channel> p(new Channel());
            if (p == nullptr) {
                LOG(FATAL) << "Failed to init channel.";
                return false;
            }
            if (p->Init(server.c_str(), options) != 0) {
                LOG(FATAL) << "Failed to init channel.";
                return false;
            }
            auto pair = vbucket_map._channel_map.emplace(server, std::move(p));
            if (!pair.second) {
                LOG(ERROR) << "Failed to add vbucket channel: " << server;
                ret = false;
            }
        }
    }
    if (!removed_servers.empty()) {
        for (const auto& server: removed_servers) {
            auto n = vbucket_map._channel_map.erase(server);
            if (n == 0) {
                LOG(ERROR) << "Failed to remove vbucket channel: " << server;
                ret = false;
            }            
        }
    }

    return ret;
}

std::string CouchbaseChannel::GetAuthentication() const {
    const policy::CouchbaseAuthenticator* auth = reinterpret_cast<
        const policy::CouchbaseAuthenticator*>(_common_options.auth);
    std::string auth_str;
    if (auth != nullptr && !auth->bucket_name().empty() 
        && !auth->bucket_password().empty()) {
        auth_str = auth->bucket_name() + ':' + auth->bucket_password();
    }
    
    return std::move(auth_str); 
}

void CouchbaseChannel::Describe(std::ostream& os, 
                                const DescribeOptions& options) {
    os << "Couchbase channel[";
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
    if(_vbucket_map.Read(&vbucket_map) != 0) {
        os << "Failed to read _vbucket_map"; 
    } else {
        os << "n=" << vbucket_map->_servers.size() << ':';
        for (const auto& servers : vbucket_map->_servers) {
            os << ' ' << servers << ';';
        }
    }
    os << "]";
}

int CouchbaseChannel::CheckHealth() {
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
    if(_vbucket_map.Read(&vbucket_map) != 0) {
        return -1;
    }
    for (const auto& channel_pair : vbucket_map->_channel_map) {
        if (channel_pair.second->CheckHealth() != 0) {
            return -1;
        }
    }
    return 0;
}

const std::string* GetMaster(const VBucketServerMap* vb_map, 
                             const size_t vb_index, int* index) {
    if (vb_index < vb_map->_vbucket.size()) {
        const int i = vb_map->_vbucket[vb_index][0];
        if (i >= 0 && i < static_cast<int>(vb_map->_servers.size())) {
            if (index != nullptr) {
                *index = i;
            }
           return &vb_map->_servers[i];
        }
    } 
    return nullptr;
}

const std::string* GetForwardMaster(const VBucketServerMap* vb_map, 
                                    const size_t vb_index, int* index) {
    if (vb_index < vb_map->_fvbucket.size()) {
        const int i = vb_map->_fvbucket[vb_index][0];
        if (i >= 0 && i < static_cast<int>(vb_map->_servers.size())) {
            if (index != nullptr) {
                *index = i;
            }
            return &vb_map->_servers[i];
        }
    } 
    if (index != nullptr) {
        *index = -1;
    }
    return nullptr;
}

const std::string* GetReplica(const VBucketServerMap* vb_map, 
                              const size_t vb_index, const size_t offset) {
    if (vb_index < vb_map->_vbucket.size() && offset <= vb_map->_num_replicas) {
        const int index =  vb_map->_vbucket[vb_index][offset];
        if (index != -1) {
            return &vb_map->_servers[index];
        }
    }  
    return nullptr;
}

size_t Hash(const butil::StringPiece& key, const size_t vbuckets_num) {
    size_t digest = butil::hash_crc32(key.data(), key.size());
    return digest & (vbuckets_num - 1);
}

} // namespace brpc
