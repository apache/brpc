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


#include "brpc/server_id.h"


namespace brpc {

ServerId2SocketIdMapper::ServerId2SocketIdMapper() {
    _tmp.reserve(128);
    if (_nref_map.init(128) != 0) {
        LOG(WARNING) << "Fail to init _nref_map";
    }
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
