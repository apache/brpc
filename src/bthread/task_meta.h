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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_TASK_META_H
#define BTHREAD_TASK_META_H

#include <pthread.h>                 // pthread_spin_init
#include "bthread/butex.h"           // butex_construct/destruct
#include "butil/atomicops.h"          // butil::atomic
#include "bthread/types.h"           // bthread_attr_t
#include "bthread/stack.h"           // ContextualStack
#include "bthread/timer_thread.h"

namespace bthread {

struct TaskStatistics {
    int64_t cputime_ns;
    int64_t nswitch;
    int64_t cpu_usage_ns;
};

class KeyTable;
struct ButexWaiter;

struct LocalStorage {
    KeyTable* keytable;
    void* assigned_data;
    void* rpcz_parent_span;
};

#define BTHREAD_LOCAL_STORAGE_INITIALIZER { NULL, NULL, NULL }

const static LocalStorage LOCAL_STORAGE_INIT = BTHREAD_LOCAL_STORAGE_INITIALIZER;

enum TaskStatus {
    TASK_STATUS_UNKNOWN,
    TASK_STATUS_CREATED,
    TASK_STATUS_FIRST_READY,
    TASK_STATUS_READY,
    TASK_STATUS_JUMPING,
    TASK_STATUS_RUNNING,
    TASK_STATUS_SUSPENDED,
    TASK_STATUS_END,
};

struct TaskMeta {
    // [Not Reset]
    butil::atomic<ButexWaiter*> current_waiter{NULL};
    uint64_t current_sleep{TimerThread::INVALID_TASK_ID};

    // A flag to mark if the Timer scheduling failed.
    bool sleep_failed{false};

    // A builtin flag to mark if the thread is stopping.
    bool stop{false};

    // The thread is interrupted and should wake up from some blocking ops.
    bool interrupted{false};

    // Scheduling of the thread can be delayed.
    bool about_to_quit{false};
    
    // [Not Reset] guarantee visibility of version_butex.
    pthread_spinlock_t version_lock{};
    
    // [Not Reset] only modified by one bthread at any time, no need to be atomic
    uint32_t* version_butex{NULL};

    // The identifier. It does not have to be here, however many code is
    // simplified if they can get tid from TaskMeta.
    bthread_t tid{INVALID_BTHREAD};

    // User function and argument
    void* (*fn)(void*){NULL};
    void* arg{NULL};

    // Stack of this task.
    ContextualStack* stack{NULL};

    // Attributes creating this task
    bthread_attr_t attr{BTHREAD_ATTR_NORMAL};
    
    // Statistics
    int64_t cpuwide_start_ns{0};
    TaskStatistics stat{};

    // bthread local storage, sync with tls_bls (defined in task_group.cpp)
    // when the bthread is created or destroyed.
    // DO NOT use this field directly, use tls_bls instead.
    LocalStorage local_storage{};

    // Only used when TaskTracer is enabled.
    // Bthread status.
    TaskStatus status{TASK_STATUS_UNKNOWN};
    // Whether bthread is traced？
    bool traced{false};
    // Worker thread id.
    pid_t worker_tid{-1};

public:
    // Only initialize [Not Reset] fields, other fields will be reset in
    // bthread_start* functions
    TaskMeta() {
        pthread_spin_init(&version_lock, 0);
        version_butex = butex_create_checked<uint32_t>();
        *version_butex = 1;
    }
        
    ~TaskMeta() {
        butex_destroy(version_butex);
        version_butex = NULL;
        pthread_spin_destroy(&version_lock);
    }

    void set_stack(ContextualStack* s) {
        stack = s;
    }

    ContextualStack* release_stack() {
        ContextualStack* tmp = stack;
        stack = NULL;
        return tmp;
    }

    StackType stack_type() const {
        return static_cast<StackType>(attr.stack_type);
    }
};

}  // namespace bthread

#endif  // BTHREAD_TASK_META_H
