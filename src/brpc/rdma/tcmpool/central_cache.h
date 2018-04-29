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

#ifndef BRPC_RDMATCMPOOL_CENTRALCACHE_H
#define BRPC_RDMATCMPOOL_CENTRALCACHE_H

#include <pthread.h>
#include <forward_list>
#include <iostream>
#include <vector>

#include "brpc/rdma/tcmpool/page_heap.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

class ThreadCache;
class FreeNode;

class CentralCache {
public:
    explicit CentralCache(PageHeap *heap);
    ~CentralCache();

    // move some freenode of @a size_class to thread cache @a tc
    // return the number of objects moved
    int move_to(int size_class, ThreadCache* tc);

    // get all freenodes back from thread cache @a tc
    void recycle(ThreadCache* tc);

    // get some freenodes back from thread cache @a tc
    void recycle2(ThreadCache* tc);

private:
    CentralCache(const CentralCache&) = delete;
    CentralCache& operator=(const CentralCache&) = delete;

    // get some freenode from page heap
    void get_from_heap(int size_class);

    // for debug
    void print(std::ostream& os);

    typedef std::forward_list<FreeNode> FreeList;
    std::vector<FreeList> _table;

    // only a handle
    PageHeap* _page_heap;
    pthread_mutex_t _lock;
};

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

#endif
