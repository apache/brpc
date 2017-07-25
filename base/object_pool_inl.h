// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#ifndef BASE_OBJECT_POOL_INL_H
#define BASE_OBJECT_POOL_INL_H

#include <iostream>                      // std::ostream
#include <pthread.h>                     // pthread_mutex_t
#include <algorithm>                     // std::max, std::min
#include "base/atomicops.h"              // base::atomic
#include "base/macros.h"                 // BAIDU_CACHELINE_ALIGNMENT
#include "base/scoped_lock.h"            // BAIDU_SCOPED_LOCK
#include "base/thread_local.h"           // BAIDU_THREAD_LOCAL
#include <vector>

#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
#define BAIDU_OBJECT_POOL_FREE_ITEM_NUM_ADD1                    \
    (_global_nfree.fetch_add(1, base::memory_order_relaxed))
#define BAIDU_OBJECT_POOL_FREE_ITEM_NUM_SUB1                    \
    (_global_nfree.fetch_sub(1, base::memory_order_relaxed))
#else
#define BAIDU_OBJECT_POOL_FREE_ITEM_NUM_ADD1
#define BAIDU_OBJECT_POOL_FREE_ITEM_NUM_SUB1
#endif

namespace base {

template <typename T, size_t NITEM>
struct ObjectPoolFreeChunk {
    size_t nfree;
    T* ptrs[NITEM];
}; 

}  // namespace base

namespace base {

struct ObjectPoolInfo {
    size_t local_pool_num;
    size_t block_group_num;
    size_t block_num;
    size_t item_num;
    size_t block_item_num;
    size_t free_chunk_item_num;
    size_t total_size;
#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
    size_t free_item_num;
#endif
};

static const size_t OP_MAX_BLOCK_NGROUP = 65536;
static const size_t OP_GROUP_NBLOCK_NBIT = 16;
static const size_t OP_GROUP_NBLOCK = (1UL << OP_GROUP_NBLOCK_NBIT);
static const size_t OP_INITIAL_FREE_LIST_SIZE = 1024;

template <typename T>
class ObjectPoolBlockItemNum {
    static const size_t N1 = ObjectPoolBlockMaxSize<T>::value / sizeof(T);
    static const size_t N2 = (N1 < 1 ? 1 : N1);
public:
    static const size_t value = (N2 > ObjectPoolBlockMaxItem<T>::value ?
                                 ObjectPoolBlockMaxItem<T>::value : N2);
};

template <typename T>
class BAIDU_CACHELINE_ALIGNMENT ObjectPool {
public:
    static const size_t BLOCK_NITEM = ObjectPoolBlockItemNum<T>::value;
    static const size_t FREE_CHUNK_NITEM =
        ObjectPoolFreeChunkMaxItem<T>::value > 0 ?
        ObjectPoolFreeChunkMaxItem<T>::value : BLOCK_NITEM;

    // Free objects are batched in a FreeChunk before they're added to
    // global list(_free_chunks).
    typedef ObjectPoolFreeChunk<T, FREE_CHUNK_NITEM>    FreeChunk;

    // When a thread needs memory, it allocates a Block. To improve locality,
    // items in the Block are only used by the thread.
    struct BAIDU_CACHELINE_ALIGNMENT Block {
        size_t nitem;
        char items[sizeof(T) * BLOCK_NITEM];

        Block() : nitem(0) {}
    };

    // An Object addresses at most OP_MAX_BLOCK_NGROUP BlockGroups,
    // each BlockGroup addresses at most OP_GROUP_NBLOCK blocks. So an
    // object addresses at most OP_MAX_BLOCK_NGROUP * OP_GROUP_NBLOCK Blocks.
    struct BlockGroup {
        base::atomic<size_t> nblock;
        base::atomic<Block*> blocks[OP_GROUP_NBLOCK];

