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

// bthread - A M:N threading library to make applications more concurrent.

// Date: Sun Aug  3 12:46:15 CST 2014

#include <pthread.h>
#include "butil/macros.h"
#include "butil/atomicops.h"
#include "bvar/passive_status.h"
#include "bthread/errno.h"                       // EAGAIN
#include "bthread/task_group.h"                  // TaskGroup

// Implement bthread_key_t related functions

namespace bthread {

class KeyTable;

// defined in task_group.cpp
extern __thread TaskGroup* tls_task_group;
extern __thread LocalStorage tls_bls;
static __thread bool tls_ever_created_keytable = false;

// We keep thread specific data in a two-level array. The top-level array
// contains at most KEY_1STLEVEL_SIZE pointers to dynamically allocated
// arrays of at most KEY_2NDLEVEL_SIZE data pointers. Many applications
// may just occupy one or two second level array, thus this machanism keeps
// memory footprint smaller and we can change KEY_1STLEVEL_SIZE to a
// bigger number more freely. The tradeoff is an additional memory indirection:
// negligible at most time.
static const uint32_t KEY_2NDLEVEL_SIZE = 32;

// Notice that we're trying to make the memory of second level and first
// level both 256 bytes to make memory allocator happier.
static const uint32_t KEY_1STLEVEL_SIZE = 31;

// Max tls in one thread, currently the value is 992 which should be enough
// for most projects throughout baidu. 
static const uint32_t KEYS_MAX = KEY_2NDLEVEL_SIZE * KEY_1STLEVEL_SIZE;

// destructors/version of TLS.
struct KeyInfo {
    uint32_t version;
    void (*dtor)(void*, const void*);
    const void* dtor_args;
};
static KeyInfo s_key_info[KEYS_MAX] = {};

// For allocating keys.
static pthread_mutex_t s_key_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t nfreekey = 0;
static size_t nkey = 0;
static uint32_t s_free_keys[KEYS_MAX];

// Stats.
static butil::static_atomic<size_t> nkeytable = BUTIL_STATIC_ATOMIC_INIT(0);
static butil::static_atomic<size_t> nsubkeytable = BUTIL_STATIC_ATOMIC_INIT(0);

// The second-level array.
// Align with cacheline to avoid false sharing.
class BAIDU_CACHELINE_ALIGNMENT SubKeyTable {
public:
    SubKeyTable() {
        memset(_data, 0, sizeof(_data));
        nsubkeytable.fetch_add(1, butil::memory_order_relaxed);
    }

    // NOTE: Call clear first.
    ~SubKeyTable() {
        nsubkeytable.fetch_sub(1, butil::memory_order_relaxed);
    }

    void clear(uint32_t offset) {
        for (uint32_t i = 0; i < KEY_2NDLEVEL_SIZE; ++i) {
            void* p = _data[i].ptr;
            if (p) {
                // Set the position to NULL before calling dtor which may set
                // the position again.
                _data[i].ptr = NULL;
                
                KeyInfo info = bthread::s_key_info[offset + i];
                if (info.dtor && _data[i].version == info.version) {
                    info.dtor(p, info.dtor_args);
                }
            }
        }
    }

    bool cleared() const {
        // We need to iterate again to check if every slot is empty. An
        // alternative is remember if set_data() was called during clear.
        for (uint32_t i = 0; i < KEY_2NDLEVEL_SIZE; ++i) {
            if (_data[i].ptr) {
                return false;
            }
        }
        return true;
    }

    inline void* get_data(uint32_t index, uint32_t version) const {
        if (_data[index].version == version) {
            return _data[index].ptr;
        }
        return NULL;
    }
    inline void set_data(uint32_t index, uint32_t version, void* data) {
        _data[index].version = version;
        _data[index].ptr = data;
    }

private:
    struct Data {
        uint32_t version;
        void* ptr;
    };
    Data _data[KEY_2NDLEVEL_SIZE];
};

// The first-level array.
// Align with cacheline to avoid false sharing.
class BAIDU_CACHELINE_ALIGNMENT KeyTable {
public:
    KeyTable() : next(NULL) {
        memset(_subs, 0, sizeof(_subs));
        nkeytable.fetch_add(1, butil::memory_order_relaxed);
    }

