// Copyright (c) 2018 Baidu Inc.
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

#ifndef BRPC_RDMATCMPOOL_MEMORYPOOL_H
#define BRPC_RDMATCMPOOL_MEMORYPOOL_H

#include <cstddef>

namespace brpc {
namespace rdma {
namespace tcmpool {

// initialize the memory pool with @a size (in bytes)
// if @a buf is not NULL, use it as the pool directly
// return true if success, return false if 
//   1. init failed (e.g. ENOMEM)
//   2. memory pool has been initialized before
//   3. size < 4MB
//
// NOTE:
// 1. We do not provide destroy API. The memory pool is destroyed when the
// thread which calls this function exits. So please call this function in
// main thread if you do not want this to happen.
// 2. If @a buf is not NULL, the free operation will not be called in
// destruction.
bool init_memory_pool(size_t size, void* buf = NULL);

// get the address of the memory pool
void* get_memory_pool_addr();

// get the size of the memory pool
size_t get_memory_pool_size();

// allocate a buf with length @a len
void* alloc(size_t len);

// free the allocated @a buf
void dealloc(void* buf);

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

#endif
