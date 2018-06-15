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

#include <errno.h>
#include <stdlib.h>
#include <butil/object_pool.h>
#include <butil/thread_local.h>
#include <bthread/bthread.h>
#include <gflags/gflags.h>
#include "brpc/rdma/block_pool.h"

namespace brpc {
namespace rdma {

// Number of bytes in 1MB
static const size_t BYTES_IN_MB = 1048576;
// Size of blocks which trigger a recycle from tls list to global list
static const size_t RECYCLE_THRESHOLD = 2 * BYTES_IN_MB;  // 2MB
// Size of blocks should be prefetched from global list to tls list once
static const size_t PREFETCH_SIZE = 128 * 1024;  // 128KB

DEFINE_int32(rdma_memory_pool_initial_size_mb, 1024,
             "Initial size of memory pool for RDMA (MB), should >=64");
DEFINE_int32(rdma_memory_pool_increase_size_mb, 1024,
             "Increased size of memory pool for RDMA (MB), should >=64");
DEFINE_int32(rdma_memory_pool_max_regions, 16, "Max number of regions");

struct IdleNode {
    void* start;
    size_t len;
    IdleNode* next;
};

struct Region {
    uintptr_t start;
    size_t size;
    uint32_t block_type;
    uint32_t id;  // lkey
};

static const int MAX_REGIONS = 16;

static int g_max_regions = MAX_REGIONS;
static butil::Mutex g_lock;
static Region* g_regions[MAX_REGIONS];
static int g_region_num = 0;

// This callback is used when extending a new region
typedef uint32_t (*Callback)(void*, size_t);
static Callback g_cb = NULL;

// TODO:
// This implementation is still coupled with the block size defined in IOBuf.
// We have to update the settings here if the block size of IOBuf is changed.
// Try to make it uncoupled with IOBuf in future.
static const int BLOCK_DEFAULT = 0;
static const int BLOCK_2_DEFAULT = 1;
static const int BLOCK_4_DEFAULT = 2;
static const int BLOCK_8_DEFAULT = 3;
static const int BLOCK_SIZE_COUNT = 4;
static const size_t BLOCK_SIZE[BLOCK_SIZE_COUNT] =
#ifdef IOBUF_HUGE_BLOCK
        { 256 * 1024, 512 * 1024, 1024 * 1024, 2048 * 1024 };
#else
        { 8192, 16384, 32768, 65536 };
#endif

// For each block size, there is a series of independent variables as follows
static IdleNode* g_idle_list[BLOCK_SIZE_COUNT] = { NULL, NULL, NULL, NULL };
static __thread IdleNode* tls_idle_list[BLOCK_SIZE_COUNT] = { NULL, NULL, NULL, NULL };
static __thread IdleNode* tls_list_tail[BLOCK_SIZE_COUNT] = { NULL, NULL, NULL, NULL };
static __thread size_t tls_list_len[BLOCK_SIZE_COUNT] = { 0, 0, 0, 0 };
static __thread size_t tls_list_min_len[BLOCK_SIZE_COUNT] = { 0, 0, 0, 0 };

static __thread bool tls_inited = false;

static inline Region* GetRegion(void* buf) {
    if (!buf) {
        errno = EINVAL;
        return NULL;;
    }
    Region* r = NULL;
    uintptr_t addr = (uintptr_t)buf;
    for (int i = 0; i < g_max_regions; ++i) {
        if (g_regions[i] == NULL) {
            break;
        }
        if (addr >= g_regions[i]->start &&
            addr < g_regions[i]->start + g_regions[i]->size) {
            r = g_regions[i];
            break;
        }
    }
    return r;
}

uint32_t GetRegionId(void* buf) {
    Region* r = GetRegion(buf);
    if (!r) {
        return 0;
    }
    return r->id;
}

// Extend the block pool with a new region (with different region ID)
static void* ExtendBlockPool(size_t region_size, int block_type) {
    if (region_size < 64) {
        errno = EINVAL;
        return NULL;
    }

    if (g_region_num == g_max_regions) {
        errno = ENOMEM;
        return NULL;
    }

    // Regularize region size
    region_size = region_size * BYTES_IN_MB / BLOCK_SIZE[block_type];
    region_size *= BLOCK_SIZE[block_type];

    // Malloc the memory and create IdleNode for new region
    IdleNode* node = butil::get_object<IdleNode>();
    if (!node) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        return NULL;
    }

    Region* region = new (std::nothrow) Region;
    if (!region) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        return NULL;
    }

