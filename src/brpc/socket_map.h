// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 20:02:37 CST 2014

#include <vector>                                  // std::vector
#include "base/containers/flat_map.h"              // FlatMap
#include "brpc/socket_id.h"                   // SockdetId
#include "brpc/options.pb.h"                  // ProtocolType
#include "brpc/input_messenger.h"             // InputMessageHandler


namespace brpc {

// Global mapping from remote-side to out-going sockets created by Channels.

// Try to share the Socket to `pt'. If the Socket does not exist, create one.
// The corresponding SocketId is written to `*id'. If this function returns
// successfully, SocketMapRemove() MUST be called when the Socket is not needed.
// Return 0 on success, -1 otherwise.
int SocketMapInsert(base::EndPoint pt, SocketId* id);

// Find the SocketId associated with `pt'.
// Return 0 on found, -1 otherwise.
int SocketMapFind(base::EndPoint pt, SocketId* id);

// Called once when the Socket returned by SocketMapInsert() is not needed.
void SocketMapRemove(base::EndPoint pt);

// Put all existing Sockets into `ids'
void SocketMapList(std::vector<SocketId>* ids);

// ======================================================================
// The underlying class that can be used otherwhere for mapping endpoints
// to sockets.

// SocketMap creates sockets on-demand by calling this object.
class SocketCreator {
public:
    virtual ~SocketCreator() {}
    virtual int CreateSocket(const base::EndPoint& pt, SocketId* id) = 0;
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
    int Insert(const base::EndPoint& pt, SocketId* id);
    void Remove(const base::EndPoint& pt, SocketId expected_id);
    int Find(const base::EndPoint& pt, SocketId* id);
    void List(std::vector<SocketId>* ids);
    void List(std::vector<base::EndPoint>* pts);
    const SocketMapOptions& options() const { return _options; }

private:
    void RemoveInternal(const base::EndPoint& pt, SocketId id,
                        bool remove_orphan);
    void ListOrphans(int defer_seconds, std::vector<base::EndPoint>* out);
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
    // and destroyed, a single map+mutex may become hot-spots.
    typedef base::FlatMap<base::EndPoint, SingleConnection> Map;
    SocketMapOptions _options;
    base::Mutex _mutex;
    Map _map;
    bool _exposed_in_bvar;
    bvar::PassiveStatus<std::string>* _this_map_bvar;
    bool _has_close_idle_thread;
    bthread_t _close_idle_thread;
};

} // namespace brpc

