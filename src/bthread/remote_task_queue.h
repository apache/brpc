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

#include "bthread/bthread.h"
#include "butil/containers/bounded_queue.h"
#include "butil/macros.h"

#include "bthread/moodycamelqueue.h"

namespace bthread {

class TaskGroup;

class RemoteQueue {
public:
    RemoteQueue() {}

    int init(size_t cap) {
        _tasks = moodycamel::ConcurrentQueue<bthread_t>(cap);
        return 0;
    }

    bool pop(bthread_t *task) {
        if (_task_cnt.load(std::memory_order_acquire) > 0 && _tasks.try_dequeue(*task)) {
            _task_cnt.fetch_sub(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool push(bthread_t task) {
        if (_tasks.enqueue(task)) {
            _task_cnt.fetch_add(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    size_t capacity() const {
        return _task_cnt.load(std::memory_order_acquire);
    }

    friend class TaskGroup;
    moodycamel::ConcurrentQueue<bthread_t> _tasks;
    std::atomic<int> _task_cnt{0};
};

// A queue for storing bthreads created by non-workers. Since non-workers
// randomly choose a TaskGroup to push which distributes the contentions,
// this queue is simply implemented as a queue protected with a lock.
// The function names should be self-explanatory.
class RemoteTaskQueue {
public:
    RemoteTaskQueue() {}

    int init(size_t cap) {
        const size_t memsize = sizeof(bthread_t) * cap;
        void* q_mem = malloc(memsize);
        if (q_mem == NULL) {
            return -1;
        }
        butil::BoundedQueue<bthread_t> q(q_mem, memsize, butil::OWNS_STORAGE);
        _tasks.swap(q);
        return 0;
    }

    bool pop(bthread_t* task) {
        if (_tasks.empty()) {
            return false;
        }
        _mutex.lock();
        const bool result = _tasks.pop(task);
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
        return _tasks.push(task);
    }

    size_t capacity() const { return _tasks.capacity(); }

private:
friend class TaskGroup;
    DISALLOW_COPY_AND_ASSIGN(RemoteTaskQueue);
    butil::BoundedQueue<bthread_t> _tasks;
    butil::Mutex _mutex;
};

}  // namespace bthread

#endif  // BTHREAD_REMOTE_TASK_QUEUE_H
