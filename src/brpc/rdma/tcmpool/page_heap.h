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

#ifndef BRPC_RDMATCMPOOL_PAGEHEAP_H
#define BRPC_RDMATCMPOOL_PAGEHEAP_H

#include <pthread.h>
#include <cstddef>
#include <forward_list>
#include <iostream>
#include <vector>

namespace brpc {
namespace rdma {
namespace tcmpool {

bool init_memory_pool(size_t size);

class PageHeap {
friend bool init_memory_pool(size_t size, void* buf);
public:
    PageHeap(void* heap, size_t size);
    ~PageHeap();

    // allocate a buf with length @a len
    // return the address of the buf, NULL if failed
    void* alloc(size_t len);

    // deallocate @ buf with length @a len
    void dealloc(void* buf, size_t len);

private:
    PageHeap(const PageHeap&) = delete;
    PageHeap& operator=(const PageHeap&) = delete;

    struct PageSpan {
        int start;
        int end;
        void* addr;
        PageSpan(int s, int e, void* p) : start(s), end(e), addr(p) { }
    };

    // for debug
    void print(std::ostream& os);

    typedef std::forward_list<PageSpan> FreeList;

    void* _heap;
    size_t _heap_size;
    std::vector<FreeList> _table;
    PageSpan** _span_index;
    pthread_mutex_t _lock;
};

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

#endif
