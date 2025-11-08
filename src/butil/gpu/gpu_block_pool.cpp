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

#if BRPC_WITH_GDR

#include <iostream>
#include <chrono>
#include "butil/fast_rand.h"
#include "gpu_block_pool.h"
namespace butil {
namespace gdr {

#define CHECK_CUDA(call)  \
do {                                                                     \
    auto _sts = (call);                                                    \
    if (_sts != cudaSuccess) {                                             \
        LOG(FATAL) << " cuda error:"                                       \
        << (cudaGetErrorString(_sts)) << std::string(" at ")    \
        << __FILE__ << ": " << __LINE__;                        \
    }                                                                      \
} while (0);

bool verify_same_context() {
  static int original_device = -1;
  static bool first_call = true;

  int current_device;
  cudaGetDevice(&current_device);

  if (first_call) {
      original_device = current_device;
      first_call = false;
      return true;
  }

  return (current_device == original_device);
}

void* get_gpu_mem(int gpu_id, int64_t gpu_mem_size) {
    CHECK_CUDA(cudaSetDevice(gpu_id));
    void *d_data;

    LOG(INFO) << "try to alloc " << gpu_mem_size << " bytes from gpu " << gpu_id;

    CHECK_CUDA(cudaMalloc(&d_data, gpu_mem_size));
    cudaDeviceSynchronize();
    return (void *)d_data;
}

void* get_cpu_mem(int gpu_id, int64_t cpu_mem_size) {
    CHECK_CUDA(cudaSetDevice(gpu_id));

    LOG(INFO) << "try to alloc " << cpu_mem_size << " bytes from gpu " << gpu_id << "on host";

    void* mem = NULL;

    CHECK_CUDA(cudaMallocHost(&mem, cpu_mem_size));

    cudaDeviceSynchronize();

    return mem;
}


BlockPoolAllocators* BlockPoolAllocators::instance_ = nullptr;

BlockPoolAllocators* BlockPoolAllocators::singleton() {
    static std::mutex mutex;
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> l(mutex);
        if(instance_ == nullptr) {
            instance_ = new BlockPoolAllocators();
            std::atomic_thread_fence(std::memory_order_release);
        }
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    return instance_;
}

bool InitGPUBlockPool(int gpu_id, ibv_pd* pd) {
    BlockPoolAllocators::singleton()->init(gpu_id, pd);
    return true;
}

class BlockHeaderList {
  public:
    BlockHeaderList() {
        objects_.reserve(kMaxObjects);
    }
    virtual ~BlockHeaderList() {
        for (size_t i = 0; i < objects_.size(); i++) {
            delete objects_[i];
      }
    }

    BlockHeader* New() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!objects_.empty()) {
                BlockHeader* result = objects_.back();
                objects_.pop_back();
                return result;
            }
        }
        return new BlockHeader;
    }
    void Release(BlockHeader* obj) {
        obj->Reset();
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (objects_.size() < kMaxObjects) {
                objects_.push_back(obj);
                return;
            }
        }
        delete obj;
    }

  private:
    static const int kMaxObjects = 100000;

    std::mutex mu_;
    std::vector<BlockHeader*> objects_;
};

static BlockHeaderList* get_bh_list() {
    static BlockHeaderList* bh_list = new BlockHeaderList();
    return bh_list;
}


BlockPoolAllocator::BlockPoolAllocator(int gpuId, bool onGpu, ibv_pd* brpc_pd,
    size_t blockSize, size_t regionSize) :
    gpu_id(gpuId)
    , on_gpu(onGpu)
    , pd(brpc_pd)
    , BLOCK_SIZE(std::max(blockSize, sizeof(BlockHeader)))
    , REGION_SIZE((regionSize / blockSize) * blockSize)  // 对齐到块大小的倍数
    , freeList(nullptr)
    , g_region_num(0)
    , totalAllocated(0)
    , totalDeallocated(0)
    , peakUsage(0) {
    LOG(INFO) << "Memory Pool initialized: block_size=" << BLOCK_SIZE 
      << ", region_size=" << REGION_SIZE 
      << ", gpu_id=" << gpu_id << ", on_gpu=" << on_gpu << ", pd=" << pd;

    extendRegion();
}

BlockPoolAllocator::~BlockPoolAllocator() {
#ifdef DEBUG
    printStatistics();
#endif

    for (int i = 0; i < max_regions; i++) {
        Region* r = &g_regions[i];
        if (!r->mr) {
            return;
        }

        LOG(INFO) << "try to free " << r->size << " bytes from gpu " << gpu_id << ", on_gpu " << on_gpu;
        ibv_dereg_mr(r->mr);
        if (on_gpu) {
            CHECK_CUDA(cudaFree(reinterpret_cast<void*>(r->start)));
        } else {
            CHECK_CUDA(cudaFreeHost(reinterpret_cast<void*>(r->start)));
        }
    }
}

