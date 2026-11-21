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

#include "brpc/details/server_tls_task_binding.h"

#include <errno.h>
#include <map>
#include <new>

#include "bthread/task_lifecycle.h"
#include "butil/scoped_lock.h"
#include "butil/synchronization/lock.h"
#include "brpc/details/server_tls_context.h"

namespace brpc {
namespace details {
namespace {

struct TaskToken {
    explicit TaskToken(const scoped_refptr<ServerTLSContext>& context_in)
        : context(context_in) {
    }

    scoped_refptr<ServerTLSContext> context;
    ServerTLSContext::Lease lease;
};

struct TaskPoolRegistry {
    butil::Mutex mutex;
    // A null context is a permanent retired tombstone.
    std::map<bthread_keytable_pool_t*, scoped_refptr<ServerTLSContext> > entries;
};

TaskPoolRegistry* GetTaskPoolRegistry() {
    // Process-lifetime storage prevents registry destruction from racing late
    // task admission or release during program shutdown.
    static TaskPoolRegistry* const registry = new TaskPoolRegistry;
    return registry;
}

int AdmitTask(bthread_keytable_pool_t* pool,
              const void* parent_token,
              void** output) {
    if (output == nullptr) {
        return EINVAL;
    }
    *output = nullptr;

    scoped_refptr<ServerTLSContext> context;
    {
        TaskPoolRegistry* const registry = GetTaskPoolRegistry();
        BAIDU_SCOPED_LOCK(registry->mutex);
        const auto it = registry->entries.find(pool);
        if (it == registry->entries.end()) {
            // Unregistered pools retain their legacy behavior.
            return 0;
        }
        if (it->second == nullptr) {
            return ECANCELED;
        }
        context = it->second;
    }

    TaskToken* const token = new (std::nothrow) TaskToken(context);
    if (token == nullptr) {
        return ENOMEM;
    }

    int rc = 0;
    const TaskToken* const parent = static_cast<const TaskToken*>(parent_token);
    if (parent != nullptr && parent->context.get() == context.get()) {
        rc = parent->lease.Derive(ServerTLSContext::LeaseKind::TASK, &token->lease);
    } else {
        rc = context->AdmitIndependent(ServerTLSContext::LeaseKind::TASK, &token->lease);
    }
    if (rc != 0) {
        delete token;
        return rc;
    }
    *output = token;
    return 0;
}

void ReleaseTask(void* opaque_token) {
    // Deleting the token releases its Lease. Both this callback and the
    // context's final wake must remain non-yielding because TaskGroup invokes
    // it after publishing task completion.
    delete static_cast<TaskToken*>(opaque_token);
}

const bthread::TaskLifecycleHooks kTaskLifecycleHooks = {
    AdmitTask,
    ReleaseTask
};

}  // namespace

int RegisterServerTLSTaskPool(
        bthread_keytable_pool_t* pool,
        const scoped_refptr<ServerTLSContext>& context) {
    if (pool == nullptr || context == nullptr || pool->list == nullptr ||
        pool->destroyed) {
        return EINVAL;
    }
    const ServerTLSContext::Snapshot snapshot = context->GetSnapshot();
    if (snapshot.phase != ServerTLSContext::Phase::INITIALIZING &&
        snapshot.phase != ServerTLSContext::Phase::OPEN) {
        return ECANCELED;
    }

    TaskPoolRegistry* registry = nullptr;
    try {
        // Construct process-lifetime registry storage before publishing the
        // hook. If allocation fails, legacy tasks must not observe an installed
        // hook whose first lookup could throw.
        registry = GetTaskPoolRegistry();
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }

    const int hook_rc =
        bthread::InstallTaskLifecycleHooks(&kTaskLifecycleHooks);
    if (hook_rc != 0) {
        return hook_rc;
    }

    try {
        BAIDU_SCOPED_LOCK(registry->mutex);
        if (registry->entries.find(pool) != registry->entries.end()) {
            return EEXIST;
        }
        registry->entries.emplace(pool, context);
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }
    return 0;
}

int RetireServerTLSTaskPool(bthread_keytable_pool_t* pool) {
    if (pool == nullptr) {
        return EINVAL;
    }

    scoped_refptr<ServerTLSContext> context;
    {
        TaskPoolRegistry* const registry = GetTaskPoolRegistry();
        BAIDU_SCOPED_LOCK(registry->mutex);
        const auto it = registry->entries.find(pool);
        if (it == registry->entries.end()) {
            return ENOENT;
        }
        if (it->second == nullptr) {
            return 0;
        }
        context = it->second;
    }
    if (context->GetSnapshot().phase != ServerTLSContext::Phase::RETIRED) {
        return EBUSY;
    }

    scoped_refptr<ServerTLSContext> detached;
    {
        TaskPoolRegistry* const registry = GetTaskPoolRegistry();
        BAIDU_SCOPED_LOCK(registry->mutex);
        const auto it = registry->entries.find(pool);
        if (it == registry->entries.end()) {
            return ENOENT;
        }
        if (it->second == nullptr) {
            return 0;
        }
        detached.swap(it->second);
    }
    // Drop the registry's context pin outside the registry mutex.
    return 0;
}

}  // namespace details
}  // namespace brpc
