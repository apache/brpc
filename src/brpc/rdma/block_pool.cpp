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

#if BRPC_WITH_RDMA

#include <errno.h>
#include <stdlib.h>
#include <vector>
#include <gflags/gflags.h>
#include "butil/fast_rand.h"
#include "butil/iobuf.h"
#include "butil/object_pool.h"
#include "butil/thread_local.h"
#include "bthread/bthread.h"
#include "brpc/rdma/block_pool.h"


namespace brpc {
namespace rdma {

DEFINE_int32(rdma_memory_pool_initial_size_mb, 1024,
             "Initial size of memory pool for RDMA (MB)");
DEFINE_int32(rdma_memory_pool_increase_size_mb, 1024,
             "Increased size of memory pool for RDMA (MB)");
DEFINE_int32(rdma_memory_pool_max_regions, 4, "Max number of regions");
DEFINE_int32(rdma_memory_pool_buckets, 4, "Number of buckets to reduce race");
DEFINE_int32(rdma_memory_pool_tls_cache_num, 128, "Number of cached block in tls");

static RegisterCallback g_cb = NULL;

// Number of bytes in 1MB
static const size_t BYTES_IN_MB = 1048576;

static const int BLOCK_DEFAULT = 0; // 8KB
static const int BLOCK_LARGE = 1;  // 64KB
static const int BLOCK_HUGE = 2;  // 2MB
static const int BLOCK_SIZE_COUNT = 3;
static size_t g_block_size[BLOCK_SIZE_COUNT] = { 8192, 65536, 2 * BYTES_IN_MB };

struct IdleNode {
    void* start;
    size_t len;
    IdleNode* next;
};

struct Region {
    Region() { start = 0; }
    uintptr_t start;
    size_t size;
    uint32_t block_type;
    uint32_t id;  // lkey
};

static const int32_t RDMA_MEMORY_POOL_MIN_REGIONS = 1;
static const int32_t RDMA_MEMORY_POOL_MAX_REGIONS = 16;
static Region g_regions[RDMA_MEMORY_POOL_MAX_REGIONS];
static int g_region_num = 0;

static const int32_t RDMA_MEMORY_POOL_MIN_SIZE = 32;  // 16MB
static const int32_t RDMA_MEMORY_POOL_MAX_SIZE = 1048576;  // 1TB

static const int32_t RDMA_MEMORY_POOL_MIN_BUCKETS = 1;
static const int32_t RDMA_MEMORY_POOL_MAX_BUCKETS = 16;
static size_t g_buckets = 1;

static bool g_dump_enable = false;
static butil::Mutex* g_dump_mutex = NULL;

// Only for default block size
static __thread IdleNode* tls_idle_list = NULL;
static __thread size_t tls_idle_num = 0;
static __thread bool tls_inited = false;
static butil::Mutex* g_tls_info_mutex = NULL;
static size_t g_tls_info_cnt = 0;
static size_t* g_tls_info[1024];

// For each block size, there are some buckets of idle list to reduce race.
struct GlobalInfo {
    std::vector<IdleNode*> idle_list[BLOCK_SIZE_COUNT];
    std::vector<butil::Mutex*> lock[BLOCK_SIZE_COUNT];
    std::vector<size_t> idle_size[BLOCK_SIZE_COUNT];
    butil::Mutex extend_lock;
};
static GlobalInfo* g_info = NULL;

static inline Region* GetRegion(const void* buf) {
    if (!buf) {
        errno = EINVAL;
        return NULL;
    }
    Region* r = NULL;
    uintptr_t addr = (uintptr_t)buf;
    for (int i = 0; i < FLAGS_rdma_memory_pool_max_regions; ++i) {
        if (g_regions[i].start == 0) {
            break;
        }
        if (addr >= g_regions[i].start &&
            addr < g_regions[i].start + g_regions[i].size) {
            r = &g_regions[i];
            break;
        }
    }
    return r;
}

uint32_t GetRegionId(const void* buf) {
    Region* r = GetRegion(buf);
    if (!r) {
        return 0;
    }
    return r->id;
}

// Extend the block pool with a new region (with different region ID)
static void* ExtendBlockPool(size_t region_size, int block_type) {
    if (region_size < 1) {
        errno = EINVAL;
        return NULL;
    }

    if (g_region_num == FLAGS_rdma_memory_pool_max_regions) {
        LOG(INFO) << "Memory pool reaches max regions";
        errno = ENOMEM;
        return NULL;
    }

    // Regularize region size
    region_size = region_size * BYTES_IN_MB / g_block_size[block_type] / g_buckets;
    region_size *= g_block_size[block_type] * g_buckets;

    LOG(INFO) << "Start extend rdma memory " << region_size / BYTES_IN_MB << "MB";

    void* region_base = NULL;
    if (posix_memalign(&region_base, 4096, region_size) != 0) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        return NULL;
    }

