// Copyright (c) 2014 brpc authors.
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

// Authors: Ge,Jun (gejun@baidu.com)

#include <set>
#include <pthread.h>
#include <gflags/gflags.h>
#include "butil/scoped_lock.h"
#include "butil/logging.h"
#include "brpc/log.h"
#include "brpc/socket_map.h"
#include "brpc/details/shared_naming_service.h"

namespace brpc {


typedef butil::FlatMap<std::string, SharedNamingService*> NamingServiceMap;
// Construct on demand to make the code work before main()
static NamingServiceMap* g_ns_map = NULL;
static pthread_mutex_t g_ns_map_mutex = PTHREAD_MUTEX_INITIALIZER;

SharedNamingService::Actions::Actions(SharedNamingService* owner)
    : _owner(owner) {
}

SharedNamingService::Actions::~Actions() {
    if (!IsCleanedUp()) {
        // If Action is not cleaned up, _owner is still a valid pointer.
        // Otherwise *_owner has already been destructed and _owner is a
        // dangling pointer.
        _owner->EndWait(ECANCELED);
    }
}

void SharedNamingService::Actions::CleanUpImp() {
    BAIDU_SCOPED_LOCK(_mutex);
    // Remove all sockets from SocketMap
    for (std::vector<ServerNode>::const_iterator it = _last_servers.begin();
         it != _last_servers.end(); ++it) {
        SocketMapRemove(SocketMapKey(it->addr));
    }
    NamingServiceActions::CleanUpImp();
}

void SharedNamingService::Actions::ResetServers(
        const std::vector<ServerNode>& servers) {
    BAIDU_SCOPED_LOCK(_mutex);
    if (IsCleanedUp()) {
        return;
    }
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
        CHECK_EQ(SocketMapInsert(SocketMapKey(_added[i].addr), &tagged_id.id), 0);
        _added_sockets.push_back(tagged_id);
    }

    _removed_sockets.clear();
    for (size_t i = 0; i < _removed.size(); ++i) {
        ServerNodeWithId tagged_id;
        tagged_id.node = _removed[i];
        CHECK_EQ(0, SocketMapFind(SocketMapKey(_removed[i].addr), &tagged_id.id));
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
        SocketMapRemove(SocketMapKey(_removed[i].addr));
    }

    if (!_removed.empty() || !_added.empty()) {
        LOG(INFO) << _owner->_full_ns << ":" << noflush;
        if (!_added.empty()) {
            LOG(INFO) << " added "<< _added.size() << noflush;
        }
        if (!_removed.empty()) {
            LOG(INFO) << " removed " << _removed.size() << noflush;
        }
        LOG(INFO);
    }

    _owner->EndWait(servers.empty() ? ENODATA : 0);
}

void SharedNamingService::EndWait(int error_code) {
    if (!_has_wait_error.load(butil::memory_order_relaxed) &&
        bthread_id_trylock(_wait_id, NULL) == 0) {
        _wait_error = error_code;
        _has_wait_error.store(true, butil::memory_order_release);
        bthread_id_unlock_and_destroy(_wait_id);
    }
}

SharedNamingService::SharedNamingService()
    : _ns(NULL)
    , _actions(new Actions(this))
    , _wait_id(INVALID_BTHREAD_ID)
    , _has_wait_error(false)
    , _wait_error(0) {
    CHECK_EQ(0, bthread_id_create(&_wait_id, NULL, NULL));
}