        BlockGroup() : nblock(0) {
            // We fetch_add nblock in add_block() before setting the entry,
            // thus address_resource() may sees the unset entry. Initialize
            // all entries to NULL makes such address_resource() return NULL.
            memset(blocks, 0, sizeof(base::atomic<Block*>) * OP_GROUP_NBLOCK);
        }
    };

    // Each thread has an instance of this class.
    class BAIDU_CACHELINE_ALIGNMENT LocalPool {
    public:
        explicit LocalPool(ObjectPool* pool)
            : _pool(pool)
            , _cur_block(NULL)
            , _cur_block_index(0) {
            _cur_free.nfree = 0;
        }

        ~LocalPool() {
            // Add to global _free if there're some free objects
            if (_cur_free.nfree) {
                _pool->push_free_chunk(_cur_free);
            }

            _pool->clear_from_destructor_of_local_pool();
        }

        static void delete_local_pool(void* arg) {
            delete (LocalPool*)arg;
        }

        // We need following macro to construct T with different CTOR_ARGS
        // which may include parenthesis because when T is POD, "new T()"
        // and "new T" are different: former one sets all fields to 0 which
        // we don't want.
#define BAIDU_OBJECT_POOL_GET(CTOR_ARGS)                                \
        /* Fetch local free ptr */                                      \
        if (_cur_free.nfree) {                                          \
            BAIDU_OBJECT_POOL_FREE_ITEM_NUM_SUB1;                       \
            return _cur_free.ptrs[--_cur_free.nfree];                   \
        }                                                               \
        /* Fetch a FreeChunk from global.                               \
           TODO: Popping from _free needs to copy a FreeChunk which is  \
           costly, but hardly impacts amortized performance. */         \
        if (_pool->pop_free_chunk(_cur_free)) {                         \
            BAIDU_OBJECT_POOL_FREE_ITEM_NUM_SUB1;                       \
            return _cur_free.ptrs[--_cur_free.nfree];                   \
        }                                                               \
        /* Fetch memory from local block */                             \
        if (_cur_block && _cur_block->nitem < BLOCK_NITEM) {            \
            T* obj = new ((T*)_cur_block->items + _cur_block->nitem) T CTOR_ARGS; \
            if (!ObjectPoolValidator<T>::validate(obj)) {               \
                obj->~T();                                              \
                return NULL;                                            \
            }                                                           \
            ++_cur_block->nitem;                                        \
            return obj;                                                 \
        }                                                               \
        /* Fetch a Block from global */                                 \
        _cur_block = add_block(&_cur_block_index);                      \
        if (_cur_block != NULL) {                                       \
            T* obj = new ((T*)_cur_block->items + _cur_block->nitem) T CTOR_ARGS; \
            if (!ObjectPoolValidator<T>::validate(obj)) {               \
                obj->~T();                                              \
                return NULL;                                            \
            }                                                           \
            ++_cur_block->nitem;                                        \
            return obj;                                                 \
        }                                                               \
        return NULL;                                                    \
 

        inline T* get() {
            BAIDU_OBJECT_POOL_GET();
        }

        template <typename A1>
        inline T* get(const A1& a1) {
            BAIDU_OBJECT_POOL_GET((a1));
        }

        template <typename A1, typename A2>
        inline T* get(const A1& a1, const A2& a2) {
            BAIDU_OBJECT_POOL_GET((a1, a2));
        }

#undef BAIDU_OBJECT_POOL_GET

        inline int return_object(T* ptr) {
            // Return to local free list
            if (_cur_free.nfree < ObjectPool::free_chunk_nitem()) {
                _cur_free.ptrs[_cur_free.nfree++] = ptr;
                BAIDU_OBJECT_POOL_FREE_ITEM_NUM_ADD1;
                return 0;
            }
            // Local free list is full, return it to global.
            // For copying issue, check comment in upper get()
            if (_pool->push_free_chunk(_cur_free)) {
                _cur_free.nfree = 1;
                _cur_free.ptrs[0] = ptr;
                BAIDU_OBJECT_POOL_FREE_ITEM_NUM_ADD1;
                return 0;
            }
            return -1;
        }

