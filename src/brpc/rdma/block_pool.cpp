// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA
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

// Number of bytes in 1MB
static const size_t BYTES_IN_MB = 1048576;

DEFINE_int32(rdma_memory_pool_initial_size_mb, 1024,
             "Initial size of memory pool for RDMA (MB)");
DEFINE_int32(rdma_memory_pool_increase_size_mb, 1024,
             "Increased size of memory pool for RDMA (MB)");
DEFINE_int32(rdma_memory_pool_max_regions, 4, "Max number of regions");
DEFINE_int32(rdma_memory_pool_buckets, 1, "Number of buckets to reduce race");
DEFINE_int32(rdma_memory_pool_tls_cache_num, 256, "Number of cached block in tls");

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

static const int MAX_REGIONS = 16;

static int g_buckets = 1;
static int g_max_regions = MAX_REGIONS;
static Region g_regions[MAX_REGIONS];
static int g_region_num = 0;

// This callback is used when extending a new region
typedef uint32_t (*Callback)(void*, size_t);
static Callback g_cb = NULL;

static const int BLOCK_DEFAULT = 0; // 8KB
static const int BLOCK_LARGE = 1;  // 64KB
static const int BLOCK_HUGE = 2;  // 2MB
static const int BLOCK_SIZE_COUNT = 3;
static size_t g_block_size[BLOCK_SIZE_COUNT];

// Only for default block size
static __thread IdleNode* tls_idle_list = NULL;
static __thread size_t tls_idle_num = 0;
static __thread bool tls_inited = false;

// For each block size, there are some buckets of idle list to reduce race.
struct GlobalInfo {
    std::vector<IdleNode*> idle_list[BLOCK_SIZE_COUNT];
    std::vector<butil::Mutex*> lock[BLOCK_SIZE_COUNT];
    butil::Mutex extend_lock;
    IdleNode* ready_list[BLOCK_SIZE_COUNT];
};
static GlobalInfo* g_info = NULL;

static inline Region* GetRegion(const void* buf) {
    if (!buf) {
        errno = EINVAL;
        return NULL;
    }
    Region* r = NULL;
    uintptr_t addr = (uintptr_t)buf;
    for (int i = 0; i < g_max_regions; ++i) {
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

    if (g_region_num == g_max_regions) {
        errno = ENOMEM;
        return NULL;
    }

    // Regularize region size
    region_size = region_size * BYTES_IN_MB / g_block_size[block_type] / g_buckets;
    region_size *= g_block_size[block_type] * g_buckets;

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
    for (int i = 0; i < g_buckets; ++i) {
        node[i] = butil::get_object<IdleNode>();
        if (!node[i]) {
            PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
            for (int j = 0; j < i; ++j) {
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

    for (int i = 0; i < g_buckets; ++i) {
        node[i]->start = (void*)(region->start + i * (region_size / g_buckets));
        node[i]->len = region_size / g_buckets;
        node[i]->next = g_info->ready_list[block_type];
        g_info->ready_list[block_type] = node[i];
    }

    return region_base;
}

void* InitBlockPool(Callback cb) {
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
    if (FLAGS_rdma_memory_pool_max_regions < 1) {
        FLAGS_rdma_memory_pool_max_regions = 1;
    }
    if (FLAGS_rdma_memory_pool_max_regions < g_max_regions) {
        g_max_regions = FLAGS_rdma_memory_pool_max_regions;
    }
    if (FLAGS_rdma_memory_pool_initial_size_mb < 1) {
        FLAGS_rdma_memory_pool_initial_size_mb = 1;
    }
    if (FLAGS_rdma_memory_pool_increase_size_mb < 1) {
        FLAGS_rdma_memory_pool_increase_size_mb = 1;
    }
    if (FLAGS_rdma_memory_pool_buckets >= 1) {
        g_buckets = FLAGS_rdma_memory_pool_buckets;
    }
    g_info = new (std::nothrow) GlobalInfo;
    if (!g_info) {
        return NULL;
    }
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        g_info->idle_list[i].resize(g_buckets, NULL);
        g_info->lock[i].resize(g_buckets, NULL);
        for (int j = 0; j < g_buckets; ++j) {
            g_info->lock[i][j] = new (std::nothrow) butil::Mutex;
            if (!g_info->lock[i][j]) {
                for (int l = 0; l <= i; ++l) {
                    for (int k = 0; k < j; ++k) {
                        delete g_info->lock[l][k];
                        return NULL;
                    }
                }
            }
        }
        g_info->ready_list[i] = NULL;
    }
    g_block_size[BLOCK_DEFAULT] = butil::IOBuf::DEFAULT_BLOCK_SIZE;
    g_block_size[BLOCK_LARGE] = 65536;
    g_block_size[BLOCK_HUGE] = 2 * BYTES_IN_MB;
    return ExtendBlockPool(FLAGS_rdma_memory_pool_initial_size_mb,
                           BLOCK_DEFAULT);
}

static inline void PickReadyBlocks(int block_type, uint64_t index) {
    IdleNode* node = g_info->ready_list[block_type];
    IdleNode* last_node = NULL;
    while (node) {
        Region* r = GetRegion(node->start);
        CHECK(r != NULL);
        if (((uintptr_t)node->start - r->start) * g_buckets / r->size == index) {
            g_info->idle_list[block_type][index] = node;
            if (last_node) {
                last_node->next = node->next;
            } else {
                g_info->ready_list[block_type] = node->next;
            }
            node->next = NULL;
            break;
        }
        last_node = node;
        node = node->next;
    }
}

static void* AllocBlockFrom(int block_type) {
    void* ptr = NULL;
    if (block_type == 0 && tls_idle_list != NULL){
        CHECK(tls_idle_num > 0);
        IdleNode* n = tls_idle_list;
        tls_idle_list = n->next;
        ptr = n->start;
        butil::return_object<IdleNode>(n);
        tls_idle_num--;
        return ptr;
    }
    uint64_t index = butil::fast_rand() % g_buckets;
    BAIDU_SCOPED_LOCK(*g_info->lock[block_type][index]);
    IdleNode* node = g_info->idle_list[block_type][index];
    if (!node) {
        BAIDU_SCOPED_LOCK(g_info->extend_lock);
        PickReadyBlocks(block_type, index);
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
                return NULL;
            } else {
                PickReadyBlocks(block_type, index);
            }
        }
    }
    node = g_info->idle_list[block_type][index];
    if (node) {
        ptr = node->start;
        if (node->len > g_block_size[block_type]) {
            node->start = (char*)node->start + g_block_size[block_type];
            node->len -= g_block_size[block_type];
        } else {
            CHECK(node->len == g_block_size[block_type]);
            g_info->idle_list[block_type][index] = node->next;
            butil::return_object<IdleNode>(node);
        }
    }

    // Move more blocks from global list to tls list
    if (g_buckets == 1 && block_type == 0) {
        node = g_info->idle_list[0][0];
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
            g_info->idle_list[0][0] = node;
        }
        if (last_node) {
            last_node->next = NULL;
        }
    }

    return ptr;
}

