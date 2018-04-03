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

#ifndef BRPC_RDMATCMPOOL_THREADCACHE_H
#define BRPC_RDMATCMPOOL_THREADCACHE_H

#include <cstddef>
#include <forward_list>
#include <iostream>
#include <vector>

#include "brpc/rdma/tcmpool/central_cache.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

struct FreeNode{
    void* addr;
    FreeNode(void* buf) : addr(buf) { }
};

class ThreadCache {
friend class CentralCache;
friend bool init_memory_pool(void*, size_t);
friend void release_memory_pool();
public:
    explicit ThreadCache(CentralCache* central_cache);
    ~ThreadCache();

    // allocate a buf with length @a len
    // return the address of the buf, NULL if failed
    void* alloc(size_t len);

    // deallocate @a buf with length @a len
    void dealloc(void* buf, size_t len);

    void release();

private:
    ThreadCache(const ThreadCache&) = delete;
    ThreadCache& operator=(const ThreadCache&) = delete;

    // for debug
    void print(std::ostream& os);

    typedef std::forward_list<FreeNode> FreeList;
    std::vector<FreeList> _table;
    std::vector<int> _nodes;
    std::vector<int> _last_recycle_nodes;
    uint32_t _size;

    // only a handle
    CentralCache* _central_cache;
};

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

#endif
