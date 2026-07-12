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


#include <cmath>                                       // std::exp
#include <gflags/gflags.h>
#include "butil/fast_rand.h"                           // fast_rand_less_than
#include "butil/time.h"                                // gettimeofday_us
#include "butil/string_splitter.h"                     // KeyValuePairsSplitter
#include "butil/strings/string_number_conversions.h"   // StringToUint
#include "brpc/socket.h"
#include "brpc/controller.h"
#include "brpc/policy/p2c_ewma_load_balancer.h"

namespace brpc {
namespace policy {

DEFINE_uint32(p2c_default_choices, 2,
              "Default number of servers sampled per selection in p2c, "
              "overridable per channel with the `choices' parameter");
DEFINE_int64(p2c_default_tau_ms, 10000,
             "Default decay time(ms) of the peak-EWMA latency in p2c "
             "(Finagle's PeakEwma default), overridable per channel with "
             "the `tau_ms' parameter");
DEFINE_int64(p2c_max_punish_ms, 30000,
             "Cap(ms) on the punished latency recorded for a failed call in "
             "p2c, bounding how long a persistently failing server takes to "
             "recover after it turns healthy. 0 means no cap");

namespace {
// Upper bound of the `choices' parameter. Compile-time because it sizes the
// sampling array on SelectServer's stack.
const uint32_t MAX_CHOICES = 64;
// Floor of the latency term so that in-flight counts break ties between
// servers that have no latency observation yet.
const double MIN_LATENCY_TERM_US = 1.0;

uint32_t WeightOfTag(const std::string& tag) {
    if (tag.empty()) {
        return 1;
    }
    uint32_t weight = 0;
    if (!butil::StringToUint(tag, &weight) || weight == 0) {
        LOG(WARNING) << "Invalid weight tag=`" << tag << "', use weight=1";
        return 1;
    }
    return weight;
}
}  // namespace

P2CEwmaLoadBalancer::P2CEwmaLoadBalancer()
    // Clamp so that broken flag values can not disable comparison or decay.
    : _choices(std::min(std::max(FLAGS_p2c_default_choices, 2u), MAX_CHOICES))
    , _tau_us(std::max<int64_t>(FLAGS_p2c_default_tau_ms, 1) * 1000L) {}

bool P2CEwmaLoadBalancer::Add(Servers& bg, const Servers& fg,
                              const ServerId& id) {
    if (bg.server_list.capacity() < 128) {
        bg.server_list.reserve(128);
    }
    if (bg.server_map.seek(id.id) != NULL) {
        return false;
    }
    ServerInfo info = { id.id, WeightOfTag(id.tag), NULL };
    const size_t* pindex = fg.server_map.seek(id.id);
    if (pindex == NULL) {
        // Both buffers do not have the server. Create the stat structure
        // which will be shared by both buffers.
        info.stat = std::make_shared<NodeStat>();
    } else {
        // Already added to the other buffer, share its stat.
        info.stat = fg.server_list[*pindex].stat;
    }
    bg.server_map[id.id] = bg.server_list.size();
    bg.server_list.push_back(info);
    return true;
}

bool P2CEwmaLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    size_t* pindex = bg.server_map.seek(id.id);
    if (pindex == NULL) {
        return false;
    }
    const size_t index = *pindex;
    bg.server_list[index] = bg.server_list.back();
    bg.server_map[bg.server_list[index].id] = index;
    bg.server_list.pop_back();
    bg.server_map.erase(id.id);
    return true;
}

size_t P2CEwmaLoadBalancer::BatchAdd(
    Servers& bg, const Servers& fg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, fg, servers[i]);
    }
    return count;
}

size_t P2CEwmaLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool P2CEwmaLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.ModifyWithForeground(Add, id);
}

bool P2CEwmaLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t P2CEwmaLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.ModifyWithForeground(BatchAdd, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

size_t P2CEwmaLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    return _db_servers.Modify(BatchRemove, servers);
}

