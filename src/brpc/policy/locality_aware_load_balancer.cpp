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


#include <limits>                                            // numeric_limits
#include <gflags/gflags.h>
#include "butil/time.h"                                       // gettimeofday_us
#include "butil/fast_rand.h"
#include "brpc/log.h"
#include "brpc/socket.h"
#include "brpc/reloadable_flags.h"
#include "brpc/policy/locality_aware_load_balancer.h"

namespace brpc {
namespace policy {

DEFINE_int64(min_weight, 1000, "Minimum weight of a node in LALB");
DEFINE_double(punish_inflight_ratio, 1.5, "Decrease weight proportionally if "
              "average latency of the inflight requests exeeds average "
              "latency of the node times this ratio");
DEFINE_double(punish_error_ratio, 1.2,
              "Multiply latencies caused by errors with this ratio");

static const int64_t DEFAULT_QPS = 1;
static const size_t INITIAL_WEIGHT_TREE_SIZE = 128;
// 1008680231
static const int64_t WEIGHT_SCALE =
    std::numeric_limits<int64_t>::max() / 72000000 / (INITIAL_WEIGHT_TREE_SIZE - 1);

LocalityAwareLoadBalancer::LocalityAwareLoadBalancer()
    : _total(0) {
}

LocalityAwareLoadBalancer::~LocalityAwareLoadBalancer() {
    _db_servers.ModifyWithForeground(RemoveAll);
}

bool LocalityAwareLoadBalancer::Add(Servers& bg, const Servers& fg,
                                    SocketId id,
                                    LocalityAwareLoadBalancer* lb) {
    if (bg.weight_tree.capacity() < INITIAL_WEIGHT_TREE_SIZE) {
        bg.weight_tree.reserve(INITIAL_WEIGHT_TREE_SIZE);
    }
    if (bg.server_map.seek(id) != NULL) {
        // The id duplicates.
        return false;
    }
    const size_t* pindex = fg.server_map.seek(id);
    if (pindex == NULL) {
        // Both fg and bg do not have the id. We create and insert a new Weight
        // structure. Later when we modify the other buffer(current fg), just
        // copy the pointer.
        
        const size_t index = bg.weight_tree.size();
        // If there exists node, set initial weight to be average of existing
        // weights, set to WEIGHT_SCALE otherwise. If we insert from empty
        // and no update during the insertions, all initial weights will be
        // WEIGHT_SCALE, which feels good.
        int64_t initial_weight = WEIGHT_SCALE;
        if (!bg.weight_tree.empty()) {
            initial_weight = lb->_total.load(butil::memory_order_relaxed)
                / bg.weight_tree.size();
        }
        
        // Maintain the mapping from id to offset in weight_tree. This mapping
        // is just for fast testing of existence of id.
        bg.server_map[id] = index;

        // Push the weight structure into the tree. Notice that we also need
        // a left_weight entry to store weight sum of all left nodes so that
        // the load balancing by weights can be done in O(logN) complexity.
        ServerInfo info = { id, lb->PushLeft(), new Weight(initial_weight) };
        bg.weight_tree.push_back(info);

        // The weight structure may already have initial weight. Add the weight
        // to left_weight entries of all parent nodes and _total. The time
        // complexity is strictly O(logN) because the tree is complete.
        const int64_t diff = info.weight->volatile_value();
        if (diff) {
            bg.UpdateParentWeights(diff, index);
            lb->_total.fetch_add(diff, butil::memory_order_relaxed);
        }
    } else {
        // We already modified the other buffer, just sync. Two buffers are
        // always synced in this algorithm.
        bg.server_map[id] = bg.weight_tree.size();
        bg.weight_tree.push_back(fg.weight_tree[*pindex]);
    }
    return true;
}

bool LocalityAwareLoadBalancer::Remove(
    Servers& bg, SocketId id, LocalityAwareLoadBalancer* lb) {
    size_t* pindex = bg.server_map.seek(id);
    if (NULL == pindex) {
        // The id does not exist.
        return false;
    }
    // Save the index and remove mapping from id to the index.
    const size_t index = *pindex;
    bg.server_map.erase(id);
    
    Weight* w = bg.weight_tree[index].weight;
    // Set the weight to 0. Before we change weight of the parent nodes,
    // SelectServer may still go to the node, but when it sees a zero weight,
    // it retries, as if this range of weight is removed.
    const int64_t rm_weight = w->Disable();
    if (index + 1 == bg.weight_tree.size()) {
        // last node. Removing is easier.
        bg.weight_tree.pop_back();
        if (rm_weight) {
            // The first buffer. Remove the weight from parents to disable
            // traffic going to this node. We can't remove the left_weight
            // entry because the foreground buffer does not pop last node yet
            // and still needs the left_weight (which should be same size with
            // the tree). We can't delete the weight structure for same reason.
            int64_t diff = -rm_weight;
            bg.UpdateParentWeights(diff, index);
            lb->_total.fetch_add(diff, butil::memory_order_relaxed);
        } else {
            // the second buffer. clean left stuff.
            delete w;
            lb->PopLeft();
        }
    } else {
        // Move last node to position `index' to fill the space.
        bg.weight_tree[index].server_id = bg.weight_tree.back().server_id;
        bg.weight_tree[index].weight = bg.weight_tree.back().weight;
        bg.server_map[bg.weight_tree[index].server_id] = index;
        bg.weight_tree.pop_back();
        
        Weight* w2 = bg.weight_tree[index].weight;  // previously back()
        if (rm_weight) {
            // First buffer.
            // We need to remove the weight of last node from its parent
            // nodes and add the weight to parent nodes of node `index'.
            // However this process is not atomic. The foreground buffer still
            // sees w2 as last node and it may change the weight during the
            // process. To solve this problem, we atomically reset the weight
            // and remember the previous index (back()) in _old_index. Later
            // change to weight will add the diff to _old_diff_sum if _old_index
            // matches the index which SelectServer is from. In this way we
            // know the weight diff from foreground before we later modify it.
            const int64_t add_weight = w2->MarkOld(bg.weight_tree.size());

            // Add the weight diff to parent nodes of node `index'. Notice
            // that we don't touch parent nodes of last node here because
            // foreground is still sending traffic to last node.
            const int64_t diff = add_weight - rm_weight;
            if (diff) {
                bg.UpdateParentWeights(diff, index);
                lb->_total.fetch_add(diff, butil::memory_order_relaxed);
            }
            // At this point, the foreground distributes traffic to nodes
            // correctly except node `index' because weight of the node is 0.
        } else {
            // Second buffer.
            // Reset _old_* fields and get the weight change by SelectServer()
            // after MarkOld().
            const std::pair<int64_t, int64_t> p = w2->ClearOld();
            // Add the diff to parent nodes of node `index'
            const int64_t diff = p.second;
            if (diff) {
                bg.UpdateParentWeights(diff, index);
            }
            // Remove weight from parent nodes of last node.
            int64_t old_weight = - p.first - p.second;
            if (old_weight) {
                bg.UpdateParentWeights(old_weight, bg.weight_tree.size());
            }
            lb->_total.fetch_add(- p.first, butil::memory_order_relaxed);
            // Clear resources.
            delete w;
            lb->PopLeft();
        }
    }
    return true;
}

bool LocalityAwareLoadBalancer::RemoveAll(Servers& bg, const Servers& fg) {
    bg.server_map.clear();
    if (!fg.weight_tree.empty()) {
        for (size_t i = 0; i < bg.weight_tree.size(); ++i) {
            delete bg.weight_tree[i].weight;
        }
    }
    bg.weight_tree.clear();
    return true;
}

size_t LocalityAwareLoadBalancer::BatchAdd(
    Servers& bg, const Servers& fg, const std::vector<SocketId>& servers,
    LocalityAwareLoadBalancer* lb) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, fg, servers[i], lb);
    }
    return count;
}

