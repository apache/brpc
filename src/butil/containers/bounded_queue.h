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

// Date: Sat Aug 18 12:42:16 CST 2012

// A thread-unsafe bounded queue(ring buffer). It can push/pop from both
// sides and is more handy than thread-safe queues in single thread. Use
// boost::lockfree::spsc_queue or boost::lockfree::queue in multi-threaded
// scenarios.

#ifndef BUTIL_BOUNDED_QUEUE_H
#define BUTIL_BOUNDED_QUEUE_H

#include "butil/macros.h"
#include "butil/logging.h"

namespace butil {

// [Create a on-stack small queue]
//   char storage[64];
//   butil::BoundedQueue<int> q(storage, sizeof(storage), butil::NOT_OWN_STORAGE);
//   q.push(1);
//   q.push(2);
//   ...
   
// [Initialize a class-member queue]
//   class Foo {
//     ...
//     BoundQueue<int> _queue;
//   };
//   int Foo::init() {
//     BoundedQueue<int> tmp(capacity);
//     if (!tmp.initialized()) {
//       LOG(ERROR) << "Fail to create _queue";
//       return -1;
//     }
//     tmp.swap(_queue);
//   }

enum StorageOwnership { OWNS_STORAGE, NOT_OWN_STORAGE };

template <typename T>
class BoundedQueue {
public:
    // You have to pass the memory for storing items at creation.
    // The queue contains at most memsize/sizeof(T) items.
    BoundedQueue(void* mem, size_t memsize, StorageOwnership ownership)
        : _count(0)
        , _cap(memsize / sizeof(T))
        , _start(0)
        , _ownership(ownership)
        , _items(mem) {
        DCHECK(_items);
    };
    
    // Construct a queue with the given capacity.
    // The malloc() may fail silently, call initialized() to test validity
    // of the queue.
    explicit BoundedQueue(size_t capacity)
        : _count(0)
        , _cap(capacity)
        , _start(0)
        , _ownership(OWNS_STORAGE)
        , _items(malloc(capacity * sizeof(T))) {
        DCHECK(_items);
    };
    
    BoundedQueue()
        : _count(0)
        , _cap(0)
        , _start(0)
        , _ownership(NOT_OWN_STORAGE)
        , _items(NULL) {
    };

    ~BoundedQueue() {
        clear();
        if (_ownership == OWNS_STORAGE) {
            free(_items);
            _items = NULL;
        }
    }

    // Push |item| into bottom side of this queue.
    // Returns true on success, false if queue is full.
    bool push(const T& item) {
        if (_count < _cap) {
            new ((T*)_items + _mod(_start + _count, _cap)) T(item);
            ++_count;
            return true;
        }
        return false;
    }

    // Push |item| into bottom side of this queue. If the queue is full,
    // pop topmost item first.
    void elim_push(const T& item) {
        if (_count < _cap) {
            new ((T*)_items + _mod(_start + _count, _cap)) T(item);
            ++_count;
        } else {
            ((T*)_items)[_start] = item;
            _start = _mod(_start + 1, _cap);
        }
    }
    
    // Push a default-constructed item into bottom side of this queue
    // Returns address of the item inside this queue
    T* push() {
        if (_count < _cap) {
            return new ((T*)_items + _mod(_start + _count++, _cap)) T();
        }
        return NULL;
    }

    // Push |item| into top side of this queue
    // Returns true on success, false if queue is full.
    bool push_top(const T& item) {
        if (_count < _cap) {
            _start = _start ? (_start - 1) : (_cap - 1);
            ++_count;
            new ((T*)_items + _start) T(item);
            return true;
        }
        return false;
    }    
    
    // Push a default-constructed item into top side of this queue
    // Returns address of the item inside this queue
    T* push_top() {
        if (_count < _cap) {
            _start = _start ? (_start - 1) : (_cap - 1);
            ++_count;
            return new ((T*)_items + _start) T();
        }
        return NULL;
    }
    
    // Pop top-most item from this queue
    // Returns true on success, false if queue is empty
    bool pop() {
        if (_count) {
            --_count;
            ((T*)_items + _start)->~T();
            _start = _mod(_start + 1, _cap);
            return true;
        }
        return false;
    }

