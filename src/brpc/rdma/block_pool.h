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

#ifndef BRPC_RDMA_BLOCK_POOL_H
#define BRPC_RDMA_BLOCK_POOL_H

#if BRPC_WITH_RDMA

namespace brpc {
namespace rdma {

// This is used as a memory pool for RDMA. The reason why we use memory
// pool is that RDMA transmission requires data in a "registered" space.
// We first get a large bulk of memory by tcmalloc (or other memory
// allocator). Then we call ibv_reg_mr to register this bulk. Every time we
// want to use a piece of memory to send/recv with RDMA, we allocate it from
// memory pool instead of tcmalloc.
//
// It is called block_pool due to the unit of memory allocation is block,
// not byte. That means that when the caller wants to get a piece of
// memory smaller than the block size, the pool will still return a whole
// block for it. Apparently, this mechanism may introduce waste of memory.
// However, since in brpc the memory pool is only used by IOBuf, which
// always requires block allocation, we believe that block_pool is a better
// design than the byte-based memory pool.
//
// Because the initial size of block_pool (default: 1GB) may not enough, we
// hope that the pool is scalable, which means that it can be enlarged when
// there is no more memory in the pool. Therefore, we introduce the concept
// of region. Every bulk of memory got from tcmalloc is called a region.
// And the region is the unit of RDMA registration. The caller must be able
// to get the LKey of the region from the pool, which we call it region ID.
//
// Since IOBuf supports different block size, the block_pool also supports
// several block sizes: 8KB(default), 64KB and 2MB. The block allocated to
// the caller is the block with minimum size which is larger than the
// applied size. For example, if the caller needs a buffer with a size of
// 9KB, block_pool will allocate a 64KB-block for it. Please remember that
// different-size blocks are in different regions.
//
// Currently, the block_pool supports 16 regions at most. If there is more than
// one region, the complexity of finding which region an address belongs to
// is O(n). Here n is the number of regions. In order to avoid race conditions
// among threads, we do not use more efficient search data structure.
// Therefore, DO NOT rely on the scalable feature of block_pool too much. The
// developper should estimate the consumption of memory used for RDMA in
// advance as possible as she/he can. Besides, if it is possible, please use
// one size of blocks only.
//
// The block_pool is thread-safe, so that the caller can call it in different
// threads. However, before calling allocation and deallocation, the caller
// must call initialization of the block_pool. Otherwise the behavior is
// undefined.

typedef uint32_t (*RegisterCallback)(void*, size_t);

// Initialize the block pool
// The argument is a callback called when the pool is enlarged with a new
// region. It should be the memory registration in brpc. However,
// in block_pool, we just abstract it into a function to get region id.
// Return the first region's address, NULL if failed and errno is set.
void* InitBlockPool(RegisterCallback cb);

// Allocate a buf with length at least @a size (require: size>0)
// Return the address allocated, NULL if failed and errno is set.
void* AllocBlock(size_t size);

// Deallocate the buf (require: buf!=NULL)
// Return 0 if success, -1 if failed and errno is set.
// If the given buf is not in any region, the errno is ERANGE.
int DeallocBlock(void* buf);

// Get the region ID of the given buf
uint32_t GetRegionId(const void* buf);

// Return the block size of given block type
// type=1: BLOCK_DEFAULT(8KB)
// type=2: BLOCK_LARGE(64KB)
// type=3: BLOCK_HUGE(2MB)
size_t GetBlockSize(int type);

// Dump memory pool information
void DumpMemoryPoolInfo(std::ostream& os);

}  // namespace rdma
}  // namespace brpc

#endif  // if BRPC_WITH_RDMA

#endif  // BRPC_RDMA_BLOCK_POOL_H