    void* region_base = NULL;
    if (posix_memalign(&region_base, 4096, region_size) != 0) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        butil::return_object<IdleNode>(node);
        delete region;
        return NULL;
    }
    CHECK(region_base != NULL);

    uint32_t id = g_cb(region_base, region_size);
    if (id == 0) {
        free(region_base);
        butil::return_object<IdleNode>(node);
        delete region;
        return NULL;
    }

    region->start = (uintptr_t)region_base;
    region->size = region_size;
    region->id = id;
    region->block_type = block_type;
    g_regions[g_region_num++] = region;

    // Update the new IdleNode and insert it into global idle list
    node->start = region_base;
    node->len = region_size;
    node->next = g_idle_list[block_type];
    g_idle_list[block_type] = node;

    return region_base;
}

void* InitBlockPool(Callback cb) {
    if (!cb) {
        errno = EINVAL;
        return NULL;
    }
    BAIDU_SCOPED_LOCK(g_lock);
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
    if (FLAGS_rdma_memory_pool_initial_size_mb < 64) {
        FLAGS_rdma_memory_pool_initial_size_mb = 64;
    }
    if (FLAGS_rdma_memory_pool_increase_size_mb < 64) {
        FLAGS_rdma_memory_pool_increase_size_mb = 64;
    }
    return ExtendBlockPool(FLAGS_rdma_memory_pool_initial_size_mb, BLOCK_DEFAULT);
}

static void* Prefetch(int block_type) {
    // Besides the block which should be returned to the caller,
    // we also prefetch some blocks to avoid locking the global mutex
    // frequently.

    void* ptr = NULL;
    IdleNode* head_node = NULL;
    bool release = true;

    {
        BAIDU_SCOPED_LOCK(g_lock);
        IdleNode* node = g_idle_list[block_type];
        if (!node) {
            if (!ExtendBlockPool(FLAGS_rdma_memory_pool_increase_size_mb, block_type)) {
                LOG_EVERY_SECOND(ERROR) << "Fail to extend new region";
                return NULL;
            }
            node = g_idle_list[block_type];
        }
        CHECK(node != NULL);

        ptr = node->start;
        if (node->len > BLOCK_SIZE[block_type]) {
            node->start = (char*)node->start + BLOCK_SIZE[block_type];
            node->len -= BLOCK_SIZE[block_type];
        } else {
            CHECK(node->len == BLOCK_SIZE[block_type]);
            g_idle_list[block_type] = node->next;
            head_node = node;
        }

        // Prefetch some blocks for this thread
        IdleNode* head = g_idle_list[block_type];
        if (!head) {
            return ptr;
        }
        node = head;
        size_t prefetch_size = PREFETCH_SIZE > BLOCK_SIZE[block_type] ?
                               PREFETCH_SIZE : BLOCK_SIZE[block_type]; 
        size_t left_size = prefetch_size;
        while (node && left_size > 0) {
            if (node->len < left_size) {
                left_size -= node->len;
                tls_list_tail[block_type] = node;
                node = node->next;
                continue;
            } else if (node->len == left_size) {
                g_idle_list[block_type] = node->next;
            } else {
                // Reuse the node which can be released
                if (!head_node) {
                    head_node = butil::get_object<IdleNode>();
                    if (!head_node) {
                        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
                        // Give up the prefetch directly
                        tls_list_tail[block_type] = NULL;
                        break;
                    }
                }
                head_node->start = (char*)node->start + left_size;
                head_node->len = node->len - left_size;
                head_node->next = node->next;
                node->len = left_size;
                g_idle_list[block_type] = head_node;
                release = false;
            }
            node->next = NULL;
            tls_list_tail[block_type] = node;
            tls_idle_list[block_type] = head;
            tls_list_len[block_type] = prefetch_size;
            break;
        }
        if (!node) {
            tls_idle_list[block_type] = head;
            tls_list_len[block_type] = prefetch_size - left_size;
            g_idle_list[block_type] = NULL;
        }
    }

    if (release && head_node) {
        butil::return_object<IdleNode>(head_node);
    }

    return ptr;
}

static void* AllocBlockFrom(int block_type) {
    void* ptr = NULL;
    // Get from tls_list first
    IdleNode* node = tls_idle_list[block_type];
    if (node) {
        ptr = node->start;
        if (node->len > BLOCK_SIZE[block_type]) {
            node->start = (char*)node->start + BLOCK_SIZE[block_type];
            node->len -= BLOCK_SIZE[block_type];
        } else {
            CHECK(node->len == BLOCK_SIZE[block_type]);
            tls_idle_list[block_type] = node->next;
            butil::return_object<IdleNode>(node);
        }
        tls_list_len[block_type] -= BLOCK_SIZE[block_type];
        if (tls_list_len[block_type] == 0) {
            CHECK(tls_idle_list[block_type] == NULL);
            tls_list_tail[block_type] = NULL;
        }
        if (tls_list_len[block_type] < tls_list_min_len[block_type]) {
            tls_list_min_len[block_type] = tls_list_len[block_type];
        }
    } else {
        // There is no block in tls_list, get from global list
        ptr = Prefetch(block_type);
    }
    return ptr;
}

