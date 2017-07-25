// Baidu RPC - A framework to host and access services throughout Baidu. 
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep  7 17:24:45 CST 2014

#include "brpc/server_id.h"


namespace brpc {

ServerId2SocketIdMapper::ServerId2SocketIdMapper() {
    _tmp.reserve(128);
    CHECK_EQ(0, _nref_map.init(128));
}

ServerId2SocketIdMapper::~ServerId2SocketIdMapper() {
}

bool ServerId2SocketIdMapper::AddServer(const ServerId& server) {
    return (++_nref_map[server.id] == 1);
}

bool ServerId2SocketIdMapper::RemoveServer(const ServerId& server) {
    int* nref = _nref_map.seek(server.id);
    if (nref == NULL) {
        LOG(ERROR) << "Unexist SocketId=" << server.id;
        return false;
    }
    if (--*nref <= 0) {
        _nref_map.erase(server.id);
        return true;
    }
    return false;
}

std::vector<SocketId>& ServerId2SocketIdMapper::AddServers(
    const std::vector<ServerId>& servers) {
    _tmp.clear();
    for (size_t i = 0; i < servers.size(); ++i) {
        if (AddServer(servers[i])) {
            _tmp.push_back(servers[i].id);
        }
    }
    return _tmp;
}

std::vector<SocketId>& ServerId2SocketIdMapper::RemoveServers(
    const std::vector<ServerId>& servers) {
    _tmp.clear();
    for (size_t i = 0; i < servers.size(); ++i) {
        if (RemoveServer(servers[i])) {
            _tmp.push_back(servers[i].id);
        }
    }
    return _tmp;
}

} // namespace brpc

