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
#include "brpc/progressive_reader.h"
#include "bthread/bthread.h"
#include "butil/base64.h"
#include "butil/third_party/libvbucket/hash.h"
#include "butil/third_party/libvbucket/vbucket.h"

namespace brpc {

DEFINE_string(couchbase_authorization_http_basic, "", 
              "Http basic authorization of couchbase");
DEFINE_string(couchbase_bucket_init_string, "", 
              "If the string is set, 'CouchbaseServerListener' will build vbucket map"
              "directly by parsing from this string in initialization");
DEFINE_string(couchbase_bucket_streaming_url, 
              "/pools/default/bucketsStreaming/",
              "Monitor couchbase vbuckets map through this url");
DEFINE_int32(listener_retry_times, 5,
             "Retry times to create couchbase vbucket map monitoring connection."
             "Listen thread will sleep a while when reach this times.");
DEFINE_int32(listener_sleep_interval_ms, 100, 
             "Listen thread sleep for the number of milliseconds after creating"
             "vbucket map monitoring connection failure.");
DEFINE_bool(retry_during_rebalance, true, 
            "A swith indicating whether to open retry during rebalance");
DEFINE_bool(replicas_read_flag, false, 
            "Read replicas for get request in case of master node failure."
            "This does not ensure that the data is the most current.");

namespace {

const butil::StringPiece kSeparator("\n\n\n\n", 4);

}

class CouchbaseServerListener;

enum RetryReason {                                                           
    RPC_FAILED = 0,
    WRONG_SERVER = 1,
    RESPONSE_FAULT = 2,
};

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

class CouchbaseServerListener {
public:
    CouchbaseServerListener(const char* server_addr, CouchbaseChannel* channel)
        : _server_addr(server_addr), 
          _url(FLAGS_couchbase_bucket_streaming_url), 
          _cb_channel(channel),
          _reader(new VBucketMapReader(this)) {
        Init();
    }

    ~CouchbaseServerListener();
   
    void UpdateVBucketMap(butil::VBUCKET_CONFIG_HANDLE vbucket);
    
    void CreateListener();
    
private:
    CouchbaseServerListener(const CouchbaseServerListener&) = delete;
    CouchbaseServerListener& operator=(const CouchbaseServerListener&) = delete;

    void Init();

    void InitVBucketMap(const std::string& str);

    static void* ListenThread(void* arg);