// FIXME(gejun): not work
size_t LocalityAwareLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<SocketId>& servers,
    LocalityAwareLoadBalancer* lb) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i], lb);
    }
    return count;
}

bool LocalityAwareLoadBalancer::AddServer(const ServerId& id) {
    if (_id_mapper.AddServer(id)) {
        RPC_VLOG << "LALB: added " << id;
        return _db_servers.ModifyWithForeground(Add, id.id, this);
    } else {
        return true;
    }
}

bool LocalityAwareLoadBalancer::RemoveServer(const ServerId& id) {
    if (_id_mapper.RemoveServer(id)) {
        RPC_VLOG << "LALB: removed " << id;
        return _db_servers.Modify(Remove, id.id, this);
    } else {
        return true;
    }
}

size_t LocalityAwareLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    std::vector<SocketId> & ids = _id_mapper.AddServers(servers);
    RPC_VLOG << "LALB: added " << ids.size();
    _db_servers.ModifyWithForeground(BatchAdd, ids, this);
    return servers.size();
}

size_t LocalityAwareLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    std::vector<SocketId> & ids = _id_mapper.RemoveServers(servers);
    RPC_VLOG << "LALB: removed " << ids.size();
    size_t count = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        count += _db_servers.Modify(Remove, ids[i], this);
    }
    return count;
    // FIXME(gejun): Batch removing is buggy
    // return _db_servers.Modify(BatchRemove, servers, this);
}

int LocalityAwareLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    const size_t n = s->weight_tree.size();
    if (n == 0) {
        return ENODATA;
    }
    size_t ntry = 0;
    size_t nloop = 0;
    int64_t total = _total.load(butil::memory_order_relaxed);
    int64_t dice = butil::fast_rand_less_than(total);
    size_t index = 0;
    int64_t self = 0;
    while (total > 0) {
        // FIXME(gejun): This is the final protection from the selection code
        // falls into infinite loop. This branch should never be entered in
        // production servers. If it does, there must be a bug.
        if (++nloop > 10000) {
            LOG(ERROR) << "A selection runs too long!";
            return EHOSTDOWN;
        }
        
        // Locate a weight range in the tree. This is obviously not atomic and
        // left-weights / total / weight-of-the-node may not be consistent. But
        // this is what we have to pay to gain more parallelism.
        const ServerInfo & info = s->weight_tree[index];
        const int64_t left = info.left->load(butil::memory_order_relaxed);
        if (dice < left) {
            index = index * 2 + 1;
            if (index < n) {
                continue;
            }
        } else if (dice >= left + (self = info.weight->volatile_value())) {
            dice -= left + self;
            index = index * 2 + 2;
            if (index < n) {
                continue;
            }
        } else if (Socket::Address(info.server_id, out->ptr) == 0
                   && (*out->ptr)->IsAvailable()) {
            if ((ntry + 1) == n  // Instead of fail with EHOSTDOWN, we prefer
                                 // choosing the server again.
                || !ExcludedServers::IsExcluded(in.excluded, info.server_id)) {
                if (!in.changable_weights) {
                    return 0;
                }
                const Weight::AddInflightResult r =
                    info.weight->AddInflight(in, index, dice - left);
                if (r.weight_diff) {
                    s->UpdateParentWeights(r.weight_diff, index);
                    _total.fetch_add(r.weight_diff, butil::memory_order_relaxed);
                }
                if (r.chosen) {
                    out->need_feedback = true;
                    return 0;
                }
            }
            if (++ntry >= n) {
                break;
            }
        } else if (in.changable_weights) {
            const int64_t diff =
                info.weight->MarkFailed(index, total / n);
            if (diff) {
                s->UpdateParentWeights(diff, index);
                _total.fetch_add(diff, butil::memory_order_relaxed);
            }
            if (dice >= left + self + diff) {
                dice -= left + self + diff;
                index = index * 2 + 2;
                if (index < n) {
                    continue;
                }
            }
            if (++ntry >= n) {
                break;
            }
        } else {
            if (++ntry >= n) {
                break;
            } 
        }
        total = _total.load(butil::memory_order_relaxed);
        dice = butil::fast_rand_less_than(total);
        index = 0;
    }
    return EHOSTDOWN;
}

