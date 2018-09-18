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

// Authors: Cai,Daojin (caidaojin@qiyi.com)


#include "brpc/channel.h"
#include "brpc/log.h"
#include "brpc/policy/couchbase_listener_naming_service.h"
#include "brpc/policy/couchbase_naming_service.h"
#include "brpc/policy/list_naming_service.h"
#include "brpc/progressive_reader.h"
#include "bthread/bthread.h"
#include "butil/status.h"
#include "butil/string_splitter.h"
#include "butil/strings/string_number_conversions.h"
#include "butil/third_party/libvbucket/vbucket.h"

namespace brpc {
namespace policy {

DEFINE_int32(couchbase_listen_retry_times, 5,
             "Retry times to create couchbase vbucket map monitoring connection."
             "Listen thread will sleep a while when reach this times.");
DEFINE_int32(couchbase_listen_interval_ms, 1000, 
             "Listen thread sleep for the number of milliseconds after creating"
             "vbucket map monitoring connection failure.");

namespace {

// Each vbucket map json string is spiltted by "\n\n\n\n".
const std::string kSeparator("\n\n\n\n");

}

class CouchbaseServerListener;

class VBucketMapReader : public brpc::ProgressiveReader {
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
    CouchbaseServerListener(
        const butil::StringPiece& server_list, const butil::StringPiece& streaming_url, 
        const butil::StringPiece& init_url, const butil::StringPiece& auth, CouchbaseNamingService* ns)
        : _streaming_url(streaming_url.data(), streaming_url.length()),
          _auth(auth.data(), auth.length()),
          _cbns(ns),
          _ns(nullptr),
          _reader(new VBucketMapReader(this)) {
        Init(server_list, init_url);
    }
    ~CouchbaseServerListener();
   
    void UpdateVBucketMap(std::string&& vb_map);
    
    void CreateListener();
    
private:
    CouchbaseServerListener(const CouchbaseServerListener&) = delete;
    CouchbaseServerListener& operator=(const CouchbaseServerListener&) = delete;

    void Init(const butil::StringPiece& server_list, const  butil::StringPiece& init_url);

    bool InitVBucketMap(const std::string& url);

    static void* ListenThread(void* arg);