    bthread_t _listen_bth;
    // Server address list of couchbase servers. From these servers(host:port),
    // we can monitor vbucket map.
    const std::string _server_addr;
    // REST/JSON url to monitor vbucket map.
    const std::string _url;
    std::string _auth;
    CouchbaseChannel* _cb_channel;
    // Monitor couchbase vbuckets map on this channel. 
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
        std::string complete = _buf.substr(pos, new_pos);
        butil::VBUCKET_CONFIG_HANDLE vb = 
            butil::vbucket_config_parse_string(complete.c_str());
        _listener->UpdateVBucketMap(vb);
        if (vb != nullptr) {
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

void CouchbaseServerListener::Init() {
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
    CHECK(_listen_channel.Init(_server_addr.c_str(), "rr", &options) == 0) 
        << "Failed to init listen channel.";
    if (!FLAGS_couchbase_bucket_init_string.empty()) {
        InitVBucketMap(FLAGS_couchbase_bucket_init_string);
    }
    CreateListener();
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
}

void CouchbaseServerListener::InitVBucketMap(const std::string& str) {
    butil::VBUCKET_CONFIG_HANDLE vb = 
        butil::vbucket_config_parse_string(str.c_str());
    UpdateVBucketMap(vb);
    if (vb != nullptr) {
        butil::vbucket_config_destroy(vb);
    }
}

void* CouchbaseServerListener::ListenThread(void* arg) {
    CouchbaseServerListener* listener = 
        static_cast<CouchbaseServerListener*>(arg);
    while (true) {
        listener->_reader->Detach();  
        Controller cntl;
        int i = 0;
        for (; i != FLAGS_listener_retry_times; ++i) {
            if (!listener->_auth.empty()) {
                cntl.http_request().SetHeader("Authorization", listener->_auth);
            }
            cntl.http_request().uri() = listener->_url;
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
       
        if (i == FLAGS_listener_retry_times) { 
            if (bthread_usleep(FLAGS_listener_sleep_interval_ms * 1000) < 0) {
                if (errno == ESTOP) {
                    LOG(INFO) << "ListenThread is stopped.";
                    break;
                }
                LOG(ERROR) << "Failed to sleep.";
            }
            continue;
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
        == butil::VBUCKET_DISTRIBUTION_KETAMA) {
        LOG(FATAL) << "Not support ketama distribution.";
        return;
    }

    const CouchbaseChannelMap& channel_map = _cb_channel->GetChannelMap();
    int vb_num = butil::vbucket_config_get_num_vbuckets(vb_conf);
    int replicas_num = butil::vbucket_config_get_num_replicas(vb_conf);
    int server_num = butil::vbucket_config_get_num_servers(vb_conf);
    std::vector<std::vector<int>> vbuckets(vb_num);
    std::vector<std::vector<int>> fvbuckets;
    std::vector<std::string> servers(server_num);
    std::vector<std::string> added_servers;
    std::vector<std::string> removed_servers;
    for (int i = 0; i != vb_num; ++i) {
        vbuckets[i].resize(replicas_num + 1, -1);
        vbuckets[i][0] = butil::vbucket_get_master(vb_conf, i);
        for (int j = 1; j <= replicas_num; ++j) {
            vbuckets[i][j] = butil::vbucket_get_replica(vb_conf, i, j);
        }
    }
    for (int i = 0; i != server_num; ++i) {
        servers[i] = butil::vbucket_config_get_server(vb_conf, i);
        const auto iter = channel_map.find(servers[i]);
        if (iter == channel_map.end()) {
            added_servers.emplace_back(servers[i]);
        }
    }
    _cb_channel->UpdateVBucketServerMap(replicas_num, vbuckets, fvbuckets, 
                                        servers, added_servers, removed_servers);
}

class VBucketContext {
    uint64_t _version = 0;
    int _server_type = 0;
    std::string _forward_master;
    std::string _master;
    std::string _key; 
    policy::MemcacheBinaryCommand _command;
    CouchbaseRequest _request;
    CouchbaseRequest _replicas_req;
};

class CouchbaseDone : public google::protobuf::Closure { 
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
    while(FLAGS_retry_during_rebalance) {
        //TODO: retry in case of rebalance/failover.
        break;
    }
    _done->Run();
}

CouchbaseChannel::CouchbaseChannel() {}

CouchbaseChannel::~CouchbaseChannel() {
    _listener.reset(nullptr);
}

int CouchbaseChannel::Init(const char* server_addr, 
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
    auto ptr = new CouchbaseServerListener(server_addr, this);
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
    do {
        std::string key;
        policy::MemcacheBinaryCommand command;
        // Do not support Flush/Version
        if (req->ParseRequest(&key, &command) != 0) {
            cntl->SetFailed("failed to parse key and command from request");
            break;
        }
        {
            butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
            if(_vbucket_map.Read(&vb_map) != 0) {
                cntl->SetFailed(ENOMEM, "failed to read vbucket map");
                break;
            }
            if (vb_map->_version == 0) {
                cntl->SetFailed(ENODATA, "vbucket map is not initialize");
                break;
            } 
            const size_t vb_index = Hash(key, vb_map->_vbucket.size());
            channel = SelectMasterChannel(vb_map.get(), vb_index);
            if (channel == nullptr) {
                cntl->SetFailed(ENODATA,"failed to get mapped channel");
                break;
            }
            CouchbaseRequest new_req;
            if (!req->BuildNewWithVBucketId(&new_req, vb_index)) {
                cntl->SetFailed("failed to add vbucket id");
                break;
            }
            channel->CallMethod(nullptr, cntl, &new_req, response, done);
        }

        while(FLAGS_retry_during_rebalance) {
            // TODO: retry in case of rebalance/failover
            break;
        }
        return;
    } while (false);
    if (cntl->FailedInline()) {
        if (done) {
            done->Run();
        }
    }
}

Channel* CouchbaseChannel::SelectMasterChannel(
    const VBucketServerMap* vb_map, const size_t vb_index) {
    return GetMappedChannel(GetMaster(vb_map, vb_index), vb_map);
}

const CouchbaseChannelMap& CouchbaseChannel::GetChannelMap() {
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
    if(_vbucket_map.Read(&vbucket_map) != 0) {
        LOG(FATAL) << "Failed to read vbucket map.";
    }
    return vbucket_map->_channel_map;
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

const std::string* CouchbaseChannel::GetMaster(
    const VBucketServerMap* vb_map, const size_t vb_index, int* index) {
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

size_t CouchbaseChannel::Hash(const butil::StringPiece& key, 
                              const size_t vbuckets_num) {
    size_t digest = butil::hash_crc32(key.data(), key.size());
    return digest & (vbuckets_num - 1);
}

bool CouchbaseChannel::UpdateVBucketServerMap(
   const int num_replicas,
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
                              const int num_replicas,
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

} // namespace brpc