    private:
        ObjectPool* _pool;
        Block* _cur_block;
        size_t _cur_block_index;
        FreeChunk _cur_free;
    };

    inline T* get_object() {
        LocalPool* lp = get_or_new_local_pool();
        if (__builtin_expect(lp != NULL, 1)) {
            return lp->get();
        }
        return NULL;
    }

    template <typename A1>
    inline T* get_object(const A1& arg1) {
        LocalPool* lp = get_or_new_local_pool();
        if (__builtin_expect(lp != NULL, 1)) {
            return lp->get(arg1);
        }
        return NULL;
    }

    template <typename A1, typename A2>
    inline T* get_object(const A1& arg1, const A2& arg2) {
        LocalPool* lp = get_or_new_local_pool();
        if (__builtin_expect(lp != NULL, 1)) {
            return lp->get(arg1, arg2);
        }
        return NULL;
    }

    inline int return_object(T* ptr) {
        LocalPool* lp = get_or_new_local_pool();
        if (__builtin_expect(lp != NULL, 1)) {
            return lp->return_object(ptr);
        }
        return -1;
    }

    void clear_objects() {
        LocalPool* lp = _local_pool;
        if (lp) {
            _local_pool = NULL;
            base::thread_atexit_cancel(LocalPool::delete_local_pool, lp);
            delete lp;
        }
    }

    inline static size_t free_chunk_nitem() {
        const size_t n = ObjectPoolFreeChunkMaxItemDynamic<T>::value();
        return (!n ? FREE_CHUNK_NITEM : n);
    }

    // Number of all allocated objects, including being used and free.
    ObjectPoolInfo describe_objects() const {
        ObjectPoolInfo info;
        info.local_pool_num = _nlocal.load(base::memory_order_relaxed);
        info.block_group_num = _ngroup.load(base::memory_order_acquire);
        info.block_num = 0;
        info.item_num = 0;
        info.free_chunk_item_num = free_chunk_nitem();
        info.block_item_num = BLOCK_NITEM;
#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
        info.free_item_num = _global_nfree.load(base::memory_order_relaxed);
#endif

        for (size_t i = 0; i < info.block_group_num; ++i) {
            BlockGroup* bg = _block_groups[i].load(base::memory_order_consume);
            if (NULL == bg) {
                break;
            }
            size_t nblock = std::min(bg->nblock.load(base::memory_order_relaxed),
                                     OP_GROUP_NBLOCK);
            info.block_num += nblock;
            for (size_t j = 0; j < nblock; ++j) {
                Block* b = bg->blocks[j].load(base::memory_order_consume);
                if (NULL != b) {
                    info.item_num += b->nitem;
                }
            }
        }
        info.total_size = info.block_num * info.block_item_num * sizeof(T);
        return info;
    }

    static inline ObjectPool* singleton() {
        ObjectPool* p = _singleton.load(base::memory_order_consume);
        if (p) {
            return p;
        }
        pthread_mutex_lock(&_singleton_mutex);
        p = _singleton.load(base::memory_order_consume);
        if (!p) {
            p = new ObjectPool();
            _singleton.store(p, base::memory_order_release);
        }
        pthread_mutex_unlock(&_singleton_mutex);
        return p;
    }

private:
    ObjectPool() {
        _free_chunks.reserve(OP_INITIAL_FREE_LIST_SIZE);
        pthread_mutex_init(&_free_chunks_mutex, NULL);
    }

    ~ObjectPool() {
        pthread_mutex_destroy(&_free_chunks_mutex);
    }

