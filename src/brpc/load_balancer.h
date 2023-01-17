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


#ifndef BRPC_LOAD_BALANCER_H
#define BRPC_LOAD_BALANCER_H

#include "bvar/passive_status.h"
#include "brpc/describable.h"
#include "brpc/destroyable.h"
#include "brpc/excluded_servers.h"                // ExcludedServers
#include "brpc/shared_object.h"                   // SharedObject
#include "brpc/server_id.h"                       // ServerId
#include "brpc/extension.h"                       // Extension<T>

namespace brpc {

class Controller;

// Select a server from a set of servers (in form of ServerId).
class LoadBalancer : public NonConstDescribable, public Destroyable {
public:
    struct SelectIn {
        int64_t begin_time_us;
        // Weight of different nodes could be changed.
        bool changable_weights;
        bool has_request_code;
        uint64_t request_code;
        const ExcludedServers* excluded;
    };

    struct SelectOut {
        explicit SelectOut(SocketUniquePtr* ptr_in)
            : ptr(ptr_in), need_feedback(false) {}
        SocketUniquePtr* ptr;
        bool need_feedback;
    };

    struct CallInfo {
        // Exactly same with SelectIn.begin_time_us, may be different from
        // controller->_begin_time_us which is beginning of the RPC.
        int64_t begin_time_us;
        // Remote side of the call.
        SocketId server_id;
        // A RPC may have multiple calls, this error may be different from
        // controller->ErrorCode();
        int error_code;
        // The controller for the RPC. Should NOT be saved in Feedback()
        // and used after the function.
        const Controller* controller;
    };

    LoadBalancer() { }

    // ====================================================================
    //  All methods must be thread-safe!
    //  Take a look at policy/round_robin_load_balancer.cpp to see how to
    //  make SelectServer() low contended by using DoublyBufferedData<>
    // =====================================================================
    
    // Add `server' into this balancer.
    // Returns true on added.
    virtual bool AddServer(const ServerId& server) = 0;

    // Remove `server' from this balancer.
    // Returns true iff the server was removed.
    virtual bool RemoveServer(const ServerId& server) = 0;

    // Add a list of `servers' into this balancer.
    // Returns number of servers added.
    virtual size_t AddServersInBatch(const std::vector<ServerId>& servers) = 0;

    // Remove a list of `servers' from this balancer.
    // Returns number of servers removed.
    virtual size_t RemoveServersInBatch(const std::vector<ServerId>& servers) = 0;
    
    // Select a server and address it into `out->ptr'.
    // If Feedback() should be called when the RPC is done, set
    // out->need_feedback to true.
    // Returns 0 on success, errno otherwise.
    virtual int SelectServer(const SelectIn& in, SelectOut* out) = 0;

    // Feedback this balancer with CallInfo gathered before RPC finishes.
    // This function is only called when corresponding SelectServer was
    // successful and out->need_feedback was set to true.
    virtual void Feedback(const CallInfo& /*info*/) { }

    // Create/destroy an instance.
    // Caller is responsible for Destroy() the instance after usage.
    virtual LoadBalancer* New(const butil::StringPiece& params) const = 0;

protected:
    virtual ~LoadBalancer() { }
};

DECLARE_bool(show_lb_in_vars);
DECLARE_int32(default_weight_of_wlb);

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
    static bool ParseParameters(const butil::StringPiece& lb_protocol,
                                std::string* lb_name,
                                butil::StringPiece* lb_params);
    static void DescribeLB(std::ostream& os, void* arg);
    void ExposeLB();

    LoadBalancer* _lb;
    butil::atomic<int> _weight_sum;
    volatile bool _exposed;
    butil::Mutex _st_mutex;
    bvar::PassiveStatus<std::string> _st;
};

// For registering global instances.
inline Extension<const LoadBalancer>* LoadBalancerExtension() {
    return Extension<const LoadBalancer>::instance();
}

} // namespace brpc


#endif  // BRPC_LOAD_BALANCER_H