Region* BlockPoolAllocator::GetRegion(const void* buf) {
    if (!buf) {
        errno = EINVAL;
        return NULL;
    }
    Region* r = NULL;
    uintptr_t addr = (uintptr_t)buf;
    for (int i = 0; i < max_regions; ++i) {
        if (g_regions[i].aligned_start == 0) {
            break;
        }
        if (addr >= g_regions[i].aligned_start &&
                addr < g_regions[i].aligned_start + g_regions[i].aligned_size) {
            r = &g_regions[i];
            break;
        }
    }
    return r;
}

uint32_t BlockPoolAllocator::get_lkey(const void* buf) {
    Region* r = GetRegion(buf);
    if (!r) {
        LOG(ERROR) << "can not get a region for buf " << buf;
        return 0;
    }
    return r->lkey;
}

void* BlockPoolAllocator::AllocateRaw(size_t num_bytes) {
    if (num_bytes == 0) {
        return nullptr;
    }
    if (num_bytes > BLOCK_SIZE) {
        LOG(FATAL) << "try to alloc " << num_bytes << " bytes, its bigger than block_size " << BLOCK_SIZE;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(poolMutex);

    if (!freeList) {
        extendRegion();
    }

    BlockHeader* block = freeList;
    freeList = freeList->next;

    void* addr = block->addr;
    get_bh_list()->Release(block);

    totalAllocated++;
    peakUsage = std::max(peakUsage, totalAllocated - totalDeallocated);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);

#ifdef DEBUG
    if (duration.count() > 1000) {  // 如果分配时间超过1微秒
        LOG(INFO) << "Slow allocation: " << duration.count() << " ns";
    }
#endif

    return addr;
}

void BlockPoolAllocator::DeallocateRaw(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(poolMutex);

    BlockHeader* block = get_bh_list()->New();
    block->addr = ptr;
    block->next = freeList;
    freeList = block;

    totalDeallocated++;
}

// 获取统计信息
void BlockPoolAllocator::printStatistics() const {
    LOG(INFO) << "=== Memory Pool Statistics ===";
    LOG(INFO) << "Total regions: " << g_region_num
        << ", Total blocks allocated: " << totalAllocated
        << ", Total blocks deallocated: " << totalDeallocated
        << ", Current usage: " << (totalAllocated - totalDeallocated) << " blocks"
        << ", Peak usage: " << peakUsage << " blocks"
        << ", Memory efficiency: "
        << (static_cast<double>(totalAllocated - totalDeallocated) /
                (g_region_num * (REGION_SIZE / BLOCK_SIZE)) * 100)
        << "%";
}

void BlockPoolAllocator::extendRegion() {
    if (g_region_num == max_regions) {
        LOG(FATAL) << "Gdr Memory pool reaches max regions";
        return ;
    }

    auto startTime = std::chrono::high_resolution_clock::now();
    void* ptr = nullptr;
    void* aligned_ptr = nullptr;
    int alignment = 4096;

    if (on_gpu) {
        ptr = get_gpu_mem(gpu_id, REGION_SIZE);
    } else {
        ptr = get_cpu_mem(gpu_id, REGION_SIZE);
    }

    aligned_ptr = (void*)(((uintptr_t)ptr + alignment - 1) & ~(alignment - 1));

    int64_t aligned_bytes = REGION_SIZE;
    if (ptr != aligned_ptr) {
        uintptr_t region_end = uintptr_t(ptr) + REGION_SIZE;
        uintptr_t aligned_end_ptr = (region_end + alignment - 1) & ~(alignment - 1);
        aligned_bytes = uintptr_t(aligned_end_ptr) - uintptr_t(aligned_ptr);
        LOG(WARNING) << "addr is not aligned with 4096: " << ptr << ", aligned_bytes: " << aligned_bytes
            << ", region_size: " << REGION_SIZE;
    }

    LOG(INFO) << "reg_mr for ptr: " << aligned_ptr << ", size:" << aligned_bytes;
    auto mr = ibv_reg_mr(pd, aligned_ptr, aligned_bytes,
            IBV_ACCESS_LOCAL_WRITE |
            IBV_ACCESS_REMOTE_READ |
            IBV_ACCESS_REMOTE_WRITE);
    //IBV_ACCESS_RELAXED_ORDERING);

    if (!mr) {
        LOG(FATAL) << "Failed to register MR: " << strerror(errno)
            << ", pd " << pd << ", aligned_ptr:" << aligned_ptr;
    } else {
        LOG(INFO) << "Success to register MR: "
            << ", pd " << pd << ", aligned_ptr:" << aligned_ptr;
    }

    LOG(INFO) << "try to init region, g_region_num:" << g_region_num;
    size_t blockCount = aligned_bytes / BLOCK_SIZE;
    Region* region = &g_regions[g_region_num++];
    region->start = (uintptr_t)ptr;
    region->aligned_start = (uintptr_t)aligned_ptr;
    region->mr = mr;
    region->size = REGION_SIZE;
    region->aligned_size = aligned_bytes;
    region->lkey = mr->lkey;
    region->blockCount = blockCount;


    LOG(INFO) << "try to insert list, freeList:" << freeList << ", blockCount:" << blockCount;
    BlockHeader* lastBlock = nullptr;
    for (size_t i = 0; i < blockCount; ++i) {
        BlockHeader* block = get_bh_list()->New();
        block->addr = reinterpret_cast<void*>(static_cast<char*>(aligned_ptr) + i * BLOCK_SIZE);
        if (lastBlock != nullptr) {
            lastBlock->next = block;
        } else {
            freeList = block;
        }
        lastBlock = block;
    }

    if (lastBlock) {
        lastBlock->next = nullptr;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);

    LOG(INFO) << "Extended region #" << g_region_num << ": " << blockCount 
        << " blocks (" << (REGION_SIZE / (1024 * 1024)) << " MB)" << ", on_gpu " << on_gpu
        << ", cost " << duration.count()  << " ns";
}