    ~KeyTable() {
        nkeytable.fetch_sub(1, butil::memory_order_relaxed);
        for (int ntry = 0; ntry < PTHREAD_DESTRUCTOR_ITERATIONS; ++ntry) {
            for (uint32_t i = 0; i < KEY_1STLEVEL_SIZE; ++i) {
                if (_subs[i]) {
                    _subs[i]->clear(i * KEY_2NDLEVEL_SIZE);
                }
            }
            bool all_cleared = true;
            for (uint32_t i = 0; i < KEY_1STLEVEL_SIZE; ++i) {
                if (_subs[i] != NULL && !_subs[i]->cleared()) {
                    all_cleared = false;
                    break;
                }
            }
            if (all_cleared) {
                for (uint32_t i = 0; i < KEY_1STLEVEL_SIZE; ++i) {
                    delete _subs[i];
                }
                return;
            }
        }
        LOG(ERROR) << "Fail to destroy all objects in KeyTable[" << this << ']';
    }

    inline void* get_data(bthread_key_t key) const {
        const uint32_t subidx = key.index / KEY_2NDLEVEL_SIZE;
        if (subidx < KEY_1STLEVEL_SIZE) {
            const SubKeyTable* sub_kt = _subs[subidx];
            if (sub_kt) {
                return sub_kt->get_data(
                    key.index - subidx * KEY_2NDLEVEL_SIZE, key.version);
            }
        }
        return NULL;
    }

    inline int set_data(bthread_key_t key, void* data) {
        const uint32_t subidx = key.index / KEY_2NDLEVEL_SIZE;
        if (subidx < KEY_1STLEVEL_SIZE &&
            key.version == s_key_info[key.index].version) {
            SubKeyTable* sub_kt = _subs[subidx];
            if (sub_kt == NULL) {
                sub_kt = new (std::nothrow) SubKeyTable;
                if (NULL == sub_kt) {
                    return ENOMEM;
                }
                _subs[subidx] = sub_kt;
            }
            sub_kt->set_data(key.index - subidx * KEY_2NDLEVEL_SIZE,
                             key.version, data);
            return 0;
        }
        CHECK(false) << "bthread_setspecific is called on invalid " << key;
        return EINVAL;
    }

public:
    KeyTable* next;
private:
    SubKeyTable* _subs[KEY_1STLEVEL_SIZE];
};

static KeyTable* borrow_keytable(bthread_keytable_pool_t* pool) {
    if (pool != NULL && pool->free_keytables) {
        BAIDU_SCOPED_LOCK(pool->mutex);
        KeyTable* p = (KeyTable*)pool->free_keytables;
        if (p) {
            pool->free_keytables = p->next;
            return p;
        }
    }
    return NULL;
}

// Referenced in task_group.cpp, must be extern.
// Caller of this function must hold the KeyTable
void return_keytable(bthread_keytable_pool_t* pool, KeyTable* kt) {
    if (NULL == kt) {
        return;
    }
    if (pool == NULL) {
        delete kt;
        return;
    }
    std::unique_lock<pthread_mutex_t> mu(pool->mutex);
    if (pool->destroyed) {
        mu.unlock();
        delete kt;
        return;
    }
    kt->next = (KeyTable*)pool->free_keytables;
    pool->free_keytables = kt;
}

static void cleanup_pthread(void* arg) {
    KeyTable* kt = static_cast<KeyTable*>(arg);
    if (kt) {
        delete kt;
        // After deletion: tls may be set during deletion.
        tls_bls.keytable = NULL;
    }
}

static void arg_as_dtor(void* data, const void* arg) {
    typedef void (*KeyDtor)(void*);
    return ((KeyDtor)arg)(data);
}

static int get_key_count(void*) {
    BAIDU_SCOPED_LOCK(bthread::s_key_mutex);
    return (int)nkey - (int)nfreekey;
}
static size_t get_keytable_count(void*) {
    return nkeytable.load(butil::memory_order_relaxed);
}
static size_t get_keytable_memory(void*) {
    const size_t n = nkeytable.load(butil::memory_order_relaxed);
    const size_t nsub = nsubkeytable.load(butil::memory_order_relaxed);
    return n * sizeof(KeyTable) + nsub * sizeof(SubKeyTable);
}

static bvar::PassiveStatus<int> s_bthread_key_count(
    "bthread_key_count", get_key_count, NULL);
static bvar::PassiveStatus<size_t> s_bthread_keytable_count(
    "bthread_keytable_count", get_keytable_count, NULL);
static bvar::PassiveStatus<size_t> s_bthread_keytable_memory(
    "bthread_keytable_memory", get_keytable_memory, NULL);

}  // namespace bthread

