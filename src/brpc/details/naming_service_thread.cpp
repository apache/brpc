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


#include <set>
#include <pthread.h>
#include <gflags/gflags.h>
#include "bthread/butex.h"
#include "butil/scoped_lock.h"
#include "butil/logging.h"
#include "brpc/log.h"
#include "brpc/socket_map.h"
#include "brpc/details/naming_service_thread.h"


namespace brpc {

struct NSKey {
    std::string protocol;
    std::string service_name;
    ChannelSignature channel_signature;

    NSKey(const std::string& prot_in,
          const std::string& service_in,
          const ChannelSignature& sig)
        : protocol(prot_in), service_name(service_in), channel_signature(sig) {
    }
};
struct NSKeyHasher {
    size_t operator()(const NSKey& nskey) const {
        size_t h = butil::DefaultHasher<std::string>()(nskey.protocol);
        h = h * 101 + butil::DefaultHasher<std::string>()(nskey.service_name);
        h = h * 101 + nskey.channel_signature.data[1];
        return h;
    }
};
inline bool operator==(const NSKey& k1, const NSKey& k2) {
    return k1.protocol == k2.protocol &&
        k1.service_name == k2.service_name &&
        k1.channel_signature == k2.channel_signature;
}

typedef butil::FlatMap<NSKey, NamingServiceThread*, NSKeyHasher> NamingServiceMap;
// Construct on demand to make the code work before main()
static NamingServiceMap* g_nsthread_map = NULL;
static pthread_mutex_t g_nsthread_map_mutex = PTHREAD_MUTEX_INITIALIZER;

NamingServiceThread::Actions::Actions(NamingServiceThread* owner)
    : _owner(owner)
    , _wait_id(INVALID_BTHREAD_ID)
    , _has_wait_error(false)
    , _wait_error(0) {
    CHECK_EQ(0, bthread_id_create(&_wait_id, NULL, NULL));
}

NamingServiceThread::Actions::~Actions() {
    // Remove all sockets from SocketMap
    for (std::vector<ServerNode>::const_iterator it = _last_servers.begin();
         it != _last_servers.end(); ++it) {
        const SocketMapKey key(*it, _owner->_options.channel_signature);
        SocketMapRemove(key);
    }
    EndWait(0);
}

void NamingServiceThread::Actions::AddServers(
    const std::vector<ServerNode>&) {
    // FIXME(gejun)
    abort();
}

void NamingServiceThread::Actions::RemoveServers(
    const std::vector<ServerNode>&) {
    // FIXME(gejun)
    abort();
}

void NamingServiceThread::Actions::ResetServers(
        const std::vector<ServerNode>& servers) {
    _servers.assign(servers.begin(), servers.end());
    
    // Diff servers with _last_servers by comparing sorted vectors.
    // Notice that _last_servers is always sorted.
    std::sort(_servers.begin(), _servers.end());
    const size_t dedup_size = std::unique(_servers.begin(), _servers.end())
        - _servers.begin();
    if (dedup_size != _servers.size()) {
        LOG(WARNING) << "Removed " << _servers.size() - dedup_size
                     << " duplicated servers";
        _servers.resize(dedup_size);
    }
    _added.resize(_servers.size());
    std::vector<ServerNode>::iterator _added_end = 
        std::set_difference(_servers.begin(), _servers.end(),
                            _last_servers.begin(), _last_servers.end(),
                            _added.begin());
    _added.resize(_added_end - _added.begin());

    _removed.resize(_last_servers.size());
    std::vector<ServerNode>::iterator _removed_end = 
        std::set_difference(_last_servers.begin(), _last_servers.end(),
                            _servers.begin(), _servers.end(),
                            _removed.begin());
    _removed.resize(_removed_end - _removed.begin());

    _added_sockets.clear();
    for (size_t i = 0; i < _added.size(); ++i) {
        ServerNodeWithId tagged_id;
        tagged_id.node = _added[i];
        // TODO: For each unique SocketMapKey (i.e. SSL settings), insert a new
        //       Socket. SocketMapKey may be passed through AddWatcher. Make sure
        //       to pick those Sockets with the right settings during OnAddedServers
        const SocketMapKey key(_added[i], _owner->_options.channel_signature);
        CHECK_EQ(0, SocketMapInsert(key, &tagged_id.id, _owner->_options.ssl_ctx));
        _added_sockets.push_back(tagged_id);
    }

    _removed_sockets.clear();
    for (size_t i = 0; i < _removed.size(); ++i) {
        ServerNodeWithId tagged_id;
        tagged_id.node = _removed[i];
        const SocketMapKey key(_removed[i], _owner->_options.channel_signature);
        CHECK_EQ(0, SocketMapFind(key, &tagged_id.id));
        _removed_sockets.push_back(tagged_id);
    }

    // Refresh sockets
    if (_removed_sockets.empty()) {
        _sockets = _owner->_last_sockets;
    } else {
        std::sort(_removed_sockets.begin(), _removed_sockets.end());
        _sockets.resize(_owner->_last_sockets.size());
        std::vector<ServerNodeWithId>::iterator _sockets_end =
            std::set_difference(
                _owner->_last_sockets.begin(), _owner->_last_sockets.end(),
                _removed_sockets.begin(), _removed_sockets.end(),
                _sockets.begin());
        _sockets.resize(_sockets_end - _sockets.begin());
    }
    if (!_added_sockets.empty()) {
        const size_t before_added = _sockets.size();
        std::sort(_added_sockets.begin(), _added_sockets.end());
        _sockets.insert(_sockets.end(),
                       _added_sockets.begin(), _added_sockets.end());
        std::inplace_merge(_sockets.begin(), _sockets.begin() + before_added,
                           _sockets.end());
    }
    std::vector<ServerId> removed_ids;
    ServerNodeWithId2ServerId(_removed_sockets, &removed_ids, NULL);

    {
        BAIDU_SCOPED_LOCK(_owner->_mutex);
        _last_servers.swap(_servers);
        _owner->_last_sockets.swap(_sockets);
        for (std::map<NamingServiceWatcher*,
                      const NamingServiceFilter*>::iterator
                 it = _owner->_watchers.begin();
             it != _owner->_watchers.end(); ++it) {
            if (!_removed_sockets.empty()) {
                it->first->OnRemovedServers(removed_ids);
            }

            std::vector<ServerId> added_ids;
            ServerNodeWithId2ServerId(_added_sockets, &added_ids, it->second);
            if (!_added_sockets.empty()) {
                it->first->OnAddedServers(added_ids);
            }
        }
    }

    for (size_t i = 0; i < _removed.size(); ++i) {
        // TODO: Remove all Sockets that have the same address in SocketMapKey.peer
        //       We may need another data structure to avoid linear cost
        const SocketMapKey key(_removed[i], _owner->_options.channel_signature);
        SocketMapRemove(key);
    }

    if (!_removed.empty() || !_added.empty()) {
        std::ostringstream info;
        info << butil::class_name_str(*_owner->_ns) << "(\"" 
             << _owner->_service_name << "\"):";
        if (!_added.empty()) {
            info << " added "<< _added.size();
        }
        if (!_removed.empty()) {
            info << " removed " << _removed.size();
        }
        LOG(INFO) << info.str();
    }

    EndWait(servers.empty() ? ENODATA : 0);
}

void NamingServiceThread::Actions::EndWait(int error_code) {
    if (bthread_id_trylock(_wait_id, NULL) == 0) {
        _wait_error = error_code;
        _has_wait_error.store(true, butil::memory_order_release);
        bthread_id_unlock_and_destroy(_wait_id);
    }
}

int NamingServiceThread::Actions::WaitForFirstBatchOfServers() {
    // Wait can happen before signal in which case it returns non-zero,
    // so we ignore return value here and use `_wait_error' instead
    if (!_has_wait_error.load(butil::memory_order_acquire)) {
        bthread_id_join(_wait_id);
    }
    return _wait_error;
}

NamingServiceThread::NamingServiceThread()
    : _tid(0)
    , _ns(NULL)
    , _actions(this) {
}

NamingServiceThread::~NamingServiceThread() {
    RPC_VLOG << "~NamingServiceThread(" << *this << ')';
    // Remove from g_nsthread_map first
    if (!_protocol.empty()) {
        const NSKey key(_protocol, _service_name, _options.channel_signature);
        std::unique_lock<pthread_mutex_t> mu(g_nsthread_map_mutex);
        if (g_nsthread_map != NULL) {
            NamingServiceThread** ptr = g_nsthread_map->seek(key);
            if (ptr != NULL && *ptr == this) {
                g_nsthread_map->erase(key);
            }
        }
    }
    if (_tid) {
        bthread_stop(_tid);
        bthread_join(_tid, NULL);
        _tid = 0;
    }
    {
        BAIDU_SCOPED_LOCK(_mutex);
        std::vector<ServerId> to_be_removed;
        ServerNodeWithId2ServerId(_last_sockets, &to_be_removed, NULL);
        if (!_last_sockets.empty()) {
            for (std::map<NamingServiceWatcher*,
                          const NamingServiceFilter*>::iterator
                     it = _watchers.begin(); it != _watchers.end(); ++it) {
                it->first->OnRemovedServers(to_be_removed);
            }
        }
        _watchers.clear();
    }

    if (_ns) {
        _ns->Destroy();
        _ns = NULL;
    }
}

void* NamingServiceThread::RunThis(void* arg) {
    static_cast<NamingServiceThread*>(arg)->Run();
    return NULL;
}

int NamingServiceThread::Start(NamingService* naming_service,
                               const std::string& protocol,
                               const std::string& service_name,
                               const GetNamingServiceThreadOptions* opt_in) {
    if (naming_service == NULL) {
        LOG(ERROR) << "Param[naming_service] is NULL";
        return -1;
    }
    _ns = naming_service;
    _protocol = protocol;
    _service_name = service_name;
    if (opt_in) {
        _options = *opt_in;
    }
    _last_sockets.clear();
    if (_ns->RunNamingServiceReturnsQuickly()) {
        RunThis(this);
    } else {
        int rc = bthread_start_urgent(&_tid, NULL, RunThis, this);
        if (rc) {
            LOG(ERROR) << "Fail to create bthread: " << berror(rc);
            return -1;
        }
    }
    return WaitForFirstBatchOfServers();
}

int NamingServiceThread::WaitForFirstBatchOfServers() {
    int rc = _actions.WaitForFirstBatchOfServers();
    if (rc == ENODATA && _options.succeed_without_server) {
        if (_options.log_succeed_without_server) {
            LOG(WARNING) << '`' << *this << "' is empty! RPC over the channel"
                " will fail until servers appear";
        }
        rc = 0;
    }
    if (rc) {
        LOG(ERROR) << "Fail to WaitForFirstBatchOfServers: " << berror(rc);
        return -1;
    }
    return 0;
}

void NamingServiceThread::ServerNodeWithId2ServerId(
    const std::vector<ServerNodeWithId>& src,
    std::vector<ServerId>* dst, const NamingServiceFilter* filter) {
    dst->reserve(src.size());
    for (std::vector<ServerNodeWithId>::const_iterator
             it = src.begin(); it != src.end(); ++it) {
        if (filter && !filter->Accept(it->node)) {
            continue;
        }
        ServerId socket;
        socket.id = it->id;
        socket.tag = it->node.tag;
        dst->push_back(socket);
    }
}

int NamingServiceThread::AddWatcher(NamingServiceWatcher* watcher,
                                    const NamingServiceFilter* filter) {
    if (watcher == NULL) {
        LOG(ERROR) << "Param[watcher] is NULL";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_mutex);
    if (_watchers.emplace(watcher, filter).second) {
        if (!_last_sockets.empty()) {
            std::vector<ServerId> added_ids;
            ServerNodeWithId2ServerId(_last_sockets, &added_ids, filter);
            watcher->OnAddedServers(added_ids);
        }
        return 0;
    }
    return -1;
}
    
int NamingServiceThread::RemoveWatcher(NamingServiceWatcher* watcher) {
    if (watcher == NULL) {
        LOG(ERROR) << "Param[watcher] is NULL";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_mutex);
    if (_watchers.erase(watcher)) {
        // Not call OnRemovedServers of the watcher because watcher can
        // remove the sockets by itself and in most cases, removing
        // sockets is useless.
        return 0;
    }
    return -1;
}

void NamingServiceThread::Run() {
    int rc = _ns->RunNamingService(_service_name.c_str(), &_actions);
    if (rc != 0) {
        LOG(WARNING) << "Fail to run naming service: " << berror(rc);
        if (rc == ENODATA) {
            LOG(ERROR) << "RunNamingService should not return ENODATA, "
                "change it to ESTOP";
            rc = ESTOP;
        }
        _actions.EndWait(rc);
    }

    // Don't remove servers here which may still be used by watchers:
    // A stop-updating naming service does not mean that it's not needed
    // anymore. Remove servers inside dtor.
}

static const size_t MAX_PROTOCOL_LEN = 31;

static const char* ParseNamingServiceUrl(const char* url, char* protocol) {
    // Accepting "[^:]{1,MAX_PROTOCOL_LEN}://*.*"
    //            ^^^^^^^^^^^^^^^^^^^^^^^^   ^^^
    //            protocol                service_name
    if (__builtin_expect(url != NULL, 1)) {
        const char* p1 = url;
        while (*p1 != ':') {
            if (p1 < url + MAX_PROTOCOL_LEN && *p1) {
                protocol[p1 - url] = *p1;
                ++p1;
            } else {
                return NULL;
            }
        }
        if (p1 <= url + MAX_PROTOCOL_LEN) {
            protocol[p1 - url] = '\0';
            const char* p2 = p1;
            if (*++p2 == '/' && *++p2 == '/') {
                return p2 + 1;
            }
        }
    }
    return NULL;
}

int GetNamingServiceThread(
    butil::intrusive_ptr<NamingServiceThread>* nsthread_out,
    const char* url,
    const GetNamingServiceThreadOptions* options) {
    char protocol[MAX_PROTOCOL_LEN + 1];
    const char* const service_name = ParseNamingServiceUrl(url, protocol);
    if (service_name == NULL) {
        LOG(ERROR) << "Invalid naming service url=" << url;
        return -1;
    }
    const NamingService* source_ns = NamingServiceExtension()->Find(protocol);
    if (source_ns == NULL) {
        LOG(ERROR) << "Unknown protocol=" << protocol;
        return -1;
    }
    const NSKey key(protocol, service_name,
                    (options ? options->channel_signature : ChannelSignature()));
    bool new_thread = false;
    butil::intrusive_ptr<NamingServiceThread> nsthread;
    {
        std::unique_lock<pthread_mutex_t> mu(g_nsthread_map_mutex);
        if (g_nsthread_map == NULL) {
            g_nsthread_map = new (std::nothrow) NamingServiceMap;
            if (NULL == g_nsthread_map) {
                mu.unlock();
                LOG(ERROR) << "Fail to new g_nsthread_map";
                return -1;
            }
            if (g_nsthread_map->init(64) != 0) {
                mu.unlock();
                LOG(ERROR) << "Fail to init g_nsthread_map";
                return -1;
            }
        }
        NamingServiceThread*& ptr = (*g_nsthread_map)[key];
        if (ptr != NULL) {
            if (ptr->AddRefManually() == 0) {
                // The ns thread's last intrusive_ptr was just destructed and
                // the removal-from-global-map-code in ptr->~NamingServideThread()
                // is about to run or already running, need to create another ns
                // thread.
                // Notice that we don't need to remove the reference because
                // the object is already destructing.
                ptr = NULL;
            } else {
                nsthread.reset(ptr, false);
            }
        }
        if (ptr == NULL) {
            NamingServiceThread* thr = new (std::nothrow) NamingServiceThread;
            if (thr == NULL) {
                mu.unlock();
                LOG(ERROR) << "Fail to new NamingServiceThread";
                return -1;
            }
            ptr = thr;
            nsthread.reset(ptr);
            new_thread = true;
        }
    }
    if (new_thread) {
        if (nsthread->Start(source_ns->New(), key.protocol, key.service_name, options) != 0) {
            LOG(ERROR) << "Fail to start NamingServiceThread";
            std::unique_lock<pthread_mutex_t> mu(g_nsthread_map_mutex);
            g_nsthread_map->erase(key);
            return -1;
        }
    } else {
        if (nsthread->WaitForFirstBatchOfServers() != 0) {
            return -1;
        }
    }
    nsthread_out->swap(nsthread);
    return 0;
}

void NamingServiceThread::Describe(std::ostream& os,
                                   const DescribeOptions& options) const {
    if (_ns == NULL) {
        os << "null";
    } else {
        _ns->Describe(os, options);
    }
    os << "://" << _service_name;
}

std::ostream& operator<<(std::ostream& os, const NamingServiceThread& nsthr) {
    nsthr.Describe(os, DescribeOptions());
    return os;
}

} // namespace brpc