GPUStreamPool::GPUStreamPool(int gpu_id) :
                    gpu_id_(gpu_id) {
    CHECK_CUDA(cudaSetDevice(gpu_id));
    d2d_streams_.resize(kMaxConcurrent);
    d2h_streams_.resize(kMaxConcurrent);
    for (int i = 0; i < kMaxConcurrent; i++) {
        CHECK_CUDA(cudaStreamCreate(&d2d_streams_[i]));
        CHECK_CUDA(cudaStreamCreate(&d2h_streams_[i]));
    }
    CHECK_CUDA(cudaDeviceSynchronize());
}

GPUStreamPool::~GPUStreamPool() {
    CHECK_CUDA(cudaDeviceSynchronize());
    for (int i = 0; i < kMaxConcurrent; i++) {
        CHECK_CUDA(cudaStreamDestroy(d2d_streams_[i]));
        CHECK_CUDA(cudaStreamDestroy(d2h_streams_[i]));
    }
}

void GPUStreamPool::fast_d2d(std::vector<void*>& src_list,
                                  std::vector<int64_t>& length_list,
                                  void* dst) {
#ifdef DEBUG
    if (!verify_same_context()) {
        LOG(FATAL) << "Context mismatch!";
        return;
    }
#endif
    int64_t offset = 0;
    int segs = src_list.size();
    if (segs == 0) return;
    if (segs != length_list.size()) {
        LOG(FATAL) << "src list size is not equal with length list size!!!";
    }

    int stream_idx = 0;
    {
        std::lock_guard<std::mutex> stream_lb_lock(d2d_lb_lock_);
        d2d_cnt_.fetch_add(1);
        stream_idx = d2d_cnt_ % kMaxConcurrent;
    }
    std::lock_guard<std::mutex> stream_lock(d2d_locks_[stream_idx]);
    CHECK_CUDA(cudaStreamSynchronize(d2d_streams_[stream_idx]));
    for (int i = 0; i < segs; i++) {
        if (length_list[i] == 0) {
            continue;
        }
        CHECK_CUDA(cudaMemcpyAsync(static_cast<char*>(dst) + offset, src_list[i], length_list[i],
                    cudaMemcpyDeviceToDevice, d2d_streams_[stream_idx]));
        offset += length_list[i];
    }
    CHECK_CUDA(cudaStreamSynchronize(d2d_streams_[stream_idx]));
}

void GPUStreamPool::fast_d2h(std::vector<void*>& src_list,
                                  std::vector<int64_t>& length_list,
                                  void* dst) {
    if (!verify_same_context()) {
        LOG(FATAL) << "Context mismatch!";
        return;
    }
    int64_t offset = 0;
    int segs = src_list.size();
    if (segs == 0) return;
    if (segs != length_list.size()) {
        LOG(FATAL) << "src list size is not equal with length list size!!!";
    }

    int stream_idx = 0;
    {
        std::lock_guard<std::mutex> stream_lb_lock(d2h_lb_lock_);
        d2h_cnt_.fetch_add(1);
        stream_idx = d2h_cnt_ % kMaxConcurrent;
    }
    std::lock_guard<std::mutex> stream_lock(d2h_locks_[stream_idx]);
    CHECK_CUDA(cudaStreamSynchronize(d2h_streams_[stream_idx]));
    for (int i = 0; i < segs; i++) {
        if (length_list[i] == 0) {
            continue;
        }
        CHECK_CUDA(cudaMemcpyAsync(static_cast<char*>(dst) + offset, src_list[i], length_list[i],
                    cudaMemcpyDeviceToHost, d2h_streams_[stream_idx]));
        offset += length_list[i];
    }
    CHECK_CUDA(cudaStreamSynchronize(d2h_streams_[stream_idx]));
}

}
}

#endif // BRPC_WITH_GDR