static void RecycleAll() {
    // Recycle from all tls idle lists
    BAIDU_SCOPED_LOCK(g_lock);
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        if (tls_list_tail[i]) {
            CHECK(tls_idle_list[i] != NULL);
            tls_list_tail[i]->next = g_idle_list[i];
            g_idle_list[i] = tls_idle_list[i];
            tls_idle_list[i] = NULL;
            tls_list_tail[i] = NULL;
            tls_list_len[i] = 0;
            tls_list_min_len[i] = 0;
        }
    }
}

void* AllocBlock(size_t size) {
    if (size == 0 || size > BLOCK_SIZE[BLOCK_SIZE_COUNT - 1]) {
        errno = EINVAL;
        return NULL;
    }
    void* ptr = NULL;
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        if (size <= BLOCK_SIZE[i]) {
            ptr = AllocBlockFrom(i);;
            break;
        }
    }
    if (ptr && !tls_inited) {
        tls_inited = true;
        butil::thread_atexit(RecycleAll);
    }
    return ptr;
}

static void Recycle(uint32_t block_type) {
    // We keep the minimum size of tls idle list (L) since last recycle.
    // Every time we do a recycle, we pick up the first L/2 tls cache and
    // put them into the global idle list. Similar with tcmalloc.
    size_t recycle_size = tls_list_min_len[block_type] / 2;
    tls_list_min_len[block_type] = tls_list_len[block_type];
    if (recycle_size < BLOCK_SIZE[block_type]) {
        recycle_size = BLOCK_SIZE[block_type];
    }
    IdleNode* head = tls_idle_list[block_type];
    IdleNode* node = head;
    IdleNode* last_node = NULL;
    while (node && recycle_size >= node->len) {
        recycle_size -= node->len;
        tls_list_len[block_type] -= node->len;
        last_node = node;
        node = node->next;
    }
    if (last_node) {
        BAIDU_SCOPED_LOCK(g_lock);
        last_node->next = g_idle_list[block_type];
        g_idle_list[block_type] = head;
        tls_idle_list[block_type] = node;
    }
    if (!node) {
        CHECK(tls_list_len[block_type] == 0);
        tls_list_tail[block_type] = NULL;
    }
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

    uint32_t block_type = r->block_type;
    size_t block_size = BLOCK_SIZE[block_type];

    IdleNode* node = butil::get_object<IdleNode>();
    if (!node) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        // May lead to block leak, but do not return -1
        return 0;
    }

    // Insert back to the tls_list
    node->start = buf;
    node->len = block_size;
    IdleNode* head = tls_idle_list[block_type];
    node->next = head;
    if (!head) {
        tls_list_tail[block_type] = node;
    }
    tls_idle_list[block_type] = node;
    tls_list_len[block_type] += block_size;

    // Too long tls_list, put it in global list
    if (tls_list_len[block_type] > RECYCLE_THRESHOLD) {
        Recycle(block_type);
    }

    return 0;
}

// Just for UT
void DestroyBlockPool() {
    RecycleAll();
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        IdleNode* node = g_idle_list[i];
        while (node) {
            IdleNode* tmp = node->next;
            butil::return_object<IdleNode>(node);
            node = tmp;
        }
        g_idle_list[i] = NULL;
    }
    for (int i = 0; i < g_region_num; ++i) {
        Region* r = g_regions[i];
        if (!r) {
            break;
        }
        free((void*)r->start);
        delete r;
        g_regions[i] = NULL;
    }
    g_region_num = 0;
    g_cb = NULL;
}

// Just for UT
int GetBlockType(void* buf) {
    Region* r = GetRegion(buf);
    if (!r) {
        return 0;
    }
    return r->block_type;
}

// Just for UT
size_t GetTLSLen(int block_type) {
    return tls_list_len[block_type];
}

// Just for UT
size_t GetGlobalLen(int block_type) {
    IdleNode* node = g_idle_list[block_type];
    size_t len = 0;
    while (node) {
        len += node->len;
        node = node->next;
    }
    return len;
}

// Just for UT
size_t GetRegionNum() {
    return g_region_num;
}

}  // namespace rdma
}  // namespace brpc

