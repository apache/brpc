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


#ifndef BRPC_SOCKET_MAP_H
#define BRPC_SOCKET_MAP_H

#include <vector>                             // std::vector
#include "bvar/bvar.h"                        // bvar::PassiveStatus
#include "butil/containers/flat_map.h"        // FlatMap
#include "brpc/socket_id.h"                   // SockdetId
#include "brpc/options.pb.h"                  // ProtocolType
#include "brpc/input_messenger.h"             // InputMessageHandler
#include "brpc/server_node.h"                 // ServerNode

namespace brpc {

// Different signature means that the Channel needs separate sockets.
struct ChannelSignature {
    uint64_t data[2];
    
    ChannelSignature() { Reset(); }
    void Reset() { data[0] = data[1] = 0; }
};

inline bool operator==(const ChannelSignature& s1, const ChannelSignature& s2) {
    return s1.data[0] == s2.data[0] && s1.data[1] == s2.data[1];
}
inline bool operator!=(const ChannelSignature& s1, const ChannelSignature& s2) {
    return !(s1 == s2);
}

// The following fields uniquely define a Socket. In other word,
// Socket can't be shared between 2 different SocketMapKeys
struct SocketMapKey {
    explicit SocketMapKey(const butil::EndPoint& pt)
        : peer(pt)
    {}
    SocketMapKey(const butil::EndPoint& pt, const ChannelSignature& cs)
        : peer(pt), channel_signature(cs)
    {}
    SocketMapKey(const ServerNode& sn, const ChannelSignature& cs)
        : peer(sn), channel_signature(cs)
    {}

    ServerNode peer;
    ChannelSignature channel_signature;
};

inline bool operator==(const SocketMapKey& k1, const SocketMapKey& k2) {
    return k1.peer == k2.peer && k1.channel_signature == k2.channel_signature;
};

struct SocketMapKeyHasher {
    size_t operator()(const SocketMapKey& key) const {
        size_t h = butil::DefaultHasher<butil::EndPoint>()(key.peer.addr);
        h = h * 101 + butil::DefaultHasher<std::string>()(key.peer.tag);
        h = h * 101 + key.channel_signature.data[1];
        return h;
    }
};


// Try to share the Socket to `key'. If the Socket does not exist, create one.
// The corresponding SocketId is written to `*id'. If this function returns
// successfully, SocketMapRemove() MUST be called when the Socket is not needed.
// Return 0 on success, -1 otherwise.
int SocketMapInsert(const SocketMapKey& key, SocketId* id,
                    const std::shared_ptr<SocketSSLContext>& ssl_ctx);

inline int SocketMapInsert(const SocketMapKey& key, SocketId* id) {
    std::shared_ptr<SocketSSLContext> empty_ptr;
    return SocketMapInsert(key, id, empty_ptr);
}

// Find the SocketId associated with `key'.
// Return 0 on found, -1 otherwise.
int SocketMapFind(const SocketMapKey& key, SocketId* id);

// Called once when the Socket returned by SocketMapInsert() is not needed.
void SocketMapRemove(const SocketMapKey& key);

// Put all existing Sockets into `ids'
void SocketMapList(std::vector<SocketId>* ids);

// ======================================================================
// The underlying class that can be used otherwhere for mapping endpoints
// to sockets.

// SocketMap creates sockets on-demand by calling this object.
class SocketCreator {
public:
    virtual ~SocketCreator() {}
    virtual int CreateSocket(const SocketOptions& opt, SocketId* id) = 0;
};

struct SocketMapOptions {
    // Constructed with default options.
    SocketMapOptions();
    
    // For creating sockets by need. Owned and deleted by SocketMap.
    // Default: NULL (must be set by user).
    SocketCreator* socket_creator;

    // Initial size of the map (proper size reduces number of resizes)
    // Default: 1024
    size_t suggested_map_size;
  
    // Pooled connections without data transmission for so many seconds will
    // be closed. No effect for non-positive values.
    // If idle_timeout_second_dynamic is not NULL, use the dereferenced value
    // each time instead of idle_timeout_second.
    // Default: 0 (disabled)
    const int* idle_timeout_second_dynamic;
    int idle_timeout_second;

    // Defer close of connections for so many seconds even if the connection
    // is not used by anyone. Close immediately for non-positive values.
    // If defer_close_second_dynamic is not NULL, use the dereferenced value
    // each time instead of defer_close_second.
    // Default: 0 (disabled)
    const int* defer_close_second_dynamic;
    int defer_close_second;
};

// Share sockets to the same EndPoint.
class SocketMap {
public:
    SocketMap();
    ~SocketMap();
    int Init(const SocketMapOptions&);
    int Insert(const SocketMapKey& key, SocketId* id,
               const std::shared_ptr<SocketSSLContext>& ssl_ctx);
    int Insert(const SocketMapKey& key, SocketId* id) {
        std::shared_ptr<SocketSSLContext> empty_ptr;
        return Insert(key, id, empty_ptr);
    }

    void Remove(const SocketMapKey& key, SocketId expected_id);
    int Find(const SocketMapKey& key, SocketId* id);
    void List(std::vector<SocketId>* ids);
    void List(std::vector<butil::EndPoint>* pts);
    const SocketMapOptions& options() const { return _options; }

private:
    void RemoveInternal(const SocketMapKey& key, SocketId id,
                        bool remove_orphan);
    void ListOrphans(int64_t defer_us, std::vector<SocketMapKey>* out);
    void WatchConnections();
    static void* RunWatchConnections(void*);
    void Print(std::ostream& os);
    static void PrintSocketMap(std::ostream& os, void* arg);

private:
    struct SingleConnection {
        int ref_count;
        Socket* socket;
        int64_t no_ref_us;
    };

    // TODO: When RpcChannels connecting to one EndPoint are frequently created
    //       and destroyed, a single map+mutex may become hot-spots.
    typedef butil::FlatMap<SocketMapKey, SingleConnection,
                           SocketMapKeyHasher> Map;
    SocketMapOptions _options;
    butil::Mutex _mutex;
    Map _map;
    bool _exposed_in_bvar;
    bvar::PassiveStatus<std::string>* _this_map_bvar;
    bool _has_close_idle_thread;
    bthread_t _close_idle_thread;
};

} // namespace brpc

#endif  // BRPC_SOCKET_MAP_H