void* AllocBlock(size_t size) {
    if (size == 0 || size > g_block_size[BLOCK_SIZE_COUNT - 1]) {
        errno = EINVAL;
        return NULL;
    }
    void* ptr = NULL;
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        if (size <= g_block_size[i]) {
            ptr = AllocBlockFrom(i);;
            break;
        }
    }
    return ptr;
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

    if (block_type == 0 && tls_idle_num < (uint32_t)FLAGS_rdma_memory_pool_tls_cache_num) {
        if (!tls_inited) {
            tls_inited = true;
            butil::thread_atexit(RecycleAll);
        }
        tls_idle_num++;
        node->next = tls_idle_list;
        tls_idle_list = node;
        return 0;
    }

    // Recycle half the cached blocks in tls when there is only one bucket
    if (g_buckets == 1 && block_type == 0) {
        int num = FLAGS_rdma_memory_pool_tls_cache_num / 2;
        IdleNode* new_head = tls_idle_list;
        IdleNode* recycle_tail = NULL;
        for (int i = 0; i < num; ++i) {
            recycle_tail = new_head;
            new_head = new_head->next;
        }
        if (recycle_tail) {
            BAIDU_SCOPED_LOCK(*g_info->lock[0][0]);
            recycle_tail->next = node;
            node->next = g_info->idle_list[0][0];
            g_info->idle_list[0][0] = tls_idle_list;
        }
        tls_idle_list = new_head;
        tls_idle_num -= num;
        return 0;
    }

    uint64_t index = ((uintptr_t)buf - r->start) * g_buckets / r->size;
    {
        BAIDU_SCOPED_LOCK(*g_info->lock[block_type][index]);
        node->next = g_info->idle_list[block_type][index];
        g_info->idle_list[block_type][index] = node;
    }
    return 0;
}

size_t GetBlockSize(int type) {
    return g_block_size[type];
}

// Just for UT
void DestroyBlockPool() {
    RecycleAll();
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        for (int j = 0; j < g_buckets; ++j) {
            IdleNode* node = g_info->idle_list[i][j];
            while (node) {
                IdleNode* tmp = node->next;
                butil::return_object<IdleNode>(node);
                node = tmp;
            }
            g_info->idle_list[i][j] = NULL;
        }
        IdleNode* node = g_info->ready_list[i];
        while (node) {
            IdleNode* tmp = node->next;
            butil::return_object<IdleNode>(node);
            node = tmp;
        }
        g_info->ready_list[i] = NULL;
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
    for (int i = 0; i < g_buckets; ++i) {
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
#endif
