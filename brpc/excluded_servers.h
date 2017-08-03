// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Sep 22 22:23:13 CST 2014

#ifndef BRPC_EXCLUDED_SERVERS_H
#define BRPC_EXCLUDED_SERVERS_H

#include "base/scoped_lock.h"
#include "base/containers/bounded_queue.h"
#include "brpc/socket_id.h"                       // SocketId


namespace brpc {

// Remember servers that should be avoided in selection. These servers
// are often selected in previous tries inside a RPC.
class ExcludedServers {
public:
    // Create a instance with at most `cap' servers.
    static ExcludedServers* Create(int cap);

    // Destroy the instance
    static void Destroy(ExcludedServers* ptr);

    // Add a server. If the internal queue is full, pop one from the queue first.
    void Add(SocketId id);

    // True if the server shall be excluded.
    bool IsExcluded(SocketId id) const;
    static bool IsExcluded(const ExcludedServers* s, SocketId id) {
        return s != NULL && s->IsExcluded(id);
    }

    // #servers inside.
    size_t size() const { return _l.size(); }

private:
    ExcludedServers(int cap)
        : _l(_space, sizeof(SocketId)* cap, base::NOT_OWN_STORAGE) {}
    ~ExcludedServers() {}
    // Controller::_accessed may be shared by sub channels in schan, protect
    // all mutable methods with this mutex. In ordinary channels, this mutex
    // is never contended.
    mutable base::Mutex _mutex;
    base::BoundedQueue<SocketId> _l;
    SocketId _space[0];
};

// ===================================================

inline ExcludedServers* ExcludedServers::Create(int cap) {
    void *space = malloc(
        offsetof(ExcludedServers, _space) + sizeof(SocketId) * cap);
    if (NULL == space) {
        return NULL;
    }
    return new (space) ExcludedServers(cap);
}

inline void ExcludedServers::Destroy(ExcludedServers* ptr) {
    if (ptr) {
        ptr->~ExcludedServers();
        free(ptr);
    }
}

inline void ExcludedServers::Add(SocketId id) {
    BAIDU_SCOPED_LOCK(_mutex);
    const SocketId* last_id = _l.bottom();
    if (last_id == NULL || *last_id != id) {
        _l.elim_push(id);
    }
}

inline bool ExcludedServers::IsExcluded(SocketId id) const {
    BAIDU_SCOPED_LOCK(_mutex);
    for (size_t i = 0; i < _l.size(); ++i) {
        if (*_l.bottom(i) == id) {
            return true;
        }
    }
    return false;
}

} // namespace brpc


#endif  // BRPC_EXCLUDED_SERVERS_H
