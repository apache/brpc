// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BAIDU_BTHREAD_WORK_STEALING_QUEUE_H
#define BAIDU_BTHREAD_WORK_STEALING_QUEUE_H

#include "base/macros.h"
#include "base/atomicops.h"

namespace bthread {

template <typename T>
class WorkStealingQueue {
public:
    WorkStealingQueue()
        : _bottom(1)
        , _top(1)
        , _capacity(0)
        , _buffer(NULL) {
    }

    ~WorkStealingQueue() {
        delete [] _buffer;
        _buffer = NULL;
    }

    int init(size_t capacity) {
        if (_capacity != 0) {  // already initialized.
            return -1;
        }
        if (capacity == 0) {  // invalid cap
            return -1;
        }
        _buffer = new(std::nothrow) T[capacity];
        if (NULL == _buffer) {
            return -1;
        }
        _capacity = capacity;
        return 0;
    }

    bool push(const T& x) {
        const size_t b = _bottom.load(base::memory_order_relaxed);
        const size_t t = _top.load(base::memory_order_acquire);
        if (b - t >= _capacity) { // Full queue.
            return false;
        }
        _buffer[b % _capacity] = x;
        _bottom.store(b + 1, base::memory_order_release);
        return true;
    }

    bool pop(T* val) {
        const size_t b = _bottom.load(base::memory_order_relaxed) - 1;
        _bottom.store(b, base::memory_order_relaxed);
        base::atomic_thread_fence(base::memory_order_seq_cst);
        size_t t = _top.load(base::memory_order_relaxed);
        bool popped = false;
        if (t <= b) {
            // Non-empty queue.
            *val = _buffer[b % _capacity];
            if (t != b) {
                return true;
            }
            // Single last element in queue.
            popped = _top.compare_exchange_strong(t, t + 1,
                                                  base::memory_order_seq_cst,
                                                  base::memory_order_relaxed);
        }
        _bottom.store(b + 1, base::memory_order_relaxed);
        return popped;
    }

    bool steal(T* val) {
        size_t t = _top.load(base::memory_order_acquire);
        base::atomic_thread_fence(base::memory_order_seq_cst);
        const size_t b = _bottom.load(base::memory_order_acquire);
        if (t >= b) {
            return false;
        }
        *val = _buffer[t % _capacity];
        return _top.compare_exchange_strong(t, t + 1,
                                            base::memory_order_seq_cst,
                                            base::memory_order_relaxed);
    }

    size_t volatile_size() const {
        const size_t b = _bottom.load(base::memory_order_relaxed);
        const size_t t = _top.load(base::memory_order_relaxed);
        return (b < t ? 0 : (b - t));
    }

    size_t capacity() const { return _capacity; }

private:
    // Copying a concurrent structure makes no sense.
    DISALLOW_COPY_AND_ASSIGN(WorkStealingQueue);

    base::atomic<size_t> _bottom;
    base::atomic<size_t> _top;
    size_t _capacity;
    T* _buffer;
};

}  // namespace bthread

#endif  // BAIDU_BTHREAD_WORK_STEALING_QUEUE_H

