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

#include "bthread/worker_idle.h"

#include <errno.h>

#include <algorithm>
#include <new>
#include <vector>

#include "butil/atomicops.h"
#include "butil/containers/doubly_buffered_data.h"
#include "butil/time.h"
#include "butil/thread_local.h"

namespace bthread {
namespace {

enum InitState : uint8_t {
    INIT_STATE_NOT_RUN = 0,
    INIT_STATE_OK = 1,
    INIT_STATE_FAILED = 2,
};

struct WorkerIdleEntry {
    int id;
    int (*init_fn)(void);
    bool (*idle_fn)(void);
    uint64_t timeout_us;
};

typedef std::vector<WorkerIdleEntry> WorkerIdleEntryList;

static butil::DoublyBufferedData<WorkerIdleEntryList, butil::Void, true> g_entries;
static butil::atomic<int> g_next_id(1);

struct WorkerIdleTLS {
    std::vector<uint8_t> init_states;
};

BAIDU_THREAD_LOCAL WorkerIdleTLS* tls_worker_idle = NULL;

static WorkerIdleTLS* get_or_create_tls() {
    if (tls_worker_idle) {
        return tls_worker_idle;
    }
    tls_worker_idle = new (std::nothrow) WorkerIdleTLS;
    return tls_worker_idle;
}

}  // namespace

int register_worker_idle_function(int (*init_fn)(void),
                                  bool (*idle_fn)(void),
                                  uint64_t timeout_us,
                                  int* handle) {
    if (idle_fn == NULL) {
        return EINVAL;
    }
    if (timeout_us == 0) {
        return EINVAL;
    }
    const int id = g_next_id.fetch_add(1, butil::memory_order_relaxed);
    WorkerIdleEntry e;
    e.id = id;
    e.init_fn = init_fn;
    e.idle_fn = idle_fn;
    e.timeout_us = timeout_us;
    g_entries.Modify([&](WorkerIdleEntryList& bg) {
        bg.push_back(e);
        return static_cast<size_t>(1);
    });
    if (handle) {
        *handle = id;
    }
    return 0;
}

int unregister_worker_idle_function(int handle) {
    if (handle <= 0) {
        return EINVAL;
    }
    size_t removed = g_entries.Modify([&](WorkerIdleEntryList& bg) {
        const size_t old_size = bg.size();
        bg.erase(std::remove_if(bg.begin(), bg.end(),
                                [&](const WorkerIdleEntry& e) {
                                    return e.id == handle;
                                }),
                 bg.end());
        return old_size - bg.size();
    });
    return removed ? 0 : EINVAL;
}

bool has_worker_idle_functions() {
    butil::DoublyBufferedData<WorkerIdleEntryList, butil::Void, true>::ScopedPtr p;
    if (g_entries.Read(&p) != 0) {
        return false;
    }
    return !p->empty();
}

void run_worker_idle_functions() {
    if (!has_worker_idle_functions()) {
        return;
    }
    butil::DoublyBufferedData<WorkerIdleEntryList, butil::Void, true>::ScopedPtr p;
    if (g_entries.Read(&p) != 0) {
        return;
    }
    if (p->empty()) {
        return;
    }

    WorkerIdleTLS* tls = get_or_create_tls();
    if (tls == NULL) {
        return;
    }

    // Step 1: Ensure per-worker init is called at most once for each entry.
    // Step 2: Run idle callbacks for initialized entries.
    // Step 3: Ignore callback return values. The caller decides how to proceed.
    for (const auto& e : *p) {
        if (e.id <= 0 || e.idle_fn == NULL) {
            continue;
        }
        if (tls->init_states.size() <= static_cast<size_t>(e.id)) {
            tls->init_states.resize(static_cast<size_t>(e.id) + 1, INIT_STATE_NOT_RUN);
        }
        uint8_t& st = tls->init_states[static_cast<size_t>(e.id)];
        if (st == INIT_STATE_NOT_RUN) {
            // Run the init callback function once.
            if (e.init_fn) {
                const int rc = e.init_fn();
                st = (rc == 0) ? INIT_STATE_OK : INIT_STATE_FAILED;
            } else {
                st = INIT_STATE_OK;
            }
        }
        if (st != INIT_STATE_OK) {
            continue;
        }
        // Run the idle callback function.
        e.idle_fn();
    }
}

timespec get_worker_idle_timeout() {
    butil::DoublyBufferedData<WorkerIdleEntryList, butil::Void, true>::ScopedPtr p;
    if (g_entries.Read(&p) != 0) {
        return {0, 0};
    }
    if (p->empty()) {
        return {0, 0};
    }
    uint64_t min_us = 0;
    for (const auto& e : *p) {
        if (e.timeout_us == 0) {
            continue;
        }
        if (min_us == 0 || e.timeout_us < min_us) {
            min_us = e.timeout_us;
        }
    }
    if (min_us == 0) {
        return {0, 0};
    }
    return butil::microseconds_to_timespec(min_us);
}

}  // namespace bthread


