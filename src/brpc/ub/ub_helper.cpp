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

#if BRPC_WITH_UBRING

#include <dlfcn.h>                                // dlopen
#include <pthread.h>
#include <cstdlib>
#include <vector>
#include <gflags/gflags.h>
#include "butil/logging.h"
#include "brpc/socket.h"
#include "brpc/ub/ub_endpoint.h"
#include "brpc/ub/ub_helper.h"
#include "ub_ring_manager.h"

namespace brpc {
namespace ub {

void* g_handle_ub = NULL;
bool g_skip_ub_init = false;

butil::atomic<bool> g_ub_available(false);

void GlobalRelease() {
    g_ub_available.store(false, butil::memory_order_release);
    UBShmEndpoint::GlobalRelease();
    UBRingManager::UbrMgrFini();
    ShmMgrFini();
}

static inline void ExitWithError() {
    GlobalRelease();
    exit(1);
}

static void GlobalUBInitializeOrDieImpl() {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // Just for UT
        return;
    }

    if (UBRingManager::UbrMgrInit()) {
        PLOG(ERROR) << "Fail to UbrMgrInit";
        ExitWithError();
    }

    if (TimerInit()) {
        PLOG(ERROR) << "Fail to TimerInit";
        ExitWithError();
    }

    if (ShmMgrInit()) {
        PLOG(ERROR) << "Fail to ShmMgrInit";
        ExitWithError();
    }

    if (UBShmEndpoint::GlobalInitialize() < 0) {
        LOG(ERROR) << "rdma_recv_block_type incorrect "
                   << "(valid value: default/large/huge)";
        ExitWithError();
    }

    g_ub_available.store(true, butil::memory_order_relaxed);
}

static pthread_once_t initialize_ub_once = PTHREAD_ONCE_INIT;

void GlobalUBInitializeOrDie() {
    if (pthread_once(&initialize_ub_once,
                     GlobalUBInitializeOrDieImpl) != 0) {
        LOG(FATAL) << "Fail to pthread_once GlobalUBInitializeOrDie";
        exit(1);
    }
}

bool IsUBAvailable() {
    return g_ub_available.load(butil::memory_order_acquire);
}

void GlobalDisableUb() {
    if (g_ub_available.exchange(false, butil::memory_order_acquire)) {
        LOG(FATAL) << "ub is disabled due to some unrecoverable problem";
    }
}

bool SupportedByUB(std::string protocol) {
    if (protocol.compare("baidu_std") == 0) {
        return true;
    }
    return false;
}

bool InitPollingModeWithTag(bthread_tag_t tag,
                            std::function<void(void)> callback,
                            std::function<void(void)> init_fn,
                            std::function<void(void)> release_fn) {
    if (UBShmEndpoint::PollingModeInitialize(tag, callback, init_fn,
                                            release_fn) == 0) {
        return true;
    }
    return false;
}

}  // namespace ub
}  // namespace brpc

#else

#include <stdlib.h>
#include "butil/logging.h"

namespace brpc {
namespace ub {
void GlobalUBInitializeOrDie() {
    LOG(ERROR) << "brpc is not compiled with rdma. To enable it, please refer to "
               << "https://github.com/apache/brpc/blob/master/docs/en/rdma.md";
    exit(1);
}
}
}

#endif  // if BRPC_WITH_UBRING