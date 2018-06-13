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

// Authors: Zhao,Chong (zhaochong@bigo.sg)

#include <gflags/gflags.h>

#include "butil/logging.h"
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/policy/primary_load_balancer.h"
#include "brpc/controller.h"

// primary_lb_failure_window_s and primary_lb_failure_count_threshold should be set recording to 
// the timeout value, if timeout value of the channel is large, primary_lb_failure_window_s is
// small and primary_lb_failure_count_threshold is large too, when the fail reason is time out, 
// you will never reach the failure count threshold.
// Use a large value of primary_lb_failure_count_threshold here to disable this function.
DEFINE_int32(primary_lb_check_interval_us, 30000000, "Health check interval us, default 30s");
DEFINE_int32(primary_lb_failure_window_s, 10, "Failure count statistics time window, default 10s");
DEFINE_int32(primary_lb_failure_count_threshold, 10000000, "Failure count threshold, default 10000000");

namespace brpc {
namespace policy {

bool PrimaryLoadBalancer::Add(Servers& bg, const ServerId& id) {
    if (bg.server_list.capacity() < 128) {
        bg.server_list.reserve(128);
    }
    std::map<ServerId, size_t>::iterator it = bg.server_map.find(id);
    if (it != bg.server_map.end()) {
        return false;
    }
    bg.server_map[id] = bg.server_list.size();
    bg.server_list.push_back(id);
    return true;
}

bool PrimaryLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    std::map<ServerId, size_t>::iterator it = bg.server_map.find(id);
    if (it != bg.server_map.end()) {
        size_t index = it->second;
        bg.server_list[index] = bg.server_list.back();
        bg.server_map[bg.server_list[index]] = index;
        bg.server_list.pop_back();
        bg.server_map.erase(it);
        return true;
    }
    return false;
}

bool PrimaryLoadBalancer::Change(Servers& bg, const SocketId& id, uint64_t time_us) {
    std::map<SocketId, SocketStatus>::iterator it = bg.status_map.find(id);
    // time_us == 0 means success, delete this entry.
    if (time_us == 0 && it != bg.status_map.end()) {
        bg.status_map.erase(it);
        return true;
    }

    // Use reference here.
    SocketStatus &status = bg.status_map[id];
    // Failure count add 1.
    status.AddFailCount(1);
    if (status.GetFailCount() >= FLAGS_primary_lb_failure_count_threshold || status.last_remove_time > 0) {
        // Fail too many times in the window, mark the server removed.
        // Or if the servered was marked removed, update the removed time.
        status.last_remove_time = time_us;
    }

    return true;
}

size_t PrimaryLoadBalancer::BatchAdd(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, servers[i]);
    }
    return count;
}

size_t PrimaryLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool PrimaryLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.Modify(Add, id);
}

bool PrimaryLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t PrimaryLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchAdd, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

size_t PrimaryLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchRemove, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to RemoveServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

int PrimaryLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    int result = EHOSTDOWN;

    do {
        butil::DoublyBufferedData<Servers>::ScopedPtr s;
        if (_db_servers.Read(&s) != 0) {
            return ENOMEM;
        }
        size_t n = s->server_list.size();
        if (n == 0) {
            return ENODATA;
        }

        // Check the server one by one, that's the meaning of primary.
        for (size_t i = 0; i < n; ++i) {
            const SocketId id = s->server_list[i].id;
            if (ExcludedServers::IsExcluded(in.excluded, id)) {
                continue;
            }
            std::map<SocketId, SocketStatus>::const_iterator it = s->status_map.find(id);
            if (it != s->status_map.end() && it->second.last_remove_time > 0) {
                if (butil::gettimeofday_us() - it->second.last_remove_time < (uint64_t)FLAGS_primary_lb_check_interval_us) {
                    continue;
                }
            }
            if (Socket::Address(id, out->ptr) == 0 && !(*out->ptr)->IsLogOff()) {
                // We found an available server
                out->need_feedback = true;
                return 0;
            }
        }

        // In case that all servers reach the error count threshold,
        // select again without considering failure threshold.
        for (size_t i = 0; i < n; ++i) {
            const SocketId id = s->server_list[i].id;
            if (!ExcludedServers::IsExcluded(in.excluded, id)
                    && Socket::Address(id, out->ptr) == 0
                    && !(*out->ptr)->IsLogOff()) {
                // We found an available server
                result = 0;
                break;
            }
        }
    } while (0);

    return result;
}

PrimaryLoadBalancer* PrimaryLoadBalancer::New() const {
    return new (std::nothrow) PrimaryLoadBalancer;
}

void PrimaryLoadBalancer::Destroy() {
    delete this;
}

void PrimaryLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "primary";
        return;
    }
    os << "Primary{";
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        os << "fail to read _db_servers";
    } else {
        os << "n=" << s->server_list.size() << ':';
        for (size_t i = 0; i < s->server_list.size(); ++i) {
            os << ' ' << s->server_list[i];
        }
    }
    os << '}';
}

// Try to write double buffer less.
void PrimaryLoadBalancer::Feedback(const CallInfo& info) {
    int error_code = info.controller->ErrorCode();
    const SocketId id = info.server_id;
    bool server_recover = false;

    if (error_code != 0) {
        _db_servers.Modify(Change, id, butil::gettimeofday_us());
        return;
    } else {
        // Double buffer data use scope lock.
        butil::DoublyBufferedData<Servers>::ScopedPtr s;
        if (_db_servers.Read(&s) != 0) {
            return;
        }
        std::map<SocketId, SocketStatus>::const_iterator it = s->status_map.find(id);
        if (it != s->status_map.end() && it->second.last_remove_time > 0) {
            server_recover = true;
        }
    }

    if (server_recover) {
        _db_servers.Modify(Change, id, 0);
    }
}

PrimaryLoadBalancer::SocketStatus::SocketStatus() : last_remove_time(0),
                        _count(), _window_count(&_count, FLAGS_primary_lb_failure_window_s) {
}

PrimaryLoadBalancer::SocketStatus::SocketStatus(const SocketStatus &status) : last_remove_time(status.last_remove_time), _count(), _window_count(&_count, FLAGS_primary_lb_failure_window_s) {
}

void PrimaryLoadBalancer::SocketStatus::AddFailCount(int n) {
    _count << n;
}

int PrimaryLoadBalancer::SocketStatus::GetFailCount() {
    return _window_count.get_value();
}

}  // namespace policy
}  // namespace brpc
