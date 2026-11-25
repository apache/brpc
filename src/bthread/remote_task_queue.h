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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Sun, 22 Jan 2017

#ifndef BTHREAD_REMOTE_TASK_QUEUE_H
#define BTHREAD_REMOTE_TASK_QUEUE_H

#include "butil/containers/bounded_queue.h"
#include "butil/atomicops.h"
#include "butil/macros.h"

namespace bthread {

class TaskGroup;

// A queue for storing bthreads created by non-workers. Since non-workers
// randomly choose a TaskGroup to push which distributes the contentions,
// this queue is simply implemented as a queue protected with a lock.
// The function names should be self-explanatory.
class RemoteTaskQueue {
public:
    RemoteTaskQueue() : _ntask(0) {}

    int init(size_t cap) {
        const size_t memsize = sizeof(bthread_t) * cap;
        void* q_mem = malloc(memsize);
        if (q_mem == nullptr) {
            return -1;
        }
        butil::BoundedQueue<bthread_t> q(q_mem, memsize, butil::OWNS_STORAGE);
        _tasks.swap(q);
        return 0;
    }

    bool pop(bthread_t* task) {
        if (_ntask.load(butil::memory_order_relaxed) == 0) {
            return false;
        }
        _mutex.lock();
        const bool result = _tasks.pop(task);
        if (result) {
            const size_t ntask = _ntask.load(butil::memory_order_relaxed);
            DCHECK_GT(ntask, 0u);
            _ntask.store(ntask - 1, butil::memory_order_relaxed);
        }
        _mutex.unlock();
        return result;
    }

    bool push(bthread_t task) {
        _mutex.lock();
        const bool res = push_locked(task);
        _mutex.unlock();
        return res;
    }

    bool push_locked(bthread_t task) {
        if (!_tasks.push(task)) {
            return false;
        }
        const size_t ntask = _ntask.load(butil::memory_order_relaxed);
        _ntask.store(ntask + 1, butil::memory_order_relaxed);
        return true;
    }

    size_t capacity() const { return _tasks.capacity(); }
    
private:
friend class TaskGroup;
    DISALLOW_COPY_AND_ASSIGN(RemoteTaskQueue);
    butil::BoundedQueue<bthread_t> _tasks;
    butil::Mutex _mutex;
    butil::atomic<size_t> _ntask;
};

}  // namespace bthread

#endif  // BTHREAD_REMOTE_TASK_QUEUE_H
