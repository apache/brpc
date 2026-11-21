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

#ifndef BTHREAD_TASK_LIFECYCLE_H
#define BTHREAD_TASK_LIFECYCLE_H

#include "bthread/types.h"
#include "butil/macros.h"

namespace bthread {

// Optional process-wide hooks for associating a task with the lifetime of its
// keytable pool. The installed table and the functions it points to must remain
// valid for the process lifetime. Hook functions must not yield, access bthread
// TLS, or invoke user code.
struct TaskLifecycleHooks {
    int (*admit)(bthread_keytable_pool_t* pool,
                 const void* parent_token,
                 void** token);
    void (*release)(void* token);
};

// Installs |hooks| once for the process. Reinstalling the same table is
// idempotent; installing a different table returns EBUSY.
int InstallTaskLifecycleHooks(const TaskLifecycleHooks* hooks);

// Move-only owner of an opaque task-lifecycle token. A TaskLifecycle must not
// be moved, reset, or used as a parent concurrently. Acquire and Reset invoke
// process-wide hooks and therefore inherit their non-yielding requirement.
class TaskLifecycle {
public:
    TaskLifecycle();
    ~TaskLifecycle();
    TaskLifecycle(TaskLifecycle&& other) noexcept;
    TaskLifecycle& operator=(TaskLifecycle&& other) noexcept;

    bool valid() const { return _token != nullptr; }

    // Acquires a lifecycle for |pool|. A null pool always remains unmanaged.
    // |parent| contributes authority only when it was created by the currently
    // installed hook table. On failure, this object remains empty.
    int Acquire(bthread_keytable_pool_t* pool, const TaskLifecycle* parent);
    void Reset();

private:
    const TaskLifecycleHooks* _hooks;
    void* _token;

    DISALLOW_COPY_AND_ASSIGN(TaskLifecycle);
};

}  // namespace bthread

#endif  // BTHREAD_TASK_LIFECYCLE_H
