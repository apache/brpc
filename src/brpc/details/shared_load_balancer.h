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

#ifndef BRPC_SHARED_LOAD_BALANCER_H
#define BRPC_SHARED_LOAD_BALANCER_H

#include "bvar/passive_status.h"
#include "brpc/load_balancer.h"
#include "brpc/shared_object.h"                   // SharedObject

namespace brpc {

DECLARE_bool(show_lb_in_vars);

// A intrusively shareable load balancer created from name.
class SharedLoadBalancer : public SharedObject, public NonConstDescribable {
public:
    SharedLoadBalancer();
    ~SharedLoadBalancer();

    int Init(const char* lb_name);

    int SelectServer(const LoadBalancer::SelectIn& in,
                     LoadBalancer::SelectOut* out) {
        if (FLAGS_show_lb_in_vars && !_exposed) {
            ExposeLB();
        }
        return _lb->SelectServer(in, out);
    }

    void Feedback(const LoadBalancer::CallInfo& info) { _lb->Feedback(info); }
    
    bool AddServer(const ServerId& server) {
        if (_lb->AddServer(server)) {
            _weight_sum.fetch_add(1, butil::memory_order_relaxed);
            return true;
        }
        return false;
    }
    bool RemoveServer(const ServerId& server) {
        if (_lb->RemoveServer(server)) {
            _weight_sum.fetch_sub(1, butil::memory_order_relaxed);
            return true;
        }
        return false;
    }
    
    size_t AddServersInBatch(const std::vector<ServerId>& servers) {
        size_t n = _lb->AddServersInBatch(servers);
        if (n) {
            _weight_sum.fetch_add(n, butil::memory_order_relaxed);
        }
        return n;
    }

    size_t RemoveServersInBatch(const std::vector<ServerId>& servers) {
        size_t n = _lb->RemoveServersInBatch(servers);
        if (n) {
            _weight_sum.fetch_sub(n, butil::memory_order_relaxed);
        }
        return n;
    }

    virtual void Describe(std::ostream& os, const DescribeOptions&);

    virtual int Weight() {
        return _weight_sum.load(butil::memory_order_relaxed);
    }

private:
    static void DescribeLB(std::ostream& os, void* arg);
    void ExposeLB();

    LoadBalancer* _lb;
    butil::atomic<int> _weight_sum;
    volatile bool _exposed;
    butil::Mutex _st_mutex;
    bvar::PassiveStatus<std::string> _st;
};


} // namespace brpc

#endif  // BRPC_SHARED_LOAD_BALANCER_H
