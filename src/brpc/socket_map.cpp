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

// Authors: Ge,Jun (gejun@baidu.com)
//          Rujie Jiang (jiangrujie@baidu.com)

#include <gflags/gflags.h>
#include <map>
#include "bthread/bthread.h"
#include "butil/time.h"
#include "butil/scoped_lock.h"
#include "butil/logging.h"
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "brpc/log.h"
#include "brpc/protocol.h"
#include "brpc/input_messenger.h"
#include "brpc/reloadable_flags.h"
#include "brpc/socket_map.h"
#include "brpc/details/ssl_helper.h"        // CreateClientSSLContext


namespace brpc {

DEFINE_int32(health_check_interval, 3, 
             "seconds between consecutive health-checkings");
// NOTE: Must be limited to positive to guarantee correctness of SocketMapRemove.
BRPC_VALIDATE_GFLAG(health_check_interval, PositiveInteger);

DEFINE_int32(idle_timeout_second, 10, 
             "Pooled connections without data transmission for so many "
             "seconds will be closed. No effect for non-positive values");
BRPC_VALIDATE_GFLAG(idle_timeout_second, PassValidate);

DEFINE_int32(defer_close_second, 0,
             "Defer close of connections for so many seconds even if the"
             " connection is not used by anyone. Close immediately for "
             "non-positive values.");
BRPC_VALIDATE_GFLAG(defer_close_second, PassValidate);

DEFINE_bool(show_socketmap_in_vars, false,
            "[DEBUG] Describe SocketMaps in /vars");
BRPC_VALIDATE_GFLAG(show_socketmap_in_vars, PassValidate);

static pthread_once_t g_socket_map_init = PTHREAD_ONCE_INIT;
static butil::static_atomic<SocketMap*> g_socket_map = BUTIL_STATIC_ATOMIC_INIT(NULL);

class GlobalSocketCreator : public SocketCreator {
public:
    int CreateSocket(const SocketOptions& opt, SocketId* id) {
        SocketOptions sock_opt = opt;
        sock_opt.health_check_interval_s = FLAGS_health_check_interval;
        return get_client_side_messenger()->Create(sock_opt, id);
    }
};

static void CreateClientSideSocketMap() {
    SocketMap* socket_map = new SocketMap;
    SocketMapOptions options;
    options.socket_creator = new GlobalSocketCreator;
    options.idle_timeout_second_dynamic = &FLAGS_idle_timeout_second;
    options.defer_close_second_dynamic = &FLAGS_defer_close_second;
    if (socket_map->Init(options) != 0) {
        LOG(FATAL) << "Fail to init SocketMap";
        exit(1);
    }
    g_socket_map.store(socket_map, butil::memory_order_release);
}

SocketMap* get_client_side_socket_map() {
    // The consume fence makes sure that we see a NULL or a fully initialized
    // SocketMap.
    return g_socket_map.load(butil::memory_order_consume);
}
SocketMap* get_or_new_client_side_socket_map() {
    get_or_new_client_side_messenger();
    pthread_once(&g_socket_map_init, CreateClientSideSocketMap);
    return g_socket_map.load(butil::memory_order_consume);
}

void ComputeSocketMapKeyChecksum(const SocketMapKey& key,
                                 unsigned char* checksum) {
    butil::MurmurHash3_x64_128_Context mm_ctx;
    butil::MurmurHash3_x64_128_Init(&mm_ctx, 0);

    const int BUFSIZE = 1024;        // Should be enough
    char buf[BUFSIZE];
    int cur_len = 0;

#define SAFE_MEMCOPY(dst, cur_len, src, size)                   \
    do {                                                        \
        int copy_len = std::min((int)size, BUFSIZE - cur_len);  \
        if (copy_len > 0) {                                     \
            memcpy(dst + cur_len, src, copy_len);               \
            cur_len += copy_len;                                \
        }                                                       \
    } while (0);

    std::size_t ephash = butil::DefaultHasher<butil::EndPoint>()(key.peer);
    SAFE_MEMCOPY(buf, cur_len, &ephash, sizeof(ephash));
    SAFE_MEMCOPY(buf, cur_len, &key.auth, sizeof(key.auth));

    const ChannelSSLOptions& ssl = key.ssl_options;
    SAFE_MEMCOPY(buf, cur_len, &ssl.enable, sizeof(ssl.enable));
    if (ssl.enable) {
        SAFE_MEMCOPY(buf, cur_len, ssl.ciphers.data(), ssl.ciphers.size());
        SAFE_MEMCOPY(buf, cur_len, ssl.protocols.data(), ssl.protocols.size());
        SAFE_MEMCOPY(buf, cur_len, ssl.sni_name.data(), ssl.sni_name.size());

        const VerifyOptions& verify = ssl.verify;
        SAFE_MEMCOPY(buf, cur_len, &verify.verify_depth,
                     sizeof(verify.verify_depth));
        if (verify.verify_depth > 0) {
            SAFE_MEMCOPY(buf, cur_len, verify.ca_file_path.data(),
                         verify.ca_file_path.size());
        }
    } else {
        // All disabled ChannelSSLOptions are the same
    }
#undef SAFE_MEMCOPY

    butil::MurmurHash3_x64_128_Update(&mm_ctx, buf, cur_len);
    const CertInfo& cert = ssl.client_cert;
    if (ssl.enable && !cert.certificate.empty()) {
        // Certificate may be too long (PEM string) to fit into `buf'
        butil::MurmurHash3_x64_128_Update(
            &mm_ctx, cert.certificate.data(), cert.certificate.size());
        butil::MurmurHash3_x64_128_Update(
            &mm_ctx, cert.private_key.data(), cert.private_key.size());
        // sni_filters has no effect in ChannelSSLOptions
    }
    butil::MurmurHash3_x64_128_Final(checksum, &mm_ctx);
}

int SocketMapInsert(const SocketMapKey& key, SocketId* id) {
    return get_or_new_client_side_socket_map()->Insert(key, id);
}    

int SocketMapFind(const SocketMapKey& key, SocketId* id) {
    SocketMap* m = get_client_side_socket_map();
    if (m) {
        return m->Find(key, id);
    }
    return -1;
}

void SocketMapRemove(const SocketMapKey& key) {
    SocketMap* m = get_client_side_socket_map();
    if (m) {
        // TODO: We don't have expected_id to pass right now since the callsite
        // at NamingServiceThread is hard to be fixed right now. As long as
        // FLAGS_health_check_interval is limited to positive, SocketMapInsert
        // never replaces the sockets, skipping comparison is still right.
        m->Remove(key, (SocketId)-1);
    }
}

void SocketMapList(std::vector<SocketId>* ids) {
    SocketMap* m = get_client_side_socket_map();
    if (m) {
        m->List(ids);
    }
}

// ========== SocketMap impl. ============

SocketMapOptions::SocketMapOptions()
    : socket_creator(NULL)
    , suggested_map_size(1024)
    , idle_timeout_second_dynamic(NULL)
    , idle_timeout_second(0)
    , defer_close_second_dynamic(NULL)
    , defer_close_second(0) {
}

SocketMap::SocketMap()
    : _exposed_in_bvar(false)
    , _this_map_bvar(NULL)
    , _has_close_idle_thread(false) {
}

SocketMap::~SocketMap() {
    RPC_VLOG << "Destroying SocketMap=" << this;
    if (_has_close_idle_thread) {
        bthread_stop(_close_idle_thread);
        bthread_join(_close_idle_thread, NULL);
    }
    if (!_map.empty()) {
        std::ostringstream err;
        int nleft = 0;
        for (Map::iterator it = _map.begin(); it != _map.end(); ++it) {
            SingleConnection* sc = &it->second;
            if ((!sc->socket->Failed() ||
                 sc->socket->health_check_interval() > 0/*HC enabled*/) &&
                sc->ref_count != 0) {
                ++nleft;
                if (nleft == 0) {
                    err << "Left in SocketMap(" << this << "):";
                }
                err << ' ' << *sc->socket;
            }
        }
        if (nleft) {
            LOG(ERROR) << err.str();
        }
    }

    delete _this_map_bvar;
    _this_map_bvar = NULL;

    delete _options.socket_creator;
    _options.socket_creator = NULL;
}

int SocketMap::Init(const SocketMapOptions& options) {
    if (_options.socket_creator != NULL) {
        LOG(ERROR) << "Already initialized";
        return -1;
    }
    _options = options;
    if (_options.socket_creator == NULL) {
        LOG(ERROR) << "SocketOptions.socket_creator must be set";
        return -1;
    }
    if (_map.init(_options.suggested_map_size, 70) != 0) {
        LOG(ERROR) << "Fail to init _map";
        return -1;
    }
    if (_options.idle_timeout_second_dynamic != NULL ||
        _options.idle_timeout_second > 0) {
        if (bthread_start_background(&_close_idle_thread, NULL,
                                     RunWatchConnections, this) != 0) {
            LOG(FATAL) << "Fail to start bthread";
            return -1;
        }
        _has_close_idle_thread = true;
    }
    return 0;
}

void SocketMap::Print(std::ostream& os) {
    // TODO: Elaborate.
    size_t count = 0;
    {
        std::unique_lock<butil::Mutex> mu(_mutex);
        count = _map.size();
    }
    os << "count=" << count;
}

void SocketMap::PrintSocketMap(std::ostream& os, void* arg) {
    static_cast<SocketMap*>(arg)->Print(os);
}

int SocketMap::Insert(const SocketMapKey& key, SocketId* id) {
    SocketMapKeyChecksum ck(key);
    std::unique_lock<butil::Mutex> mu(_mutex);
    SingleConnection* sc = _map.seek(ck);
    if (sc) {
        if (!sc->socket->Failed() ||
            sc->socket->health_check_interval() > 0/*HC enabled*/) {
            ++sc->ref_count;
            *id = sc->socket->id();
            return 0;
        }
        // A socket w/o HC is failed (permanently), replace it.
        SocketUniquePtr ptr(sc->socket);  // Remove the ref added at insertion.
        _map.erase(ck); // in principle, we can override the entry in map w/o
        // removing and inserting it again. But this would make error branches
        // below have to remove the entry before returning, which is
        // error-prone. We prefer code maintainability here.
        sc = NULL;
    }
    std::unique_ptr<SSL_CTX, FreeSSLCTX> ssl_ctx(
        CreateClientSSLContext(key.ssl_options));
    if (key.ssl_options.enable && !ssl_ctx) {
        return -1;
    }
    SocketId tmp_id;
    SocketOptions opt;
    opt.remote_side = key.peer;
    // Can't save SSL_CTX in SocketMap since SingleConnection's desctruction
    // may happen before Socket's destruction (remove Channel before RPC complete)
    opt.owns_ssl_ctx = true;
    opt.ssl_ctx = ssl_ctx.get();
    opt.sni_name = key.ssl_options.sni_name;
    if (_options.socket_creator->CreateSocket(opt, &tmp_id) != 0) {
        PLOG(FATAL) << "Fail to create socket to " << key.peer;
        return -1;
    }
    ssl_ctx.release();
    // Add a reference to make sure that sc->socket is always accessible. Not
    // use SocketUniquePtr which cannot put into containers before c++11.
    // The ref will be removed at entry's removal.
    SocketUniquePtr ptr;
    if (Socket::Address(tmp_id, &ptr) != 0) {
        LOG(FATAL) << "Fail to address SocketId=" << tmp_id;
        return -1;
    }
    SingleConnection new_sc = { 1, ptr.release(), 0 };
    _map[ck] = new_sc;
    *id = tmp_id;
    bool need_to_create_bvar = false;
    if (FLAGS_show_socketmap_in_vars && !_exposed_in_bvar) {
        _exposed_in_bvar = true;
        need_to_create_bvar = true;
    }
    mu.unlock();
    if (need_to_create_bvar) {
        char namebuf[32];
        int len = snprintf(namebuf, sizeof(namebuf), "rpc_socketmap_%p", this);
        _this_map_bvar = new bvar::PassiveStatus<std::string>(
            butil::StringPiece(namebuf, len), PrintSocketMap, this);
    }
    return 0;
}

void SocketMap::Remove(const SocketMapKey& key, SocketId expected_id) {
    return RemoveInternal(key, expected_id, false);
}

void SocketMap::RemoveInternal(const SocketMapKey& key,
                               SocketId expected_id,
                               bool remove_orphan) {
    SocketMapKeyChecksum ck(key);
    std::unique_lock<butil::Mutex> mu(_mutex);
    SingleConnection* sc = _map.seek(ck);
    if (!sc) {
        return;
    }
    if (!remove_orphan &&
        (expected_id == (SocketId)-1 || expected_id == sc->socket->id())) {
        --sc->ref_count;
    }
    if (sc->ref_count == 0) {
        // NOTE: save the gflag which may be reloaded at any time
        const int defer_close_second = _options.defer_close_second_dynamic ?
            *_options.defer_close_second_dynamic
            : _options.defer_close_second;
        if (!remove_orphan && defer_close_second > 0) {
            // Start count down on this Socket 
            sc->no_ref_us = butil::cpuwide_time_us();
        } else {
            Socket* const s = sc->socket;
            _map.erase(ck);
            bool need_to_create_bvar = false;
            if (FLAGS_show_socketmap_in_vars && !_exposed_in_bvar) {
                _exposed_in_bvar = true;
                need_to_create_bvar = true;
            }
            mu.unlock();
            if (need_to_create_bvar) {
                char namebuf[32];
                int len = snprintf(namebuf, sizeof(namebuf), "rpc_socketmap_%p", this);
                _this_map_bvar = new bvar::PassiveStatus<std::string>(
                    butil::StringPiece(namebuf, len), PrintSocketMap, this);
            }
            s->ReleaseAdditionalReference(); // release extra ref
            SocketUniquePtr ptr(s);  // Dereference
        }
    }
}

int SocketMap::Find(const SocketMapKey& key, SocketId* id) {
    SocketMapKeyChecksum ck(key);
    BAIDU_SCOPED_LOCK(_mutex);
    SingleConnection* sc = _map.seek(ck);
    if (sc) {
        *id = sc->socket->id();
        return 0;
    }
    return -1;
}

void SocketMap::List(std::vector<SocketId>* ids) {
    ids->clear();
    BAIDU_SCOPED_LOCK(_mutex);
    for (Map::iterator it = _map.begin(); it != _map.end(); ++it) {
        ids->push_back(it->second.socket->id());
    }
}

void SocketMap::List(std::vector<butil::EndPoint>* pts) {
    pts->clear();
    BAIDU_SCOPED_LOCK(_mutex);
    for (Map::iterator it = _map.begin(); it != _map.end(); ++it) {
        pts->push_back(it->second.socket->remote_side());
    }
}

void SocketMap::ListOrphans(int64_t defer_us, std::vector<butil::EndPoint>* out) {
    out->clear();
    const int64_t now = butil::cpuwide_time_us();
    BAIDU_SCOPED_LOCK(_mutex);
    for (Map::iterator it = _map.begin(); it != _map.end(); ++it) {
        SingleConnection& sc = it->second;
        if (sc.ref_count == 0 && now - sc.no_ref_us >= defer_us) {
            out->push_back(it->first.peer);
        }
    }
}

void* SocketMap::RunWatchConnections(void* arg) {
    static_cast<SocketMap*>(arg)->WatchConnections();
    return NULL;
}

void SocketMap::WatchConnections() {
    std::vector<SocketId> main_sockets;
    std::vector<SocketId> pooled_sockets;
    std::vector<butil::EndPoint> orphan_sockets;
    const uint64_t CHECK_INTERVAL_US = 1000000UL;
    while (bthread_usleep(CHECK_INTERVAL_US) == 0) {
        // NOTE: save the gflag which may be reloaded at any time.
        const int idle_seconds = _options.idle_timeout_second_dynamic ?
            *_options.idle_timeout_second_dynamic
            : _options.idle_timeout_second;
        if (idle_seconds > 0) {
            // Check idle pooled connections
            List(&main_sockets);
            for (size_t i = 0; i < main_sockets.size(); ++i) {
                SocketUniquePtr s;
                if (Socket::Address(main_sockets[i], &s) == 0) {
                    s->ListPooledSockets(&pooled_sockets);
                    for (size_t i = 0; i < pooled_sockets.size(); ++i) {
                        SocketUniquePtr s2;
                        if (Socket::Address(pooled_sockets[i], &s2) == 0) {
                            s2->ReleaseReferenceIfIdle(idle_seconds);
                        }
                    }
                }
            }
        }

        // Check connections without Channel. This works when `defer_seconds'
        // <= 0, in which case orphan connections will be closed immediately
        // NOTE: save the gflag which may be reloaded at any time
        const int defer_seconds = _options.defer_close_second_dynamic ?
            *_options.defer_close_second_dynamic :
            _options.defer_close_second;
        ListOrphans(defer_seconds * 1000000L, &orphan_sockets);
        for (size_t i = 0; i < orphan_sockets.size(); ++i) {
            RemoveInternal(orphan_sockets[i], (SocketId)-1, true);
        }
    }
}

} // namespace brpc
