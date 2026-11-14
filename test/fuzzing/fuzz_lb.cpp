// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to You under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and
// limitations under the License.

// Fuzz the consistent hashing load balancer (src/brpc/policy/
// consistent_hashing_load_balancer.cpp): fuzz bytes build the server ring
// (node count and ids) and then drive SelectServer with hostile request
// codes, exercising AddServer/RemoveServer/rebuild and the selection paths.
// Sockets are created through the same in-process pattern used by
// fuzz_common.h.

#include "brpc/socket.h"
#include "butil/endpoint.h"
#include "brpc/policy/consistent_hashing_load_balancer.h"
#include "brpc/excluded_servers.h"

static bool create_socket(brpc::SocketId* id, int port) {
    brpc::SocketOptions options;
    options.remote_side = butil::EndPoint(butil::IP_ANY, port);
    return brpc::Socket::Create(options, id) == 0;
}

// Sockets live in brpc's process-wide SocketMap for the lifetime of the
// process; use a fixed pool so the harness cannot grow it without bound.
static const size_t kPoolSize = 16;
static brpc::SocketId g_pool[kPoolSize];
static bool g_pool_ready = false;

static bool pool_init() {
    if (!g_pool_ready) {
        for (size_t i = 0; i < kPoolSize; ++i) {
            if (!create_socket(&g_pool[i], 10000 + static_cast<int>(i))) {
                return false;
            }
        }
        g_pool_ready = true;
    }
    return true;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) {
        return 0;
    }

    const size_t n_nodes = 1 + (data[0] % 8);
    if (size < 1 + n_nodes * 2 + 8) {
        return 0;
    }

    // hash type selected by the input: Ketama / MurmurHash3 / MD5 variants
    const int htype = (data[1] >> 4) % 3;
    brpc::policy::ConsistentHashingLoadBalancer lb(
        static_cast<brpc::policy::ConsistentHashingLoadBalancerType>(htype));

    if (!pool_init()) {
        return 0;
    }

    size_t off = 1;
    for (size_t i = 0; i < n_nodes; ++i) {
        lb.AddServer(brpc::ServerId(g_pool[data[1 + i * 2] % kPoolSize]));
        off += 2;
    }

    brpc::ExcludedServers* excluded = brpc::ExcludedServers::Create(8);
    const size_t n_select = (size - off) / 8;
    for (size_t i = 0; i < n_select; ++i) {
        uint64_t code = 0;
        for (size_t b = 0; b < 8; ++b) {
            code = (code << 8) | data[off + i * 8 + b];
        }
        brpc::LoadBalancer::SelectIn in = {
            0 /* begin_time_us */, true, true,
            static_cast<uint64_t>(code & 0xffffffffu), excluded};
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        lb.SelectServer(in, &out);

        // occasionally mutate the ring between selections
        if ((code & 3) == 0 && i + 1 < n_select) {
            const brpc::ServerId sid(
                g_pool[(code >> 32) % kPoolSize]);
            if ((code & 4) == 0) {
                lb.AddServer(sid);
            } else {
                lb.RemoveServer(sid);
            }
        }
    }
    brpc::ExcludedServers::Destroy(excluded);
    return 0;
}