    // Create a Block and append it to right-most BlockGroup.
    static Block* add_block(size_t* index) {
        Block* const new_block = new(std::nothrow) Block;
        if (NULL == new_block) {
            return NULL;
        }
        size_t ngroup;
        do {
            ngroup = _ngroup.load(base::memory_order_acquire);
            if (ngroup >= 1) {
                BlockGroup* const g =
                    _block_groups[ngroup - 1].load(base::memory_order_consume);
                const size_t block_index =
                    g->nblock.fetch_add(1, base::memory_order_relaxed);
                if (block_index < OP_GROUP_NBLOCK) {
                    g->blocks[block_index].store(
                        new_block, base::memory_order_release);
                    *index = (ngroup - 1) * OP_GROUP_NBLOCK + block_index;
                    return new_block;
                }
                g->nblock.fetch_sub(1, base::memory_order_relaxed);
            }
        } while (add_block_group(ngroup));

        // Fail to add_block_group.
        delete new_block;
        return NULL;
    }

    // Create a BlockGroup and append it to _block_groups.
    // Shall be called infrequently because a BlockGroup is pretty big.
    static bool add_block_group(size_t old_ngroup) {
        BlockGroup* bg = NULL;
        BAIDU_SCOPED_LOCK(_block_group_mutex);
        const size_t ngroup = _ngroup.load(base::memory_order_acquire);
        if (ngroup != old_ngroup) {
            // Other thread got lock and added group before this thread.
            return true;
        }
        if (ngroup < OP_MAX_BLOCK_NGROUP) {
            bg = new(std::nothrow) BlockGroup;
            if (NULL != bg) {
                // Release fence is paired with consume fence in add_block()
                // to avoid un-constructed bg to be seen by other threads.
                _block_groups[ngroup].store(bg, base::memory_order_release);
                _ngroup.store(ngroup + 1, base::memory_order_release);
            }
        }
        return bg != NULL;
    }

    inline LocalPool* get_or_new_local_pool() {
        LocalPool* lp = _local_pool;
        if (__builtin_expect(lp != NULL, 1)) {
            return lp;
        }
        lp = new(std::nothrow) LocalPool(this);
        if (NULL == lp) {
            return NULL;
        }
        BAIDU_SCOPED_LOCK(_change_thread_mutex); //avoid race with clear()
        _local_pool = lp;
        base::thread_atexit(LocalPool::delete_local_pool, lp);
        _nlocal.fetch_add(1, base::memory_order_relaxed);
        return lp;
    }

    void clear_from_destructor_of_local_pool() {
        // Remove tls
        _local_pool = NULL;

        // Do nothing if there're active threads.
        if (_nlocal.fetch_sub(1, base::memory_order_relaxed) != 1) {
            return;
        }

        // Can't delete global even if all threads(called ObjectPool
        // functions) quit because the memory may still be referenced by 
        // other threads. But we need to validate that all memory can
        // be deallocated correctly in tests, so wrap the function with 
        // a macro which is only defined in unittests.
#ifdef BAIDU_CLEAR_OBJECT_POOL_AFTER_ALL_THREADS_QUIT
        BAIDU_SCOPED_LOCK(_change_thread_mutex);  // including acquire fence.
        // Do nothing if there're active threads.
        if (_nlocal.load(base::memory_order_relaxed) != 0) {
            return;
        }
        // All threads exited and we're holding _change_thread_mutex to avoid
        // racing with new threads calling get_object().

        // Clear global free list.
        FreeChunk dummy;
        while (pop_free_chunk(dummy));

        // Delete all memory
        const size_t ngroup = _ngroup.exchange(0, base::memory_order_relaxed);
        for (size_t i = 0; i < ngroup; ++i) {
            BlockGroup* bg = _block_groups[i].load(base::memory_order_relaxed);
            if (NULL == bg) {
                break;
            }
            size_t nblock = std::min(bg->nblock.load(base::memory_order_relaxed),
                                     OP_GROUP_NBLOCK);
            for (size_t j = 0; j < nblock; ++j) {
                Block* b = bg->blocks[j].load(base::memory_order_relaxed);
                if (NULL == b) {
                    continue;
                }
                for (size_t k = 0; k < b->nitem; ++k) {
                    T* const objs = (T*)b->items;
                    objs[k].~T();
                }
                delete b;
            }
            delete bg;
        }

        memset(_block_groups, 0, sizeof(BlockGroup*) * OP_MAX_BLOCK_NGROUP);
#endif
    }

private:
    bool pop_free_chunk(FreeChunk& c) {
        // This is critical for high contention situation in which many threads
        // try to get_object but return in other threads
        if (_free_chunks.empty()) {
            return false;
        }
        pthread_mutex_lock(&_free_chunks_mutex);
        if (_free_chunks.empty()) {
            pthread_mutex_unlock(&_free_chunks_mutex);
            return false;
        }
        FreeChunk* p = _free_chunks.back();
        _free_chunks.pop_back();
        pthread_mutex_unlock(&_free_chunks_mutex);
        c = *p;
        delete p;
        return true;
    }