SharedNamingService::~SharedNamingService() {
    RPC_VLOG << "~SharedNamingService(" << *this << ')';
    // Remove from g_ns_map first
    if (!_full_ns.empty()) {
        std::unique_lock<pthread_mutex_t> mu(g_ns_map_mutex);
        if (g_ns_map != NULL) {
            SharedNamingService** ptr = g_ns_map->seek(_full_ns);
            if (ptr != NULL && *ptr == this) {
                g_ns_map->erase(_full_ns);
                LOG(INFO) << "erase " << _full_ns;
            }
        }
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

int SharedNamingService::Start(const NamingService* naming_service,
                               const std::string& service_name,
                               const std::string& full_ns,
                               const GetSharedNamingServiceOptions* opt_in) {
    if (naming_service == NULL) {
        LOG(ERROR) << "Param[naming_service] is NULL";
        return -1;
    }
    _ns = naming_service->New();
    _service_name = service_name;
    _full_ns = full_ns;
    if (opt_in) {
        _options = *opt_in;
    }
    _last_sockets.clear();
    _ns->RunNamingService(_service_name.c_str(), _actions);
    return 0;
}

int SharedNamingService::WaitForFirstBatchOfServers() {
    // Wait can happen before signal in which case it returns non-zero,
    // so we ignore return value here and use `_wait_error' instead
    if (!_has_wait_error.load(butil::memory_order_acquire)) {
        bthread_id_join(_wait_id);
    }
    int rc = _wait_error;
    if (rc == ENODATA && _options.succeed_without_server) {
        if (_options.log_succeed_without_server) {
            LOG(WARNING) << '`' << *this << "' is empty! RPC over the channel"
                " will fail until servers appear";
        }
        rc = 0;
    }
    return rc;
}

void SharedNamingService::ServerNodeWithId2ServerId(
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

int SharedNamingService::AddWatcher(NamingServiceWatcher* watcher,
                                    const NamingServiceFilter* filter) {
    if (watcher == NULL) {
        LOG(ERROR) << "Param[watcher] is NULL";
        return -1;
    }
    BAIDU_SCOPED_LOCK(_mutex);
    if (_watchers.insert(std::make_pair(watcher, filter)).second) {
        if (!_last_sockets.empty()) {
            std::vector<ServerId> added_ids;
            ServerNodeWithId2ServerId(_last_sockets, &added_ids, filter);
            watcher->OnAddedServers(added_ids);
        }
        return 0;
    }
    return -1;
}
    
int SharedNamingService::RemoveWatcher(NamingServiceWatcher* watcher) {
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

int GetSharedNamingService(
    butil::intrusive_ptr<SharedNamingService>* nsp_out,
    const char* url,
    const GetSharedNamingServiceOptions* options) {
    char protocol[MAX_PROTOCOL_LEN + 1];
    const char* const service_name = ParseNamingServiceUrl(url, protocol);
    if (service_name == NULL) {
        LOG(ERROR) << "Invalid naming service=" << url;
        return -1;
    }
    const NamingService* ns = NamingServiceExtension()->Find(protocol);
    if (ns == NULL) {
        LOG(ERROR) << "Unknown naming service=" << protocol;
        return -1;
    }

    // full_ns is normalized comparing to `url'.
    std::string full_ns;
    const size_t prot_len = strlen(protocol);
    full_ns.reserve(prot_len + 3 + strlen(service_name));
    for (size_t i = 0; i < prot_len; ++i) {
        full_ns.push_back(::tolower(protocol[i]));
    }
    full_ns.append("://");
    full_ns.append(service_name);

    bool new_ns = false;
    butil::intrusive_ptr<SharedNamingService> nsp;
    {
        std::unique_lock<pthread_mutex_t> mu(g_ns_map_mutex);
        if (g_ns_map == NULL) {
            g_ns_map = new (std::nothrow) NamingServiceMap;
            if (NULL == g_ns_map) {
                mu.unlock();
                LOG(ERROR) << "Fail to new g_ns_map";
                return -1;
            }
            if (g_ns_map->init(64) != 0) {
                mu.unlock();
                LOG(ERROR) << "Fail to init g_ns_map";
                return -1;
            }
        }
        SharedNamingService*& ptr = (*g_ns_map)[full_ns];
        if (ptr != NULL) {
            if (ptr->AddRefManually() == 0) {
                // The NS's last intrusive_ptr was just destructed and the
                // removal-from-global-map-code in ptr->~SharedNamingService()
                // is about to run or already running, need to create another NS.
                // Notice that we don't need to remove the reference because
                // the object is already destructing.
                ptr = NULL;
            } else {
                nsp.reset(ptr, false);
            }
        }
        if (ptr == NULL) {
            SharedNamingService* thr = new (std::nothrow) SharedNamingService;
            if (thr == NULL) {
                mu.unlock();
                LOG(ERROR) << "Fail to new SharedNamingService";
                return -1;
            }
            ptr = thr;
            nsp.reset(ptr);
            new_ns = true;
        }
    }
    if (new_ns) {
        if (nsp->Start(ns, service_name, full_ns, options) != 0) {
            LOG(ERROR) << "Fail to start SharedNamingService";
            return -1;
        }
    }
    const int rc = nsp->WaitForFirstBatchOfServers();
    if (rc) {
        LOG(ERROR) << "Fail to WaitForFirstBatchOfServers: " << berror(rc);
        return -1;
    }
    nsp_out->swap(nsp);
    return 0;
}

void SharedNamingService::Describe(std::ostream& os,
                                   const DescribeOptions& options) const {
    if (_ns == NULL) {
        os << "null";
    } else {
        _ns->Describe(os, options);
    }
    os << "://" << _service_name;
}

std::ostream& operator<<(std::ostream& os, const SharedNamingService& nsthr) {
    nsthr.Describe(os, DescribeOptions());
    return os;
}


} // namespace brpc
