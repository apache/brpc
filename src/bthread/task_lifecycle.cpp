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

#include "bthread/task_lifecycle.h"

#include <errno.h>
#include <utility>

#include "butil/atomicops.h"

namespace bthread {

namespace {

butil::static_atomic<const TaskLifecycleHooks*> g_task_lifecycle_hooks =
    BUTIL_STATIC_ATOMIC_INIT(nullptr);

}  // namespace

int InstallTaskLifecycleHooks(const TaskLifecycleHooks* hooks) {
    if (hooks == nullptr || hooks->admit == nullptr || hooks->release == nullptr) {
        return EINVAL;
    }
    const TaskLifecycleHooks* expected = nullptr;
    if (g_task_lifecycle_hooks.compare_exchange_strong(
            expected, hooks, butil::memory_order_acq_rel,
            butil::memory_order_acquire)) {
        return 0;
    }
    return expected == hooks ? 0 : EBUSY;
}

TaskLifecycle::TaskLifecycle()
    : _hooks(nullptr)
    , _token(nullptr) {
}

TaskLifecycle::~TaskLifecycle() {
    Reset();
}

TaskLifecycle::TaskLifecycle(TaskLifecycle&& other) noexcept
    : _hooks(other._hooks)
    , _token(other._token) {
    other._hooks = nullptr;
    other._token = nullptr;
}

TaskLifecycle& TaskLifecycle::operator=(TaskLifecycle&& other) noexcept {
    if (this != &other) {
        Reset();
        _hooks = other._hooks;
        _token = other._token;
        other._hooks = nullptr;
        other._token = nullptr;
    }
    return *this;
}

int TaskLifecycle::Acquire(bthread_keytable_pool_t* pool,
                           const TaskLifecycle* parent) {
    if (valid()) {
        return EINVAL;
    }
    if (pool == nullptr) {
        return 0;
    }
    const TaskLifecycleHooks* hooks =
        g_task_lifecycle_hooks.load(butil::memory_order_acquire);
    if (hooks == nullptr) {
        return 0;
    }
    const void* parent_token = nullptr;
    if (parent != nullptr && parent->_hooks == hooks) {
        parent_token = parent->_token;
    }
    void* token = nullptr;
    const int rc = hooks->admit(pool, parent_token, &token);
    if (rc != 0) {
        // Keep rollback defensive if an implementation allocated a token
        // before discovering an admission error.
        if (token != nullptr) {
            hooks->release(token);
        }
        return rc;
    }
    if (token != nullptr) {
        _hooks = hooks;
        _token = token;
    }
    return 0;
}

void TaskLifecycle::Reset() {
    const TaskLifecycleHooks* hooks = _hooks;
    void* token = _token;
    _hooks = nullptr;
    _token = nullptr;
    if (token != nullptr) {
        hooks->release(token);
    }
}

}  // namespace bthread