extern "C" {

int bthread_keytable_pool_init(bthread_keytable_pool_t* pool) {
    if (pool == NULL) {
        LOG(ERROR) << "Param[pool] is NULL";
        return EINVAL;
    }
    pthread_mutex_init(&pool->mutex, NULL);
    pool->free_keytables = NULL;
    pool->destroyed = 0;
    return 0;
}

int bthread_keytable_pool_destroy(bthread_keytable_pool_t* pool) {
    if (pool == NULL) {
        LOG(ERROR) << "Param[pool] is NULL";
        return EINVAL;
    }
    bthread::KeyTable* saved_free_keytables = NULL;
    {
        BAIDU_SCOPED_LOCK(pool->mutex);
        if (pool->free_keytables) {
            saved_free_keytables = (bthread::KeyTable*)pool->free_keytables;
            pool->free_keytables = NULL;
        }
        pool->destroyed = 1;
    }
    // Cheat get/setspecific and destroy the keytables.
    bthread::TaskGroup* const g = bthread::tls_task_group;
    bthread::KeyTable* old_kt = bthread::tls_bls.keytable;
    while (saved_free_keytables) {
        bthread::KeyTable* kt = saved_free_keytables;
        saved_free_keytables = kt->next;
        bthread::tls_bls.keytable = kt;
        if (g) {
            g->current_task()->local_storage.keytable = kt;
        }
        delete kt;
        if (old_kt == kt) {
            old_kt = NULL;
        }
    }
    bthread::tls_bls.keytable = old_kt;
    if (g) {
        g->current_task()->local_storage.keytable = old_kt;
    }
    // TODO: return_keytable may race with this function, we don't destroy
    // the mutex right now.
    // pthread_mutex_destroy(&pool->mutex);
    return 0;
}

int bthread_keytable_pool_getstat(bthread_keytable_pool_t* pool,
                                  bthread_keytable_pool_stat_t* stat) {
    if (pool == NULL || stat == NULL) {
        LOG(ERROR) << "Param[pool] or Param[stat] is NULL";
        return EINVAL;
    }
    std::unique_lock<pthread_mutex_t> mu(pool->mutex);
    size_t count = 0;
    bthread::KeyTable* p = (bthread::KeyTable*)pool->free_keytables;
    for (; p; p = p->next, ++count) {}
    stat->nfree = count;
    return 0;
}

// TODO: this is not strict `reserve' because we only check #free.
// Currently there's no way to track KeyTables that may be returned
// to the pool in future.
void bthread_keytable_pool_reserve(bthread_keytable_pool_t* pool,
                                   size_t nfree,
                                   bthread_key_t key,
                                   void* ctor(const void*),
                                   const void* ctor_args) {
    if (pool == NULL) {
        LOG(ERROR) << "Param[pool] is NULL";
        return;
    }
    bthread_keytable_pool_stat_t stat;
    if (bthread_keytable_pool_getstat(pool, &stat) != 0) {
        LOG(ERROR) << "Fail to getstat of pool=" << pool;
        return;
    }
    for (size_t i = stat.nfree; i < nfree; ++i) {
        bthread::KeyTable* kt = new (std::nothrow) bthread::KeyTable;
        if (kt == NULL) {
            break;
        }
        void* data = ctor(ctor_args);
        if (data) {
            kt->set_data(key, data);
        }  // else append kt w/o data.

        std::unique_lock<pthread_mutex_t> mu(pool->mutex);
        if (pool->destroyed) {
            mu.unlock();
            delete kt;
            break;
        }
        kt->next = (bthread::KeyTable*)pool->free_keytables;
        pool->free_keytables = kt;
        if (data == NULL) {
            break;
        }
    }
}

int bthread_key_create2(bthread_key_t* key,
                        void (*dtor)(void*, const void*),
                        const void* dtor_args) {
    uint32_t index = 0;
    {
        BAIDU_SCOPED_LOCK(bthread::s_key_mutex);
        if (bthread::nfreekey > 0) {
            index = bthread::s_free_keys[--bthread::nfreekey];
        } else if (bthread::nkey < bthread::KEYS_MAX) {
            index = bthread::nkey++;
        } else {
            return EAGAIN;  // what pthread_key_create returns in this case.
        }
    }
    bthread::s_key_info[index].dtor = dtor;
    bthread::s_key_info[index].dtor_args = dtor_args;
    key->index = index;
    key->version = bthread::s_key_info[index].version;
    if (key->version == 0) {
        ++bthread::s_key_info[index].version;
        ++key->version;
    }
    return 0;
}

int bthread_key_create(bthread_key_t* key, void (*dtor)(void*)) {
    if (dtor == NULL) {
        return bthread_key_create2(key, NULL, NULL);
    } else {
        return bthread_key_create2(key, bthread::arg_as_dtor, (const void*)dtor);
    }
}

int bthread_key_delete(bthread_key_t key) {
    if (key.index < bthread::KEYS_MAX &&
        key.version == bthread::s_key_info[key.index].version) {
        BAIDU_SCOPED_LOCK(bthread::s_key_mutex);
        if (key.version == bthread::s_key_info[key.index].version) {
            if (++bthread::s_key_info[key.index].version == 0) {
                ++bthread::s_key_info[key.index].version;
            }
            bthread::s_key_info[key.index].dtor = NULL;
            bthread::s_key_info[key.index].dtor_args = NULL;        
            bthread::s_free_keys[bthread::nfreekey++] = key.index;
            return 0;
        } 
    }
    CHECK(false) << "bthread_key_delete is called on invalid " << key;
    return EINVAL;
}

// NOTE: Can't borrow_keytable in bthread_setspecific, otherwise following
// memory leak may occur:
//  -> bthread_getspecific fails to borrow_keytable and returns NULL.
//  -> bthread_setspecific succeeds to borrow_keytable and overwrites old data
//     at the position with newly created data, the old data is leaked.
int bthread_setspecific(bthread_key_t key, void* data) {
    bthread::KeyTable* kt = bthread::tls_bls.keytable;
    if (NULL == kt) {
        kt = new (std::nothrow) bthread::KeyTable;
        if (NULL == kt) {
            return ENOMEM;
        }
        bthread::tls_bls.keytable = kt;
        bthread::TaskGroup* const g = bthread::tls_task_group;
        if (g) {
            g->current_task()->local_storage.keytable = kt;
        } else {
            // Only cleanup keytable created by pthread.
            // keytable created by bthread will be deleted
            // in `return_keytable' or `bthread_keytable_pool_destroy'.
            if (!bthread::tls_ever_created_keytable) {
                bthread::tls_ever_created_keytable = true;
                CHECK_EQ(0, butil::thread_atexit(bthread::cleanup_pthread, kt));
            }
        }
    }
    return kt->set_data(key, data);
}

void* bthread_getspecific(bthread_key_t key) {
    bthread::KeyTable* kt = bthread::tls_bls.keytable;
    if (kt) {
        return kt->get_data(key);
    }
    bthread::TaskGroup* const g = bthread::tls_task_group;
    if (g) {
        bthread::TaskMeta* const task = g->current_task();
        kt = bthread::borrow_keytable(task->attr.keytable_pool);
        if (kt) {
            g->current_task()->local_storage.keytable = kt;
            bthread::tls_bls.keytable = kt;
            return kt->get_data(key);
        }
    }
    return NULL;
}

void bthread_assign_data(void* data) {
    bthread::tls_bls.assigned_data = data;
}

void* bthread_get_assigned_data() {
    return bthread::tls_bls.assigned_data;
}

}  // extern "C"
