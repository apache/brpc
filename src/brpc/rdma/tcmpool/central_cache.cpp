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

#include "central_cache.h"

#include "brpc/rdma/tcmpool/const.h"
#include "brpc/rdma/tcmpool/thread_cache.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

CentralCache::CentralCache(PageHeap* page_heap)
    : _table(SIZE_CLASS_NUM)
    , _page_heap(page_heap)
{
    pthread_mutex_init(&_lock, NULL);
}

CentralCache::~CentralCache() {
    pthread_mutex_destroy(&_lock);
    _table.clear();
}

inline int get_size(int size_class) {
    return MIN_OBJECT_SIZE << size_class;
}

int CentralCache::move_to(int size_class, ThreadCache* tc) {
    pthread_mutex_lock(&_lock);
    if (_table[size_class].empty()) {
        get_from_heap(size_class);
    }
    int i = 0;
    size_t total_size = 0;
    while (!_table[size_class].empty()) {
        tc->_table[size_class].splice_after(tc->_table[size_class].before_begin(),
                                            _table[size_class],
                                            _table[size_class].before_begin(),
                                           std::next(_table[size_class].begin()));
        i++;
        total_size += get_size(size_class);
        if (total_size >= THRESHOLD_SMALL_OBJECT) {
            break;
        }
    }
    pthread_mutex_unlock(&_lock);
    return i;
}

void CentralCache::recycle(ThreadCache* tc) {
    pthread_mutex_lock(&_lock);
    for (int i = 0; i < SIZE_CLASS_NUM; i++) {
        if (tc->_table[i].empty()) {
            continue;
        }
        _table[i].splice_after(_table[i].before_begin(), tc->_table[i]);
    }
    pthread_mutex_unlock(&_lock);
}

void CentralCache::recycle2(ThreadCache* tc) {
    pthread_mutex_lock(&_lock);
    for (int i = 0; i < SIZE_CLASS_NUM; i++) {
        if (tc->_table[i].empty()) {
            continue;
        }
        int nodes = tc->_last_recycle_nodes[i] / 2;
        if (nodes > 0) {
            _table[i].splice_after(_table[i].before_begin(), tc->_table[i],
                                    tc->_table[i].before_begin(),
                                    std::next(tc->_table[i].begin(), nodes));
            tc->_nodes[i] -= nodes;
            tc->_size -= nodes * get_size(i);
        }
        tc->_last_recycle_nodes[i] = tc->_nodes[i];
    }
    pthread_mutex_unlock(&_lock);
}

void CentralCache::get_from_heap(int size_class) {
    int object_size = get_size(size_class);
    void* buf = _page_heap->alloc(object_size);
    if (!buf) {
        return;
    }
    int object_num = std::max((int)PAGE_SIZE / object_size, 1);
    for (int i = 0; i < object_num; i++) {
        _table[size_class].emplace_front((uint8_t*)buf + object_size * i);
    }
}

void CentralCache::print(std::ostream& os) {
    os << "CentralCache Dump: " << std::endl;
    for (int i = 0; i < SIZE_CLASS_NUM; i++) {
        int cnt = 0;
        for (FreeList::iterator it = _table[i].begin();
                it != _table[i].end(); it++) {
            cnt++;
        }
        if (cnt > 0) {
            os << "size: " << get_size(i) << ", free: " << cnt << std::endl;
        }
    }
}

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