    bthread_t _listen_bth;
    // REST/JSON url to monitor vbucket map.
    std::string _streaming_url;
    std::string _auth;
    std::string _listen_port;
    CouchbaseNamingService* _cbns;
    // List naming service is used by '_listen_channel'.
    ListNamingService* _ns;
    // Monitor couchbase vbuckets map on this channel. 
    brpc::Channel _listen_channel;
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
    const size_t end_pos = _buf.rfind(kSeparator);
    if (end_pos != std::string::npos) {
        size_t begin_pos = _buf.rfind(kSeparator, end_pos - 1);
        if (begin_pos == std::string::npos) {
            begin_pos = 0;
        } else {
            begin_pos += kSeparator.size();
        }
        std::string vb_map = _buf.substr(begin_pos, end_pos);
        _buf = _buf.substr(end_pos + kSeparator.size());
        _listener->UpdateVBucketMap(std::move(vb_map));
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
}

void CouchbaseServerListener::Init(const butil::StringPiece& server_list, 
                                   const butil::StringPiece& init_url) {
    brpc::ChannelOptions options;
    options.protocol = PROTOCOL_HTTP;
    options.max_retry = FLAGS_couchbase_listen_retry_times;
    _ns = new ListNamingService();
    if (_ns == nullptr) {
        LOG(FATAL) << "Fail to new list naming service.";
        return;
    }
    std::string servers(server_list.data(), server_list.length());
    _ns->AllowUpdate();
    _ns->UpdateServerList(&servers);
    CHECK(_listen_channel.Init(_ns, "rr", &options) == 0) 
        << "Failed to init listen channel.";
    const std::string init_url_str(init_url.data(), init_url.length());
    InitVBucketMap(init_url_str);
    CreateListener();
}
 
bool CouchbaseServerListener::InitVBucketMap(const std::string& uri) {
    Controller cntl;
    if (!_auth.empty()) {
        cntl.http_request().SetHeader("Authorization", _auth);
    }
    cntl.http_request().uri() = uri;
    _listen_channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    if (!cntl.Failed()) {
        std::string vb_map = cntl.response_attachment().to_string();
        if (!vb_map.empty()) {
            _listen_port = butil::IntToString(cntl.remote_side().port);
            UpdateVBucketMap(std::move(vb_map));
            return true;
        }
    }
    LOG(ERROR) << "Failed to init vbucket map: " << cntl.ErrorText();
    // Set empty for first batch of server.
    std::vector<std::string> empty_servers;
    std::string empty_vb;
    _cbns->ResetServers(empty_servers, empty_vb);
    return false;
}

void* CouchbaseServerListener::ListenThread(void* arg) {
    CouchbaseServerListener* listener = 
        static_cast<CouchbaseServerListener*>(arg);
    while (true) {
        listener->_reader->Detach();  
        Controller cntl;
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
    CHECK(bthread_start_urgent(
        &_listen_bth, nullptr, ListenThread, this) == 0)
        << "Failed to start listen thread.";  
}

void CouchbaseServerListener::UpdateVBucketMap(std::string&& vb_map) { 
    butil::VBUCKET_CONFIG_HANDLE vb = 
		butil::vbucket_brief_parse_string(vb_map.c_str());
    if (vb != nullptr) {
        const size_t server_num = butil::vbucket_config_get_num_servers(vb);
        std::vector<std::string> servers(server_num);
        for (size_t i = 0; i != server_num; ++i) {
            servers[i] = butil::vbucket_config_get_server(vb, i);
        }
        butil::vbucket_config_destroy(vb);
        // Update new server list for '_listen_channel'.
        std::string server_list;
        for (const auto& server : servers) {
            const size_t pos = server.find(':');
            server_list.append(server.data(), pos);
            server_list += ":" + _listen_port + ",";
        }
        server_list.pop_back();
        _ns->UpdateServerList(&server_list);		
        _cbns->ResetServers(servers, vb_map);
    } else {
        LOG(ERROR) << "Failed to get VBUCKET_CONFIG_HANDLE from string:\n" 
                   << "\'" << vb_map << "\'.";
    }
}

CouchbaseNamingService::CouchbaseNamingService() : _actions(nullptr) {}

CouchbaseNamingService::~CouchbaseNamingService() {}

int CouchbaseNamingService::ResetServers(const std::vector<std::string>& servers, 
                                         std::string& vb_map) {
    if (_actions) {
        std::vector<brpc::ServerNode> server_node;
        // The server_node[0] is a fake server. We only use server_node[0].tag 
        // to bring the vbucket map json string. 
        server_node.emplace_back();
        server_node[0].tag.swap(vb_map);
        for (const std::string& server : servers) {
            butil::EndPoint point;
            if (butil::str2endpoint(server.c_str(), &point) != 0 &&
                butil::hostname2endpoint(server.c_str(), &point) != 0) {
                LOG(ERROR) << "Invalid address=`" << server << '\'';
                continue;
            }
            server_node.emplace_back(point);
        }
        _actions->ResetServers(server_node);
    }
    return 0;
}

int CouchbaseNamingService::RunNamingService(const char* service_name,
                                             NamingServiceActions* actions) {
    butil::StringPiece server_list, streaming_url, init_url, auth;
    CHECK(ParseNsUrl(service_name, server_list, streaming_url, init_url, auth))
        << "Failed to parse couchbase naming url: " << service_name;
    _service_name = service_name;
    // '_actions' MUST init before '_listener' due to it will be used by '_listener'. 
    _actions = actions;
    _listener.reset(new CouchbaseServerListener(server_list, streaming_url, 
                                                init_url, auth, this));
    return 0;
}

void CouchbaseNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "couchbase_channel";
    return;
}

NamingService* CouchbaseNamingService::New() const {
    return new CouchbaseNamingService;
}

void CouchbaseNamingService::Destroy() {
    _listener.reset(nullptr);
    delete this;
}

std::string CouchbaseNamingService::BuildNsUrl(
    const char* servers_addr, const std::string& streaming_url, 
    const std::string& init_url, const std::string& auth, const std::string& unique_id) {
    std::string ns_url("couchbase_channel://");
    ns_url.append(servers_addr);
    ns_url.append("_streaming@" + streaming_url + "_init@" + init_url);
    if (!auth.empty()) {
        ns_url.append("_auth@" + auth);
    }
    ns_url.append("_unique@" + unique_id);
    return std::move(ns_url);
}

bool CouchbaseNamingService::ParseNsUrl(
    const butil::StringPiece service_full_name, butil::StringPiece& server_list, 
    butil::StringPiece& streaming_url, butil::StringPiece& init_url, butil::StringPiece& auth) {
    size_t pos = service_full_name.rfind("_unique@");
    butil::StringPiece service_name = service_full_name.substr(0, pos);
    butil::StringPiece stream_prefix("_streaming@");
    size_t stream_pos = service_name.find(stream_prefix);
    if (stream_pos == service_name.npos) {
        return false;
    }
    server_list = service_name.substr(0, stream_pos);
    stream_pos += stream_prefix.length();
    butil::StringPiece init_prefix("_init@");
    size_t init_pos = service_name.find(init_prefix, stream_pos);
    if (init_pos == service_name.npos) {
        return false;
    }
    streaming_url = service_name.substr(stream_pos, init_pos - stream_pos);
    init_pos += init_prefix.length();
	
    butil::StringPiece auth_prefix("_auth@");
    size_t auth_pos = service_name.find(auth_prefix, init_pos);
    if (auth_pos == service_name.npos) {
        auth.clear();
        init_url = service_name.substr(init_pos);		
    } else {
        init_url = service_name.substr(init_pos, auth_pos - init_pos);
        auth = service_name.substr(auth_pos + auth_prefix.length());
    }
    return true;
}


}  // namespace policy
} // namespace brpc
