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

#include "page_heap.h"

#include <stdlib.h>

#include "brpc/rdma/tcmpool/const.h"

namespace brpc {
namespace rdma {
namespace tcmpool {

PageHeap::PageHeap(void* heap, size_t size)
    : _heap(heap)
    , _heap_size(size)
    , _table(MAX_PAGE_NUM)
{
    pthread_mutex_init(&_lock, NULL);
    size_t actual_size = size / PAGE_SIZE;
    _span_index = (PageSpan**)calloc(actual_size, sizeof(PageSpan*));
    if (!_span_index) {
        return;
    }
    if (actual_size >= MAX_PAGE_NUM) {
        _table[MAX_PAGE_NUM - 1].emplace_front(0, actual_size, heap);
        _span_index[0] = &_table[MAX_PAGE_NUM - 1].front();
        _span_index[actual_size - 1] = &_table[MAX_PAGE_NUM - 1].front();
    } else {
        _table[actual_size - 1].emplace_front(0, actual_size, heap);
        _span_index[0] = &_table[actual_size - 1].front();
        _span_index[actual_size - 1] = &_table[actual_size - 1].front();
    }
}

PageHeap::~PageHeap() {
    if (_span_index) {
        free(_span_index);
        _span_index = NULL;
    }
    pthread_mutex_destroy(&_lock);
}

inline int get_page_num(size_t len) {
    return (len - 1) / PAGE_SIZE + 1;
}

void* PageHeap::alloc(size_t len) {
    int page_num = get_page_num(len);
    int num = std::min(page_num, MAX_PAGE_NUM);

    pthread_mutex_lock(&_lock);
    while (true) {
        while (_table[num - 1].empty()) {
            num++;
            if (num > MAX_PAGE_NUM) {
                pthread_mutex_unlock(&_lock);
                return NULL;
            }
        }
        FreeList::iterator prev = _table[num - 1].before_begin();
        FreeList::iterator it = _table[num - 1].begin();
        int left = 0;
        while (it != _table[num - 1].end()) {
            left = (int)(it->end - it->start) - page_num;
            if (left >= 0) {
                break;
            }
            prev = it;
            it = std::next(it);
        }
        if (it == _table[num - 1].end()) {
            num++;
            if (num > MAX_PAGE_NUM) {
                pthread_mutex_unlock(&_lock);
                return NULL;
            }
            continue;
        }
        int start = it->start;
        void* addr = it->addr;
        int left_start = start + page_num;
        int left_end = it->end;
        void* left_addr = (uint8_t*)it->addr + PAGE_SIZE * page_num;
        _table[num - 1].erase_after(prev);
        if (left > MAX_PAGE_NUM) {
            _table[MAX_PAGE_NUM - 1].emplace_front(left_start, left_end, left_addr);
            _span_index[left_start] =  &_table[MAX_PAGE_NUM - 1].front();
            _span_index[left_start + left - 1] =  &_table[MAX_PAGE_NUM - 1].front();
        } else if (left > 0) {
            _table[left - 1].emplace_front(left_start, left_end, left_addr);
            _span_index[left_start] =  &_table[left - 1].front();
            _span_index[left_start + left - 1] =  &_table[left - 1].front();
        }
        _span_index[start] = NULL;
        _span_index[start + page_num - 1] = NULL;
        pthread_mutex_unlock(&_lock);

        size_t* meta = (size_t*)addr;
        *meta = page_num * PAGE_SIZE;
        return addr;
    }
}

void PageHeap::dealloc(void* buf, size_t len) {
    int page_num = get_page_num(len);

    // find the offset first
    size_t page_start = ((uint8_t*)buf - (uint8_t*)_heap) / PAGE_SIZE;
    size_t page_end = page_start + page_num;

    // find the neighbours
    PageSpan* before = NULL;
    PageSpan* after = NULL;

    pthread_mutex_lock(&_lock);
    if (page_start > 0) {
        before = _span_index[page_start - 1];
    }
    if (page_end < _heap_size / PAGE_SIZE) {
        after = _span_index[page_end];
    }

    // check if we need merge
    int actual_page_num = page_num;
    int actual_start = page_start;
    int erase_page_num1 = -1;
    int erase_page_num2 = -1;
    int erase_start1 = -1;
    int erase_start2 = -1;
    if (before && !after) {
        actual_start = before->start;
        actual_page_num += before->end - before->start;
        erase_page_num1 = std::min(before->end - before->start, MAX_PAGE_NUM);
        erase_start1 = before->start;
    } else if (!before && after) {
        actual_page_num += after->end - after->start;
        erase_page_num2 = std::min(after->end - after->start, MAX_PAGE_NUM);
        erase_start2 = after->start;
    } else if (before && after) {
        actual_start = before->start;
        actual_page_num += before->end - before->start;
        actual_page_num += after->end - after->start;
        erase_page_num1 = std::min(before->end - before->start, MAX_PAGE_NUM);
        erase_page_num2 = std::min(after->end - after->start, MAX_PAGE_NUM);
        erase_start1 = before->start;
        erase_start2 = after->start;
    }
    if (erase_page_num1 > 0) {
        auto prev = _table[erase_page_num1 - 1].before_begin();
        for (auto it = _table[erase_page_num1 - 1].begin();
                it != _table[erase_page_num1 - 1].end();
                it = std::next(it)) {
            if (it->start == erase_start1) {
                _table[erase_page_num1 - 1].erase_after(prev);
                break;
            }
            prev = it;
        }
    }
    if (erase_page_num2 > 0) {
        auto prev = _table[erase_page_num2 - 1].before_begin();
        for (auto it = _table[erase_page_num2 - 1].begin();
                it != _table[erase_page_num2 - 1].end();
                it = std::next(it)) {
            if (it->start == erase_start2) {
                _table[erase_page_num2 - 1].erase_after(prev);
                break;
            }
            prev = it;
        }
    }

    // reinsert the span
    if (actual_page_num > MAX_PAGE_NUM) {
        _table[MAX_PAGE_NUM - 1].emplace_front(actual_start, actual_start + actual_page_num,
                                               (uint8_t*)_heap + PAGE_SIZE * actual_start);
        _span_index[actual_start] = &_table[MAX_PAGE_NUM - 1].front();
        _span_index[actual_start + actual_page_num - 1] = &_table[MAX_PAGE_NUM - 1].front();
    } else {
        _table[actual_page_num - 1].emplace_front(actual_start, actual_start + actual_page_num,
                                                  (uint8_t*)_heap + PAGE_SIZE * actual_start);
        _span_index[actual_start] = &_table[actual_page_num - 1].front();
        _span_index[actual_start + actual_page_num - 1] = &_table[actual_page_num - 1].front();
    }
    pthread_mutex_unlock(&_lock);
}

void PageHeap::print(std::ostream& os) {
    os << "PageHeap Dump: " << std::endl;
    for (int i = 0; i < MAX_PAGE_NUM; i++) {
        int cnt = 0;
        for (FreeList::iterator it = _table[i].begin();
                it != _table[i].end(); it++) {
            cnt++;
            os << "(" << it->start << ", " << it->end << ") ";
        }
        if (cnt > 0) {
            os << "| " << i + 1 << " Pages" << std::endl;
        }
    }
}

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

