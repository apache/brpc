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
#ifndef BUTIL_GPU_GPU_BLOCK_POOL_H
#define BUTIL_GPU_GPU_BLOCK_POOL_H

#if BRPC_WITH_GDR

#include <infiniband/verbs.h>
#include <sys/types.h>
#include <stdint.h>
#include <linux/types.h>
#include <string>
#include <vector>
#include <mutex>
#include <infiniband/verbs.h>
#include "butil/containers/hash_tables.h"
#include "butil/logging.h"
#include <cuda_runtime.h>
#include "cuda.h"

// #include "gdrapi.h"
namespace butil {
namespace gdr {

static int gdr_block_size_kb = [](){
    int ret = 64;
    const char* env_var_val = getenv("GDR_BLOCK_SIZE_KB");
    if (env_var_val == nullptr) {
        return ret;
    }
    ret = std::stoi(env_var_val);

    return ret;
}();

void* get_gpu_mem(int gpu_id, int64_t gpu_mem_size);
void* get_cpu_mem(int gpu_id, int64_t cpu_mem_size);

bool InitGPUBlockPool(int gpu_id, ibv_pd* pd);

struct Region {
    Region() { start = 0; aligned_start = 0;}
    uintptr_t start;
    uintptr_t aligned_start;

    size_t size;
    size_t aligned_size;
    size_t blockCount;
    struct ibv_mr *mr {nullptr};
    uint32_t lkey;
};

struct BlockHeader {
    BlockHeader() { addr = nullptr; next = nullptr;}
    void Reset() { addr = nullptr; next = nullptr; }
    void* addr;
    BlockHeader* next;
};

class BlockPoolAllocator {
  private:
    int gpu_id;
    bool on_gpu;
    ibv_pd* pd {nullptr};

    const size_t BLOCK_SIZE;
    const size_t REGION_SIZE;

    BlockHeader* freeList;
    static constexpr size_t max_regions = 16;
    int g_region_num {0};
    Region g_regions[max_regions];
    std::mutex poolMutex;

    // 统计信息
    size_t totalAllocated;
    size_t totalDeallocated;
    size_t peakUsage;

  public:
    explicit BlockPoolAllocator(int gpu_id,
                                bool on_gpu, ibv_pd* pd,
                                size_t blockSize, size_t regionSize);

    ~BlockPoolAllocator();

    void* AllocateRaw(size_t num_bytes);

    void DeallocateRaw(void* ptr);

    // 获取统计信息
    void printStatistics() const;

    int64_t getCurrentUsage() const {
      return totalAllocated - totalDeallocated;
    }

    int64_t getTotalMemory() const {
      return g_region_num * REGION_SIZE;
    }

    int64_t get_block_size() const {
      return BLOCK_SIZE;
    }

    uint32_t get_lkey(const void* buf);

  private:
    Region* GetRegion(const void* buf);
    void extendRegion();
};

class GPUStreamPool {
public:
    explicit GPUStreamPool(int gpu_id);

    ~GPUStreamPool();

    GPUStreamPool(const GPUStreamPool&) = delete;
    GPUStreamPool& operator=(const GPUStreamPool&) = delete;

    void fast_d2h(std::vector<void*>& src_list, std::vector<int64_t>& length_list, void* dst);

    void fast_d2d(std::vector<void*>& src_list, std::vector<int64_t>& length_list, void* dst);

    static constexpr int kMaxConcurrent = 32;
private:
    int gpu_id_ {-1};
    std::atomic<int64_t> d2h_cnt_ {0};
    std::atomic<int64_t> d2d_cnt_ {0};
    std::mutex d2h_locks_[kMaxConcurrent];
    std::mutex d2d_locks_[kMaxConcurrent];
    std::mutex d2h_lb_lock_;
    std::mutex d2d_lb_lock_;
    std::vector<cudaStream_t> d2h_streams_;
    std::vector<cudaStream_t> d2d_streams_;
};

class BlockPoolAllocators {
public:
    static BlockPoolAllocators* singleton();
    BlockPoolAllocators() {}
    virtual ~BlockPoolAllocators() {
        CHECK_EQ(this, instance_);
        instance_ = nullptr;
    }

    void init(int gpu_id, ibv_pd* pd) {
        LOG(INFO) << "set GPU BlockPoolAllocator for " << gpu_id;
        size_t region_size = 512LL * 1024 * 1024;
        size_t block_size = gdr_block_size_kb * 1024;
        gpu_mem_alloc = new BlockPoolAllocator(gpu_id, true, pd, block_size, region_size);

        region_size = 32LL * 1024 * 1024;
        block_size = 512;
        cpu_mem_alloc = new BlockPoolAllocator(gpu_id, false, pd, block_size, region_size);

        gpu_stream_pool = new GPUStreamPool(gpu_id);
    }

    BlockPoolAllocator* get_gpu_allocator() {
        return gpu_mem_alloc;
    }

    BlockPoolAllocator* get_cpu_allocator() {
        return cpu_mem_alloc;
    }

    GPUStreamPool* get_gpu_stream_pool() {
        return gpu_stream_pool;
    }

public:
    static BlockPoolAllocators* instance_;

private:
    BlockPoolAllocator* gpu_mem_alloc {nullptr};
    BlockPoolAllocator* cpu_mem_alloc {nullptr};
    GPUStreamPool*      gpu_stream_pool {nullptr};
};

}
}

#endif // BRPC_WITH_GDR

#endif
