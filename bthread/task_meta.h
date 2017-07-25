// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_TASK_META_H
#define BAIDU_BTHREAD_TASK_META_H

#include <pthread.h>                 // pthread_spin_init
#include "bthread/butex.h"           // butex_construct/destruct
#include "base/atomicops.h"          // base::atomic
#include "bthread/types.h"           // bthread_attr_t
#include "bthread/stack.h"           // Context, StackContainer

namespace bthread {

struct TaskStatistics {
    int64_t cputime_ns;
    int64_t nswitch;
};

struct KeyTable;
struct ButexWaiter;

struct LocalStorage {
    KeyTable* keytable;
    void* assigned_data;
    void* rpcz_parent_span;
};

#define BTHREAD_LOCAL_STORAGE_INITIALIZER { NULL, NULL, NULL }

const static LocalStorage LOCAL_STORAGE_INIT = BTHREAD_LOCAL_STORAGE_INITIALIZER;

struct TaskMeta {
    // [Not Reset]
    base::atomic<ButexWaiter*> current_waiter;
    uint64_t current_sleep;
    
    bool stop;
    bool interruptible;
    bool about_to_quit;
    
    // [Not Reset] guarantee visibility of version_butex.
    pthread_spinlock_t version_lock;
    
    // [Not Reset] only modified by one bthread at any time, no need to be atomic
    uint32_t* version_butex;

    // The identifier. It does not have to be here, however many code is
    // simplified if they can get tid from TaskMeta.
    bthread_t tid;

    // User function and argument
    void* (*fn)(void*);
    void* arg;

    StackContainer* stack_container;

    // Attributes creating this task
    bthread_attr_t attr;
    
    // Statistics
    int64_t cpuwide_start_ns;
    TaskStatistics stat;

    // bthread local storage.
    LocalStorage local_storage;
    
    // [Not Reset] The memory to construct `version_butex'
    char version_butex_memory[BUTEX_MEMORY_SIZE];

public:
    // Only initialize [Not Reset] fields, other fields will be reset in
    // bthread_start* functions
    TaskMeta()
        : current_waiter(NULL)
        , current_sleep(0)
        , stack_container(NULL) {
        pthread_spin_init(&version_lock, 0);
        version_butex = (uint32_t*)butex_construct(version_butex_memory);
        *version_butex = 1;
    }
        
    ~TaskMeta() {
        butex_destruct(version_butex_memory);
        version_butex = NULL;
        pthread_spin_destroy(&version_lock);
    }

    void set_stack(StackContainer* sc) {
        stack_container = sc;
    }

    StackContainer* release_stack() {
        StackContainer* tmp = stack_container;
        stack_container = NULL;
        return tmp;
    }

    StackType stack_type() const {
        return static_cast<StackType>(attr.stack_type);
    }
};

}  // namespace bthread

#endif  // BAIDU_BTHREAD_TASK_META_H
