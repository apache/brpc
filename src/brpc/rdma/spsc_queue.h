// Copyright (c) 2014 baidu-rpc authors.
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

#ifndef BRPC_RDMA_SPSC_QUEUE_H
#define BRPC_RDMA_SPSC_QUEUE_H

#include "butil/atomicops.h"

namespace brpc {
namespace rdma {

// This is a single producer single consumer queue.
template <class T>
struct SpscQueue {
public:
    SpscQueue()
        : _bottom(1)
        , _capacity(0)
        , _buffer(NULL)
        , _top(1) {
    }

    ~SpscQueue() {
        delete [] _buffer;
    }

    int init(size_t capacity) {
        if (_capacity != 0) {
            LOG(ERROR) << "Already initialized";
            return -1;
        }
        if (capacity == 0) {
            LOG(ERROR) << "Invalid capacity=" << capacity;
            return -1;
        }
        _buffer = new(std::nothrow) T[capacity];
        if (NULL == _buffer) {
            return -1;
        }
        _capacity = capacity;
        return 0;
    }

    void reset() {
        delete [] _buffer;
        _buffer = NULL;
        _bottom.store(0, butil::memory_order_relaxed);
        _top.store(0, butil::memory_order_relaxed);
        _capacity = 0;
    }

    bool push(const T& x) {
        const size_t b = _bottom.load(std::memory_order_relaxed);
        size_t next = b + 1;
        if (next == _capacity) {
            next = 0;
        }
        const size_t t = _top.load(butil::memory_order_acquire);
        if (next == t) {
            // queue is full
            return false;
        }
        _buffer[b] = x;
        _bottom.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& val) {
        const size_t t = _top.load(std::memory_order_relaxed);
        if (t == _bottom.load(std::memory_order_acquire)) {
            // queue is empty
            return false;
        }

        val = _buffer[t];
        size_t next = t + 1;
        if (next == _capacity) {
            next = 0;
        }
        _top.store(next, std::memory_order_release);
        return true;
    }

    size_t capacity() const { return  _capacity; }

private:
    DISALLOW_COPY_AND_ASSIGN(SpscQueue);

    butil::atomic<size_t> _bottom;
    size_t _capacity;
    T* _buffer;
    butil::atomic<size_t> BAIDU_CACHELINE_ALIGNMENT _top;

};

} // namespace rdma
} // namespace brpc

#endif