    uint32_t id = g_cb(region_base, region_size);
    if (id == 0) {
        free(region_base);
        return NULL;
    }

    IdleNode* node[g_buckets];
    for (size_t i = 0; i < g_buckets; ++i) {
        node[i] = butil::get_object<IdleNode>();
        if (!node[i]) {
            PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
            for (size_t j = 0; j < i; ++j) {
                butil::return_object<IdleNode>(node[j]);
            }
            free(region_base);
            return NULL;
        }
    }
 
    Region* region = &g_regions[g_region_num++];
    region->start = (uintptr_t)region_base;
    region->size = region_size;
    region->id = id;
    region->block_type = block_type;

    for (size_t i = 0; i < g_buckets; ++i) {
        node[i]->start = (void*)(region->start + i * (region_size / g_buckets));
        node[i]->len = region_size / g_buckets;
        node[i]->next = NULL;
        g_info->idle_list[block_type][i] = node[i];
        g_info->idle_size[block_type][i] += node[i]->len;
    }

    return region_base;
}

void* InitBlockPool(RegisterCallback cb) {
    if (!cb) {
        errno = EINVAL;
        return NULL;
    }
    if (g_cb) {
        LOG(WARNING) << "Do not initialize block pool repeatedly";
        errno = EINVAL;
        return NULL;
    }
    g_cb = cb;
    if (FLAGS_rdma_memory_pool_max_regions < RDMA_MEMORY_POOL_MIN_REGIONS ||
        FLAGS_rdma_memory_pool_max_regions > RDMA_MEMORY_POOL_MAX_REGIONS) {
        LOG(WARNING) << "rdma_memory_pool_max_regions("
                     << FLAGS_rdma_memory_pool_max_regions << ") not in ["
                     << RDMA_MEMORY_POOL_MIN_REGIONS << ","
                     << RDMA_MEMORY_POOL_MAX_REGIONS << "]!";
        errno = EINVAL;
        return NULL;
    }
    if (FLAGS_rdma_memory_pool_initial_size_mb < RDMA_MEMORY_POOL_MIN_SIZE ||
        FLAGS_rdma_memory_pool_initial_size_mb > RDMA_MEMORY_POOL_MAX_SIZE) {
        LOG(WARNING) << "rdma_memory_pool_initial_size_mb("
                     << FLAGS_rdma_memory_pool_initial_size_mb << ") not in ["
                     << RDMA_MEMORY_POOL_MIN_SIZE << ","
                     << RDMA_MEMORY_POOL_MAX_SIZE << "]!";
        errno = EINVAL;
        return NULL;
    }
    if (FLAGS_rdma_memory_pool_increase_size_mb < RDMA_MEMORY_POOL_MIN_SIZE ||
        FLAGS_rdma_memory_pool_increase_size_mb > RDMA_MEMORY_POOL_MAX_SIZE) {
        LOG(WARNING) << "rdma_memory_pool_increase_size_mb("
                     << FLAGS_rdma_memory_pool_increase_size_mb << ") not in ["
                     << RDMA_MEMORY_POOL_MIN_SIZE << ","
                     << RDMA_MEMORY_POOL_MAX_SIZE << "]!";
        errno = EINVAL;
        return NULL;
    }
    if (FLAGS_rdma_memory_pool_buckets < RDMA_MEMORY_POOL_MIN_BUCKETS ||
        FLAGS_rdma_memory_pool_buckets > RDMA_MEMORY_POOL_MAX_BUCKETS) {
        LOG(WARNING) << "rdma_memory_pool_buckets("
                     << FLAGS_rdma_memory_pool_buckets << ") not in ["
                     << RDMA_MEMORY_POOL_MIN_BUCKETS << ","
                     << RDMA_MEMORY_POOL_MAX_BUCKETS << "]!";
        errno = EINVAL;
        return NULL;
    }
    g_buckets = FLAGS_rdma_memory_pool_buckets;

    g_info = new (std::nothrow) GlobalInfo;
    if (!g_info) {
        return NULL;
    }

    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        g_info->idle_list[i].resize(g_buckets, NULL);
        if (g_info->idle_list[i].size() != g_buckets) {
            return NULL;
        }
        g_info->lock[i].resize(g_buckets, NULL);
        if (g_info->lock[i].size() != g_buckets) {
            return NULL;
        }
        g_info->idle_size[i].resize(g_buckets, 0);
        if (g_info->idle_size[i].size() != g_buckets) {
            return NULL;
        }
        for (size_t j = 0; j < g_buckets; ++j) {
            g_info->lock[i][j] = new (std::nothrow) butil::Mutex;
            if (!g_info->lock[i][j]) {
                return NULL;
            }
        }
    }