double P2CEwmaLoadBalancer::Score(
    const ServerInfo& info, int64_t now_us) const {
    const int64_t ewma_us =
        info.stat->ewma_us.load(butil::memory_order_relaxed);
    const int32_t inflight =
        info.stat->inflight.load(butil::memory_order_relaxed);
    double latency_term = MIN_LATENCY_TERM_US;
    if (ewma_us > 0) {
        // Decay the (possibly stale) EWMA at read time so that a server
        // penalized long ago regains traffic and gets re-observed.
        const int64_t stamp_us =
            info.stat->stamp_us.load(butil::memory_order_relaxed);
        const int64_t elapsed_us = now_us - stamp_us;
        double decayed = (double)ewma_us;
        if (elapsed_us > 0) {
            decayed *= std::exp(-(double)elapsed_us / (double)_tau_us);
        }
        latency_term = std::max(decayed, MIN_LATENCY_TERM_US);
    }
    // Clamp so that a transiently negative counter can not invert routing.
    const int32_t load = std::max(inflight + 1, 1);
    return latency_term * (double)load / (double)info.weight;
}

int P2CEwmaLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    const size_t n = s->server_list.size();
    if (n == 0) {
        return ENODATA;
    }
    // Reuse the caller-provided timestamp to avoid an extra clock read per
    // selection; only Channel::CheckHealth passes 0.
    const int64_t now_us = in.begin_time_us > 0
        ? in.begin_time_us : butil::gettimeofday_us();

    const ServerInfo* best = NULL;
    double best_score = 0;
    SocketUniquePtr best_ptr;
    // Score the server at `index' and keep it if it beats the current best.
    // Excluded and unavailable servers are skipped.
    auto consider = [&](size_t index) {
        const ServerInfo& info = s->server_list[index];
        if (ExcludedServers::IsExcluded(in.excluded, info.id)) {
            return;
        }
        SocketUniquePtr ptr;
        if (!IsServerAvailable(info.id, &ptr)) {
            return;
        }
        const double score = Score(info, now_us);
        if (best == NULL || score < best_score) {
            best = &info;
            best_score = score;
            best_ptr.swap(ptr);
        }
    };

    const size_t choices = std::min((size_t)_choices, n);
    if (choices >= n) {
        // Scan from a random offset so that equal scores do not herd all
        // clients onto the lowest-indexed server.
        const size_t start = butil::fast_rand_less_than(n);
        for (size_t i = 0; i < n; ++i) {
            consider((start + i) % n);
        }
    } else {
        // Sample `choices' distinct random servers. Attempts are bounded so
        // that duplicated draws never loop for long.
        size_t chosen[MAX_CHOICES];
        size_t nchosen = 0;
        const size_t max_attempts = 4 * choices + 8;
        for (size_t attempt = 0;
             attempt < max_attempts && nchosen < choices; ++attempt) {
            const size_t index = butil::fast_rand_less_than(n);
            bool duplicated = false;
            for (size_t i = 0; i < nchosen; ++i) {
                if (chosen[i] == index) {
                    duplicated = true;
                    break;
                }
            }
            if (duplicated) {
                continue;
            }
            chosen[nchosen++] = index;
            consider(index);
        }
        if (best == NULL) {
            // All sampled servers were excluded or unavailable, fall back
            // to scoring the whole list before violating exclusion below.
            for (size_t i = 0; i < n; ++i) {
                consider(i);
            }
        }
    }

    if (best == NULL) {
        // Always take last chance: all servers are excluded, send to any
        // available one as rr/random do.
        for (size_t i = 0; i < n; ++i) {
            if (IsServerAvailable(s->server_list[i].id, &best_ptr)) {
                best = &s->server_list[i];
                break;
            }
        }
        if (best == NULL) {
            return EHOSTDOWN;
        }
    }
    if (in.changable_weights) {
        best->stat->inflight.fetch_add(1, butil::memory_order_relaxed);
        out->need_feedback = true;
    }
    out->ptr->swap(best_ptr);
    return 0;
}