    // Pop top-most item from this queue and copy into |item|.
    // Returns true on success, false if queue is empty
    bool pop(T* item) {
        if (_count) {
            --_count;
            T* const p = (T*)_items + _start;
            *item = *p;
            p->~T();
            _start = _mod(_start + 1, _cap);
            return true;
        }
        return false;
    }

    // Pop bottom-most item from this queue
    // Returns true on success, false if queue is empty
    bool pop_bottom() {
        if (_count) {
            --_count;
            ((T*)_items + _mod(_start + _count, _cap))->~T();
            return true;
        }
        return false;
    }

    // Pop bottom-most item from this queue and copy into |item|.
    // Returns true on success, false if queue is empty
    bool pop_bottom(T* item) {
        if (_count) {
            --_count;
            T* const p = (T*)_items + _mod(_start + _count, _cap);
            *item = *p;
            p->~T();
            return true;
        }
        return false;
    }

    // Pop all items
    void clear() {
        for (uint32_t i = 0; i < _count; ++i) {
            ((T*)_items + _mod(_start + i, _cap))->~T();
        }
        _count = 0;
        _start = 0;
    }

    // Get address of top-most item, NULL if queue is empty
    T* top() { 
        return _count ? ((T*)_items + _start) : NULL; 
    }
    const T* top() const { 
        return _count ? ((const T*)_items + _start) : NULL; 
    }

    // Randomly access item from top side.
    // top(0) == top(), top(size()-1) == bottom()
    // Returns NULL if |index| is out of range.
    T* top(size_t index) {
        if (index < _count) {
            return (T*)_items + _mod(_start + index, _cap);
        }
        return NULL;   // including _count == 0
    }
    const T* top(size_t index) const {
        if (index < _count) {
            return (const T*)_items + _mod(_start + index, _cap);
        }
        return NULL;   // including _count == 0
    }

    // Get address of bottom-most item, NULL if queue is empty
    T* bottom() { 
        return _count ? ((T*)_items + _mod(_start + _count - 1, _cap)) : NULL; 
    }
    const T* bottom() const {
        return _count ? ((const T*)_items + _mod(_start + _count - 1, _cap)) : NULL; 
    }
    
    // Randomly access item from bottom side.
    // bottom(0) == bottom(), bottom(size()-1) == top()
    // Returns NULL if |index| is out of range.
    T* bottom(size_t index) {
        if (index < _count) {
            return (T*)_items + _mod(_start + _count - index - 1, _cap);
        }
        return NULL;  // including _count == 0
    }
    const T* bottom(size_t index) const {
        if (index < _count) {
            return (const T*)_items + _mod(_start + _count - index - 1, _cap);
        }
        return NULL;  // including _count == 0
    }

    bool empty() const { return !_count; }
    bool full() const { return _cap == _count; }

    // Number of items
    size_t size() const { return _count; }

    // Maximum number of items that can be in this queue
    size_t capacity() const { return _cap; }

    // Maximum value of capacity()
    size_t max_capacity() const { return (1UL << (sizeof(_cap) * 8)) - 1; }

    // True if the queue was constructed successfully.
    bool initialized() const { return _items != NULL; }

    // Swap internal fields with another queue.
    void swap(BoundedQueue& rhs) {
        std::swap(_count, rhs._count);
        std::swap(_cap, rhs._cap);
        std::swap(_start, rhs._start);
        std::swap(_ownership, rhs._ownership);
        std::swap(_items, rhs._items);
    }

private:
    // Since the space is possibly not owned, we disable copying.
    DISALLOW_COPY_AND_ASSIGN(BoundedQueue);
    
    // This is faster than % in this queue because most |off| are smaller
    // than |cap|. This is probably not true in other place, be careful
    // before you use this trick.
    static uint32_t _mod(uint32_t off, uint32_t cap) {
        while (off >= cap) {
            off -= cap;
        }
        return off;
    }
    
    uint32_t _count;
    uint32_t _cap;
    uint32_t _start;
    StorageOwnership _ownership;
    void* _items;
};

}  // namespace butil

#endif  // BUTIL_BOUNDED_QUEUE_H