void LocalityAwareLoadBalancer::Feedback(const CallInfo& info) {        
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return;
    }
    const size_t* pindex = s->server_map.seek(info.server_id);
    if (NULL == pindex) {
        return;
    }
    const size_t index = *pindex;
    Weight* w = s->weight_tree[index].weight;
    const int64_t diff = w->Update(info, index);
    if (diff != 0) {
        s->UpdateParentWeights(diff, index);
        _total.fetch_add(diff, butil::memory_order_relaxed);
    }
}

int64_t LocalityAwareLoadBalancer::Weight::Update(
    const CallInfo& ci, size_t index) {
    const int64_t end_time_us = butil::gettimeofday_us();
    const int64_t latency = end_time_us - ci.begin_time_us;
    BAIDU_SCOPED_LOCK(_mutex);
    if (Disabled()) {
        // The weight was disabled and will be removed soon, do nothing
        // and the diff is 0.
        return 0;
    }

    _begin_time_sum -= ci.begin_time_us;
    --_begin_time_count;

    if (latency <= 0) {
        // time skews, ignore the sample.
        return 0;
    }
    if (ci.error_code == 0) {
        // Add a new entry
        TimeInfo tm_info = { latency, end_time_us };
        if (!_time_q.empty()) {
            tm_info.latency_sum += _time_q.bottom()->latency_sum;
        }
        _time_q.elim_push(tm_info);
    } else {
        // Accumulate into the last entry so that errors always decrease
        // the overall QPS and latency.
        // Note that the latency used is linearly mixed from the real latency
        // (of an erroneous call) and the timeout, so that errors that are more
        // unlikely to be solved by later retries are punished more.
        // Examples:
        //   max_retry=0: always use timeout
        //   max_retry=1, retried=0: latency
        //   max_retry=1, retried=1: timeout
        //   max_retry=2, retried=0: latency
        //   max_retry=2, retried=1: (latency + timeout) / 2
        //   max_retry=2, retried=2: timeout
        //   ...
        int ndone = 1;
        int nleft = 0;
        if (ci.controller->max_retry() > 0) {
            ndone = ci.controller->retried_count();
            nleft = ci.controller->max_retry() - ndone;
        }
        const int64_t err_latency =
            (nleft * (int64_t)(latency * FLAGS_punish_error_ratio)
             + ndone * ci.controller->timeout_ms() * 1000L) / (ndone + nleft);
        
        if (!_time_q.empty()) {
            TimeInfo* ti = _time_q.bottom();
            ti->latency_sum += err_latency;
            ti->end_time_us = end_time_us;
        } else {
            // If the first response is error, enlarge the latency as timedout
            // since we know nothing about the normal latency yet.
            const TimeInfo tm_info = {
                std::max(err_latency, ci.controller->timeout_ms() * 1000L),
                end_time_us
            };
            _time_q.push(tm_info);
        }
    }
        
    const int64_t top_time_us = _time_q.top()->end_time_us;
    const size_t n = _time_q.size();
    int64_t scaled_qps = DEFAULT_QPS * WEIGHT_SCALE;
    if (end_time_us > top_time_us) {        
        // Only calculate scaled_qps when the queue is full or the elapse
        // between bottom and top is reasonably large(so that error of the
        // calculated QPS is probably smaller).
        if (n == _time_q.capacity() ||
            end_time_us >= top_time_us + 1000000L/*1s*/) { 
            // will not overflow.
            scaled_qps = (n - 1) * 1000000L * WEIGHT_SCALE / (end_time_us - top_time_us);
            if (scaled_qps < WEIGHT_SCALE) {
                scaled_qps = WEIGHT_SCALE;
            }
        }
        _avg_latency = (_time_q.bottom()->latency_sum -
                        _time_q.top()->latency_sum) / (n - 1);
    } else if (n == 1) {
        _avg_latency = _time_q.bottom()->latency_sum;
    } else {
        // end_time_us <= top_time_us && n > 1: the QPS is so high that
        // the time elapse between top and bottom is 0(possible in examples),
        // or time skews, we don't update the weight for safety.
        return 0;
    }
    if (_avg_latency == 0) {
        return 0;
    }
    _base_weight = scaled_qps / _avg_latency;
    return ResetWeight(index, end_time_us);
}

