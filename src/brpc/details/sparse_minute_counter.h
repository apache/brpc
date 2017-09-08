// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_SPARSE_MINUTE_COUNTER_H
#define BRPC_SPARSE_MINUTE_COUNTER_H


#include "butil/containers/bounded_queue.h"

namespace brpc {

// An utility to add up per-second value into per-minute value.
// About "sparse":
//   This utility assumes that when a lot of instances exist, many of them do
//   not need to update every second. This is true for stats of connections.
//   When a lot of connections(>100K) connected to one server, most of them
//   are idle since the overall actitivities of all connections are limited
//   by throughput of the server. To make use of the fact, this utility stores
//   per-second values in a sparse array tagged with timestamps. The array
//   is resized on-demand to save memory.
template <typename T> class SparseMinuteCounter {
    struct Item {
        int64_t timestamp_ms;
        T value;
        Item() : timestamp_ms(0) {}
        Item(int64_t ts, const T& v) : timestamp_ms(ts), value(v) {}
    };
public:
    SparseMinuteCounter() : _q(NULL) {}
    ~SparseMinuteCounter() { DestroyQueue(_q); }

    // Add `value' into this counter at timestamp `now_ms'
    // Returns true when old value is popped and set into *popped.
    bool Add(int64_t now_ms, const T& value, T* popped);
    
    // Try to pop value before one minute.
    // Returns true when old value is popped and set into *popped.
    bool TryPop(int64_t now_ms, T* popped);

private:
    DISALLOW_COPY_AND_ASSIGN(SparseMinuteCounter);
    typedef butil::BoundedQueue<Item> Q;
    static Q* CreateQueue(uint32_t cap);
    static void DestroyQueue(Q* q);
    void Resize();

    Q* _q;
    Item _first_item;
};

template <typename T>
bool SparseMinuteCounter<T>::Add(int64_t now_ms, const T& val, T* popped) {
    if (_q) { // more common
        Item new_item(now_ms, val);
        if (_q->full()) {
            const Item* const oldest = _q->top();
            if (now_ms < oldest->timestamp_ms + 60000 &&
                _q->capacity() < 60) {
                Resize();
                _q->push(new_item);
                return false;
            } else {
                *popped = oldest->value;
                _q->pop();
                _q->push(new_item);
                return true;
            }
        } else {
            _q->push(new_item);
            return false;
        }
    } else {
        // first-time storing is different. If Add() is rarely called,
        // This strategy may not allocate _q at all.
        if (_first_item.timestamp_ms == 0) {
            _first_item.timestamp_ms = std::max(now_ms, (int64_t)1);
            _first_item.value = val;
            return false;
        }
        const int64_t delta = now_ms - _first_item.timestamp_ms;
        if (delta >= 60000) {
            *popped = _first_item.value;
            _first_item.timestamp_ms = std::max(now_ms, (int64_t)1);
            _first_item.value = val;
            return true;
        }
        // Predict initial capacity of _q according to interval between
        // now_ms and last timestamp.
        int64_t initial_cap = (delta <= 1000 ? 30 : (60000 + delta - 1) / delta);
        if (initial_cap < 4) {
            initial_cap = 4;
        }
        _q = CreateQueue(initial_cap);
        _q->push(_first_item);
        _q->push(Item(now_ms, val));
        return false;
    }
}

template <typename T>
typename SparseMinuteCounter<T>::Q*
SparseMinuteCounter<T>::CreateQueue(uint32_t cap) {
    const size_t memsize =
        sizeof(Q) + sizeof(Item) * cap;
    char* mem = (char*)malloc(memsize); // intended crash on ENOMEM
    return new (mem) Q(mem + sizeof(Q), sizeof(Item) * cap, butil::NOT_OWN_STORAGE);
}

template <typename T>
void SparseMinuteCounter<T>::DestroyQueue(Q* q) {
    if (q) {
        q->~Q();
        free(q);
    }
}

template <typename T>
void SparseMinuteCounter<T>::Resize() {
    CHECK_LT(_q->capacity(), (size_t)60);
    uint32_t new_cap = std::min(2 * (uint32_t)_q->capacity(), 60u);
    Q* new_q = CreateQueue(new_cap);
    for (size_t i = 0; i < _q->size(); ++i) {
        new_q->push(*_q->top(i));
    }
    DestroyQueue(_q);
    _q = new_q;
}

template <typename T>
bool SparseMinuteCounter<T>::TryPop(int64_t now_ms, T* popped) {
    if (_q) {
        const Item* const oldest = _q->top();
        if (oldest == NULL || now_ms < oldest->timestamp_ms + 60000) {
            return false;
        }
        *popped = oldest->value;
        _q->pop();
        return true;
    } else {
        if (_first_item.timestamp_ms == 0 ||
            now_ms < _first_item.timestamp_ms + 60000) {
            return false;
        }
        _first_item.timestamp_ms = 0;
        *popped = _first_item.value;
        return true;
    }
}

} // namespace brpc


#endif  // BRPC_SPARSE_MINUTE_COUNTER_H