    g_dump_mutex = new butil::Mutex;
    g_tls_info_mutex = new butil::Mutex;

    return ExtendBlockPool(FLAGS_rdma_memory_pool_initial_size_mb,
                           BLOCK_DEFAULT);
}

static void* AllocBlockFrom(int block_type) {
    bool locked = false;
    if (BAIDU_UNLIKELY(g_dump_enable)) {
        g_dump_mutex->lock();
        locked = true;
    }
    void* ptr = NULL;
    if (block_type == 0 && tls_idle_list != NULL){
        CHECK(tls_idle_num > 0);
        IdleNode* n = tls_idle_list;
        tls_idle_list = n->next;
        ptr = n->start;
        butil::return_object<IdleNode>(n);
        tls_idle_num--;
        if (locked) {
            g_dump_mutex->unlock();
        }
        return ptr;
    }

    uint64_t index = butil::fast_rand() % g_buckets;
    BAIDU_SCOPED_LOCK(*g_info->lock[block_type][index]);
    IdleNode* node = g_info->idle_list[block_type][index];
    if (!node) {
        BAIDU_SCOPED_LOCK(g_info->extend_lock);
        node = g_info->idle_list[block_type][index];
        if (!node) {
            // There is no block left, extend a new region
            if (!ExtendBlockPool(FLAGS_rdma_memory_pool_increase_size_mb,
                                 block_type)) {
                LOG_EVERY_SECOND(ERROR) << "Fail to extend new region. "
                                        << "You can set the size of memory pool larger. "
                                        << "Refer to the help message of these flags: "
                                        << "rdma_memory_pool_initial_size_mb, "
                                        << "rdma_memory_pool_increase_size_mb, "
                                        << "rdma_memory_pool_max_regions.";
                if (locked) {
                    g_dump_mutex->unlock();
                }
                return NULL;
            }
            node = g_info->idle_list[block_type][index];
        }
    }
    if (node) {
        ptr = node->start;
        if (node->len > g_block_size[block_type]) {
            node->start = (char*)node->start + g_block_size[block_type];
            node->len -= g_block_size[block_type];
        } else {
            g_info->idle_list[block_type][index] = node->next;
            butil::return_object<IdleNode>(node);
        }
        g_info->idle_size[block_type][index] -= g_block_size[block_type];
    } else {
        if (locked) {
            g_dump_mutex->unlock();
        }
        return NULL;
    }

    // Move more blocks from global list to tls list
    if (block_type == 0) {
        node = g_info->idle_list[0][index];
        tls_idle_list = node;
        IdleNode* last_node = NULL;
        while (node) {
            if (tls_idle_num > (uint32_t)FLAGS_rdma_memory_pool_tls_cache_num / 2
                    || node->len > g_block_size[0]) {
                break;
            }
            tls_idle_num++;
            last_node = node;
            node = node->next;
        }
        if (tls_idle_num == 0) {
            tls_idle_list = NULL;
        } else {
            g_info->idle_list[0][index] = node;
        }
        if (last_node) {
            last_node->next = NULL;
        }
    }

    if (locked) {
        g_dump_mutex->unlock();
    }
    return ptr;
}

void* AllocBlock(size_t size) {
    if (size == 0 || size > g_block_size[BLOCK_SIZE_COUNT - 1]) {
        errno = EINVAL;
        return NULL;
    }
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        if (size <= g_block_size[i]) {
            return AllocBlockFrom(i);;
        }
    }
    return NULL;
}

void RecycleAll() {
    // Only block_type == 0 needs recycle
    while (tls_idle_list) {
        IdleNode* node = tls_idle_list;
        tls_idle_list = node->next;
        Region* r = GetRegion(node->start);
        uint64_t index = ((uintptr_t)node->start - r->start) * g_buckets / r->size;
        BAIDU_SCOPED_LOCK(*g_info->lock[0][index]);
        node->next = g_info->idle_list[0][index];
        g_info->idle_list[0][index] = node;
    }
    tls_idle_num = 0;
}

