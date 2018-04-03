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

#include "thread_cache.h"

#include "brpc/rdma/tcmpool/const.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

ThreadCache::ThreadCache(CentralCache* central_cache)
    : _table(SIZE_CLASS_NUM)
    , _nodes(SIZE_CLASS_NUM)
    , _last_recycle_nodes(SIZE_CLASS_NUM)
    , _size(0)
    , _central_cache(central_cache)
{
}

ThreadCache::~ThreadCache() {
}

void ThreadCache::release() {
    if (_central_cache) {
        _central_cache->recycle(this);
    }
}

inline int get_class(size_t len) {
    int size_class = 0;
    while (len > (MIN_OBJECT_SIZE << size_class)) {
        size_class++;
    }
    return size_class;
}

void* ThreadCache::alloc(size_t len) {
    int size_class = get_class(len);
    if (_table[size_class].empty()) {
        _last_recycle_nodes[size_class] = 0;
        _nodes[size_class] = _central_cache->move_to(size_class, this);
        _size += _nodes[size_class] * (MIN_OBJECT_SIZE << size_class);
        if (_table[size_class].empty()) {
            return NULL;
        }
    } else {
        if (_nodes[size_class] < _last_recycle_nodes[size_class]) {
            _last_recycle_nodes[size_class] = _nodes[size_class];
        }
    }
    _size -= MIN_OBJECT_SIZE << size_class;
    _nodes[size_class]--;
    FreeNode node = _table[size_class].front();
    _table[size_class].pop_front();
    size_t* meta = (size_t*)node.addr;
    *meta = MIN_OBJECT_SIZE << size_class;
    return node.addr;
}

void ThreadCache::dealloc(void* buf, size_t len) {
    int size_class = get_class(len);
    _table[size_class].emplace_front(buf);
    _size += len;
    _nodes[size_class]++;
    if (_size > RECYCLE_THRESHOLD) {
        _central_cache->recycle2(this);
    }
}

void ThreadCache::print(std::ostream& os) {
    os << "ThreadCache Dump: " << std::endl;
    for (int i = 0; i < SIZE_CLASS_NUM; i++) {
        int cnt = 0;
        for (FreeList::iterator it = _table[i].begin();
                it != _table[i].end(); it++) {
            cnt++;
        }
        if (cnt > 0) {
            os << "size class: " << i << ", free: " << cnt << std::endl;
        }
    }
}

}  // namespace tcmpool
}  // namepsace rdma
}  // namespace brpc

