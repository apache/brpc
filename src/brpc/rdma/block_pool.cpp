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
#include <vector>
#include <butil/fast_rand.h>
#include <butil/object_pool.h>
#include <bthread/bthread.h>
#include <gflags/gflags.h>
#include "brpc/rdma/block_pool.h"

namespace brpc {
namespace rdma {

// Number of bytes in 1MB
static const size_t BYTES_IN_MB = 1048576;

DEFINE_int32(rdma_memory_pool_initial_size_mb, 1024,
             "Initial size of memory pool for RDMA (MB), should >=64");
DEFINE_int32(rdma_memory_pool_increase_size_mb, 1024,
             "Increased size of memory pool for RDMA (MB), should >=64");
DEFINE_int32(rdma_memory_pool_max_regions, 16, "Max number of regions");
DEFINE_int32(rdma_memory_pool_buckets, 4, "Number of buckets to reduce race");

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

static int g_buckets = 1;
static int g_max_regions = MAX_REGIONS;
static Region* g_regions[MAX_REGIONS];
static int g_region_num = 0;

// This callback is used when extending a new region
typedef uint32_t (*Callback)(void*, size_t);
static Callback g_cb = NULL;

// TODO:
// This implementation is still coupled with the block size defined in IOBuf.
// We have to update the settings here if the block size of IOBuf is changed.
// Try to make it uncoupled with IOBuf in future. If this is fixed, remember
// to update the NOTE in iobuf.cpp too.
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

// For each block size, there are some buckets of idle list to reduce race.
static std::vector<IdleNode*> g_idle_list[BLOCK_SIZE_COUNT];
static std::vector<butil::Mutex*> g_lock[BLOCK_SIZE_COUNT];
static butil::Mutex g_extend_lock;
static IdleNode* g_ready_list[BLOCK_SIZE_COUNT];

static inline Region* GetRegion(const void* buf) {
    if (!buf) {
        errno = EINVAL;
        return NULL;
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

uint32_t GetRegionId(const void* buf) {
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
    region_size = region_size * BYTES_IN_MB / BLOCK_SIZE[block_type] / g_buckets;
    region_size *= BLOCK_SIZE[block_type] * g_buckets;

    Region* region = new (std::nothrow) Region;
    if (!region) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        return NULL;
    }

    void* region_base = NULL;
    if (posix_memalign(&region_base, 4096, region_size) != 0) {
        PLOG_EVERY_SECOND(ERROR) << "Memory not enough";
        delete region;
        return NULL;
    }

    uint32_t id = g_cb(region_base, region_size);
    if (id == 0) {
        free(region_base);
        delete region;
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
            delete region;
            return NULL;
        }
    }
 
    region->start = (uintptr_t)region_base;
    region->size = region_size;
    region->id = id;
    region->block_type = block_type;
    g_regions[g_region_num++] = region;

    for (int i = 0; i < g_buckets; ++i) {
        node[i]->start = (void*)(region->start + i * (region_size / g_buckets));
        node[i]->len = region_size / g_buckets;
        node[i]->next = g_ready_list[block_type];
        g_ready_list[block_type] = node[i];
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
    if (FLAGS_rdma_memory_pool_initial_size_mb < 64) {
        FLAGS_rdma_memory_pool_initial_size_mb = 64;
    }
    if (FLAGS_rdma_memory_pool_increase_size_mb < 64) {
        FLAGS_rdma_memory_pool_increase_size_mb = 64;
    }
    if (FLAGS_rdma_memory_pool_buckets >= 1) {
        g_buckets = FLAGS_rdma_memory_pool_buckets;
    }
    for (size_t i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        g_idle_list[i].resize(g_buckets, NULL);
        g_lock[i].resize(g_buckets, NULL);
        for (int j = 0; j < g_buckets; ++j) {
            g_lock[i][j] = new (std::nothrow) butil::Mutex;
            if (!g_lock[i][j]) {
                for (size_t l = 0; l <= i; ++l) {
                    for (int k = 0; k < j; ++k) {
                        delete g_lock[l][k];
                        return NULL;
                    }
                }
            }
        }
    }
    return ExtendBlockPool(FLAGS_rdma_memory_pool_initial_size_mb,
                           BLOCK_DEFAULT);
}

static inline void PickReadyBlocks(int block_type, uint64_t index) {
    g_idle_list[block_type][index] = g_ready_list[block_type];
    g_ready_list[block_type] = g_ready_list[block_type]->next;
    g_idle_list[block_type][index]->next = NULL;
}

static void* AllocBlockFrom(int block_type) {
    void* ptr = NULL;
    uint64_t index = butil::fast_rand_less_than(g_buckets);
    BAIDU_SCOPED_LOCK(*g_lock[block_type][index]);
    IdleNode* node = g_idle_list[block_type][index];
    if (!node) {
        BAIDU_SCOPED_LOCK(g_extend_lock);
        if (g_ready_list[block_type]) {
            PickReadyBlocks(block_type, index);
        }
        node = g_idle_list[block_type][index];
        if (!node) {
            // There is no block left, extend a new region
            if (!ExtendBlockPool(FLAGS_rdma_memory_pool_increase_size_mb,
                                 block_type)) {
                LOG_EVERY_SECOND(ERROR) << "Fail to extend new region";
                return NULL;
            } else {
                PickReadyBlocks(block_type, index);
            }
        }
    }
    node = g_idle_list[block_type][index];
    if (node) {
        ptr = node->start;
        if (node->len > BLOCK_SIZE[block_type]) {
            node->start = (char*)node->start + BLOCK_SIZE[block_type];
            node->len -= BLOCK_SIZE[block_type];
        } else {
            CHECK(node->len == BLOCK_SIZE[block_type]);
            g_idle_list[block_type][index] = node->next;
            butil::return_object<IdleNode>(node);
        }
    }
    return ptr;
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
    return ptr;
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

    node->start = buf;
    node->len = block_size;
    uint64_t index = butil::fast_rand_less_than(g_buckets);
    {
        BAIDU_SCOPED_LOCK(*g_lock[block_type][index]);
        IdleNode* head = g_idle_list[block_type][index];
        node->next = head;
        g_idle_list[block_type][index] = node;
    }
    return 0;
}

// Just for UT
void DestroyBlockPool() {
    for (int i = 0; i < BLOCK_SIZE_COUNT; ++i) {
        for (int j = 0; j < g_buckets; ++j) {
            IdleNode* node = g_idle_list[i][j];
            while (node) {
                IdleNode* tmp = node->next;
                butil::return_object<IdleNode>(node);
                node = tmp;
            }
            g_idle_list[i][j] = NULL;
        }
        IdleNode* node = g_ready_list[i];
        while (node) {
            IdleNode* tmp = node->next;
            butil::return_object<IdleNode>(node);
            node = tmp;
        }
        g_ready_list[i] = NULL;
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
        return -1;
    }
    return r->block_type;
}

// Just for UT
size_t GetBlockSize(int type) {
    return BLOCK_SIZE[type];
}

// Just for UT
size_t GetGlobalLen(int block_type) {
    size_t len = 0;
    for (int i = 0; i < g_buckets; ++i) {
        IdleNode* node = g_idle_list[block_type][i];
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