LocalityAwareLoadBalancer* LocalityAwareLoadBalancer::New(
    const butil::StringPiece&) const {
    return new (std::nothrow) LocalityAwareLoadBalancer;
}

void LocalityAwareLoadBalancer::Destroy() {
    delete this;
}

void LocalityAwareLoadBalancer::Weight::Describe(std::ostream& os, int64_t now) {
    std::unique_lock<butil::Mutex> mu(_mutex);
    int64_t begin_time_sum = _begin_time_sum;
    int begin_time_count = _begin_time_count;
    int64_t weight = _weight;
    int64_t base_weight = _base_weight;
    size_t n = _time_q.size();
    double qps = 0;
    int64_t avg_latency = _avg_latency;
    if (n <= 1UL) {
        qps = 0;
    } else {
        if (n == _time_q.capacity()) {
            --n;
        }
        qps = n * 1000000 / (double)(now - _time_q.top()->end_time_us);
    }
    mu.unlock();

    os << "weight=" << weight;
    if (base_weight != weight) {
        os << "(base=" << base_weight << ')';
    }
    if (begin_time_count != 0) {
        os << " inflight_delay=" << now - begin_time_sum / begin_time_count
           << "(count=" << begin_time_count << ')';
    } else {
        os << " inflight_delay=0";
    }
    os  << " avg_latency=" << avg_latency
        << " expected_qps=" << qps;
}

void LocalityAwareLoadBalancer::Describe(
    std::ostream& os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "la";
        return;
    }
    os << "LocalityAware{total="
       << _total.load(butil::memory_order_relaxed) << ' ';
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        os << "fail to read _db_servers";
    } else {
        const int64_t now = butil::gettimeofday_us();
        const size_t n = s->weight_tree.size();
        os << '[';
        for (size_t i = 0; i < n; ++i) {
            const ServerInfo & info = s->weight_tree[i];
            os << "\n{id=" << info.server_id;
            {
                SocketUniquePtr tmp_ptr;
                if (Socket::Address(info.server_id, &tmp_ptr) != 0) {
                    os << "(broken)";
                }
            }
            os << " left="
               << info.left->load(butil::memory_order_relaxed) << ' ';
            info.weight->Describe(os, now);
            os << '}';
        }
        os << ']';
    }
    os << '}';
}

LocalityAwareLoadBalancer::Weight::Weight(int64_t initial_weight)
    : _weight(initial_weight)
    , _base_weight(initial_weight)
    , _begin_time_sum(0)
    , _begin_time_count(0)
    , _old_diff_sum(0)
    , _old_index((size_t)-1L)
    , _old_weight(0)
    , _avg_latency(0)
    , _time_q(_time_q_items, sizeof(_time_q_items), butil::NOT_OWN_STORAGE) {
}

LocalityAwareLoadBalancer::Weight::~Weight() {
}

int64_t LocalityAwareLoadBalancer::Weight::Disable() {
    BAIDU_SCOPED_LOCK(_mutex);
    const int64_t saved = _weight;
    _base_weight = -1;
    _weight = 0;
    return saved;
}

int64_t LocalityAwareLoadBalancer::Weight::MarkOld(size_t index) {
    BAIDU_SCOPED_LOCK(_mutex);
    const int64_t saved = _weight;
    _old_weight = saved;
    _old_diff_sum = 0;
    _old_index = index;
    return saved;
}
        
std::pair<int64_t, int64_t> LocalityAwareLoadBalancer::Weight::ClearOld() {
    BAIDU_SCOPED_LOCK(_mutex);
    const int64_t old_weight = _old_weight;
    const int64_t diff = _old_diff_sum;
    _old_diff_sum = 0;
    _old_index = (size_t)-1;
    _old_weight = 0;
    return std::make_pair(old_weight, diff);
}

}  // namespace policy
} // namespace brpc