void P2CEwmaLoadBalancer::Feedback(const CallInfo& info) {
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return;
    }
    const size_t* pindex = s->server_map.seek(info.server_id);
    if (pindex == NULL) {
        // The server was removed after selection, its stat is gone with it.
        return;
    }
    NodeStat* stat = s->server_list[*pindex].stat.get();
    stat->inflight.fetch_sub(1, butil::memory_order_relaxed);

    const int64_t now_us = butil::gettimeofday_us();
    int64_t latency_us = now_us - info.begin_time_us;
    if (latency_us <= 0) {
        // time skews, ignore the sample.
        return;
    }
    if (info.error_code != 0) {
        // Punish failures with at least the timeout(if any) and twice the
        // current average, so that a failing server keeps losing comparisons
        // even when it fails fast.
        const int64_t timeout_us = info.controller->timeout_ms() * 1000L;
        latency_us = std::max(latency_us, timeout_us);
        latency_us = std::max(
            latency_us, 2 * stat->ewma_us.load(butil::memory_order_relaxed));
        // Cap the punishment: consecutive failures double the EWMA each
        // time, which otherwise grows without bound and delays recovery
        // after the server turns healthy.
        const int64_t max_punish_us = FLAGS_p2c_max_punish_ms * 1000L;
        if (max_punish_us > 0 && latency_us > max_punish_us) {
            latency_us = max_punish_us;
        }
    }

    BAIDU_SCOPED_LOCK(stat->update_mutex);
    const int64_t ewma_us = stat->ewma_us.load(butil::memory_order_relaxed);
    int64_t new_ewma_us = latency_us;
    if (ewma_us > 0 && latency_us < ewma_us) {
        // Downward samples decay the average while upward spikes(handled by
        // the branch above) replace it immediately.
        const int64_t elapsed_us =
            now_us - stat->stamp_us.load(butil::memory_order_relaxed);
        const double w = elapsed_us > 0
            ? std::exp(-(double)elapsed_us / (double)_tau_us) : 1.0;
        new_ewma_us = (int64_t)(ewma_us * w + latency_us * (1.0 - w));
    }
    stat->ewma_us.store(new_ewma_us, butil::memory_order_relaxed);
    stat->stamp_us.store(now_us, butil::memory_order_relaxed);
}

P2CEwmaLoadBalancer* P2CEwmaLoadBalancer::New(
    const butil::StringPiece& params) const {
    P2CEwmaLoadBalancer* lb = new (std::nothrow) P2CEwmaLoadBalancer;
    if (lb != NULL && !lb->SetParameters(params)) {
        delete lb;
        lb = NULL;
    }
    return lb;
}

bool P2CEwmaLoadBalancer::SetParameters(const butil::StringPiece& params) {
    for (butil::KeyValuePairsSplitter sp(params.begin(), params.end(), ' ', '=');
         sp; ++sp) {
        if (sp.value().empty()) {
            LOG(ERROR) << "Empty value for " << sp.key() << " in lb parameter";
            return false;
        }
        if (sp.key() == "choices") {
            unsigned choices = 0;
            if (!butil::StringToUint(sp.value().as_string(), &choices) ||
                choices < 2 || choices > MAX_CHOICES) {
                LOG(ERROR) << "Invalid choices=`" << sp.value() << "'";
                return false;
            }
            _choices = choices;
        } else if (sp.key() == "tau_ms") {
            int64_t tau_ms = 0;
            if (!butil::StringToInt64(sp.value(), &tau_ms) || tau_ms <= 0) {
                LOG(ERROR) << "Invalid tau_ms=`" << sp.value() << "'";
                return false;
            }
            _tau_us = tau_ms * 1000L;
        } else {
            LOG(ERROR) << "Unknown parameter " << sp.key_and_value();
            return false;
        }
    }
    return true;
}

void P2CEwmaLoadBalancer::Destroy() {
    delete this;
}

void P2CEwmaLoadBalancer::Describe(
    std::ostream& os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "p2c";
        return;
    }
    os << "P2CEwma{choices=" << _choices << " tau_ms=" << _tau_us / 1000;
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        os << " fail to read _db_servers";
    } else {
        const int64_t now_us = butil::gettimeofday_us();
        os << " n=" << s->server_list.size() << ':';
        for (size_t i = 0; i < s->server_list.size(); ++i) {
            const ServerInfo& info = s->server_list[i];
            os << ' ' << info.id << '(' << "w=" << info.weight
               << " ewma_us="
               << info.stat->ewma_us.load(butil::memory_order_relaxed)
               << " inflight="
               << info.stat->inflight.load(butil::memory_order_relaxed)
               << " score=" << Score(info, now_us) << ')';
        }
    }
    os << '}';
}

}  // namespace policy
} // namespace brpc