    bool push_free_chunk(const FreeChunk& c) {
        FreeChunk* c2 = new (std::nothrow) FreeChunk(c);
        if (NULL == c2) {
            return false;
        }
        pthread_mutex_lock(&_free_chunks_mutex);
        _free_chunks.push_back(c2);
        pthread_mutex_unlock(&_free_chunks_mutex);
        return true;
    }
    
    static base::static_atomic<ObjectPool*> _singleton;
    static pthread_mutex_t _singleton_mutex;
    static BAIDU_THREAD_LOCAL LocalPool* _local_pool;
    static base::static_atomic<long> _nlocal;
    static base::static_atomic<size_t> _ngroup;
    static pthread_mutex_t _block_group_mutex;
    static pthread_mutex_t _change_thread_mutex;
    static base::static_atomic<BlockGroup*> _block_groups[OP_MAX_BLOCK_NGROUP];

    std::vector<FreeChunk*> _free_chunks;
    pthread_mutex_t _free_chunks_mutex;

#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
    static base::static_atomic<size_t> _global_nfree;
#endif
};

// Declare template static variables:

template <typename T>
const size_t ObjectPool<T>::FREE_CHUNK_NITEM;

template <typename T>
BAIDU_THREAD_LOCAL typename ObjectPool<T>::LocalPool*
ObjectPool<T>::_local_pool = NULL;

template <typename T>
base::static_atomic<ObjectPool<T>*> ObjectPool<T>::_singleton = BASE_STATIC_ATOMIC_INIT(NULL);

template <typename T>
pthread_mutex_t ObjectPool<T>::_singleton_mutex = PTHREAD_MUTEX_INITIALIZER;

template <typename T>
static_atomic<long> ObjectPool<T>::_nlocal = BASE_STATIC_ATOMIC_INIT(0);

template <typename T>
base::static_atomic<size_t> ObjectPool<T>::_ngroup = BASE_STATIC_ATOMIC_INIT(0);

template <typename T>
pthread_mutex_t ObjectPool<T>::_block_group_mutex = PTHREAD_MUTEX_INITIALIZER;

template <typename T>
pthread_mutex_t ObjectPool<T>::_change_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

template <typename T>
base::static_atomic<typename ObjectPool<T>::BlockGroup*>
ObjectPool<T>::_block_groups[OP_MAX_BLOCK_NGROUP] = {};

#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
template <typename T>
base::static_atomic<size_t> ObjectPool<T>::_global_nfree = BASE_STATIC_ATOMIC_INIT(0);
#endif

inline std::ostream& operator<<(std::ostream& os,
                                ObjectPoolInfo const& info) {
    return os << "local_pool_num: " << info.local_pool_num
              << "\nblock_group_num: " << info.block_group_num
              << "\nblock_num: " << info.block_num
              << "\nitem_num: " << info.item_num
              << "\nblock_item_num: " << info.block_item_num
              << "\nfree_chunk_item_num: " << info.free_chunk_item_num
              << "\ntotal_size: " << info.total_size
#ifdef BASE_OBJECT_POOL_NEED_FREE_ITEM_NUM
              << "\nfree_num: " << info.free_item_num
#endif
        ;
}
}  // namespace base

#endif  // BASE_OBJECT_POOL_INL_H