int DeallocBlock(void* buf) {
    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    Region* r = GetRegion(buf);
    if (!r) {
        errno = ERANGE;
        return -1;
    }

    IdleNode* node = butil::get_object<IdleNode>();
    if (!node) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        // May lead to block leak, but do not return -1
        return 0;
    }

    uint32_t block_type = r->block_type;
    size_t block_size = g_block_size[block_type];
    node->start = buf;
    node->len = block_size;

    bool locked = false;
    if (BAIDU_UNLIKELY(g_dump_enable)) {
        g_dump_mutex->lock();
        locked = true;
    }
    if (block_type == 0 && tls_idle_num < (uint32_t)FLAGS_rdma_memory_pool_tls_cache_num) {
        if (!tls_inited) {
            tls_inited = true;
            butil::thread_atexit(RecycleAll);
            BAIDU_SCOPED_LOCK(*g_tls_info_mutex);
            if (g_tls_info_cnt < 1024) {
                g_tls_info[g_tls_info_cnt++] = &tls_idle_num;
            }
        }
        tls_idle_num++;
        node->next = tls_idle_list;
        tls_idle_list = node;
        if (locked) {
            g_dump_mutex->unlock();
        }
        return 0;
    }

    uint64_t index = ((uintptr_t)buf - r->start) * g_buckets / r->size;
    if (block_type == 0) {
        size_t len = 0;
        // Recycle half the cached blocks in tls for default block size
        int num = FLAGS_rdma_memory_pool_tls_cache_num / 2;
        IdleNode* new_head = tls_idle_list;
        IdleNode* recycle_tail = NULL;
        for (int i = 0; i < num; ++i) {
            recycle_tail = new_head;
            len += recycle_tail->len;
            new_head = new_head->next;
        }
        if (recycle_tail) {
            BAIDU_SCOPED_LOCK(*g_info->lock[0][index]);
            recycle_tail->next = node;
            node->next = g_info->idle_list[0][index];
            g_info->idle_list[0][index] = tls_idle_list;
            g_info->idle_size[0][index] += len;
        }
        tls_idle_list = new_head;
        tls_idle_num -= num;
    } else {
        BAIDU_SCOPED_LOCK(*g_info->lock[block_type][index]);
        node->next = g_info->idle_list[block_type][index];
        g_info->idle_list[block_type][index] = node;
        g_info->idle_size[block_type][index] += node->len;
    }
    if (locked) {
        g_dump_mutex->unlock();
    }
    return 0;
}

size_t GetBlockSize(int type) {
    return g_block_size[type];
}

void DumpMemoryPoolInfo(std::ostream& os) {
    if (!g_dump_mutex) {
        return;
    }
    g_dump_enable = true;
    usleep(1000); // wait until all the threads read new g_dump_enable
    BAIDU_SCOPED_LOCK(*g_dump_mutex);
    os << "********************* Memory Pool Info Dump **********************\n";
    os << "Region Info:\n";
    for (int i = 0; i < g_region_num; ++i) {
        os << "\tRegion " << i << ":\n"
           << "\t\tBase Addr: " << g_regions[i].start << "\n"
           << "\t\tSize: " << g_regions[i].size << "\n"
           << "\t\tBlock Type: " << g_regions[i].block_type << "\n"
           << "\t\tId: " << g_regions[i].id << "\n";
    }
    os << "Idle List Info:\n";
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        os << "\tFor block size " << GetBlockSize(i) << ":\n";
        for (size_t j = 0; j < g_buckets; ++j) {
            os << "\t\tBucket " << j << ": " << g_info->idle_size[i][j] << "\n";
        }
    }
    os << "Thread Local Cache Info:\n";
    for (size_t i = 0; i < g_tls_info_cnt; ++i) {
        os << "\tThread " << i << ": " << *g_tls_info[i] * 8192 << "\n";
    }
    os << "******************************************************************\n";
    g_dump_enable = false;
}

// Just for UT
void DestroyBlockPool() {
    RecycleAll();
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        for (size_t j = 0; j < g_buckets; ++j) {
            IdleNode* node = g_info->idle_list[i][j];
            while (node) {
                IdleNode* tmp = node->next;
                butil::return_object<IdleNode>(node);
                node = tmp;
            }
            g_info->idle_list[i][j] = NULL;
        }
    }
    delete g_info;
    g_info = NULL;
    for (int i = 0; i < g_region_num; ++i) {
        if (g_regions[i].start == 0) {
            break;
        }
        free((void*)g_regions[i].start);
        g_regions[i].start = 0;
    }
    g_region_num = 0;
    g_cb = NULL;
}

// Just for UT
int GetBlockType(void* buf) {
    Region* r = GetRegion(buf);
    if (!r) {
        return -1;
    }
    return r->block_type;
}

// Just for UT
size_t GetGlobalLen(int block_type) {
    size_t len = 0;
    for (size_t i = 0; i < g_buckets; ++i) {
        IdleNode* node = g_info->idle_list[block_type][i];
        while (node) {
            len += node->len;
            node = node->next;
        }
    }
    return len;
}

// Just for UT
size_t GetRegionNum() {
    return g_region_num;
}

}  // namespace rdma
}  // namespace brpc

#endif  // if BRPC_WITH_RDMA
