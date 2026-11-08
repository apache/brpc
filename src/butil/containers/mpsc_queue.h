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

// A Multiple Producer, Single Consumer Queue.
// It allows multiple threads to enqueue, and allows one thread
// (and only one thread) to dequeue.

#ifndef BUTIL_MPSC_QUEUE_H
#define BUTIL_MPSC_QUEUE_H

#include <new>
#include "butil/object_pool.h"
#include "butil/type_traits.h"
#include "butil/memory/manual_constructor.h"

namespace butil {

template <typename T>
struct BAIDU_CACHELINE_ALIGNMENT MPSCQueueNode {
    static MPSCQueueNode* const UNCONNECTED;

    atomic<MPSCQueueNode*> next{nullptr};
    ManualConstructor<T> data_mem;
};

template <typename T>
MPSCQueueNode<T>* const MPSCQueueNode<T>::UNCONNECTED = (MPSCQueueNode<T>*)(intptr_t)-1;

// Default allocator for MPSCQueueNode.
template <typename T>
class DefaultAllocator {
public:
    void* Alloc() { return malloc(sizeof(MPSCQueueNode<T>)); }
    void Free(void* p) { free(p); }
};

// Allocator using ObjectPool for MPSCQueueNode.
template <typename T>
class ObjectPoolAllocator {
public:
    void* Alloc() { return get_object<MPSCQueueNode<T>>(); }
    void Free(void* p) { return_object(static_cast<MPSCQueueNode<T>*>(p)); }
};


template <typename T, typename Alloc = DefaultAllocator<T>>
class MPSCQueue {
public:
    MPSCQueue()
        : _head(nullptr)
        , _cur_enqueue_node(nullptr)
        , _cur_dequeue_node(nullptr) {}

    ~MPSCQueue();

    // Enqueue data to the queue.
    void Enqueue(typename add_const_reference<T>::type data);
    void Enqueue(T&& data);

    // Dequeue data from the queue.
    bool Dequeue(T& data);

private:
    template <typename U>
    void EnqueueInternal(U&& data);

    // Reverse the list until old_head.
    void ReverseList(MPSCQueueNode<T>* old_head);

    void EnqueueImpl(MPSCQueueNode<T>* node);
    bool DequeueImpl(T* data);

    Alloc _alloc;
    atomic<MPSCQueueNode<T>*> _head;
    atomic<MPSCQueueNode<T>*> _cur_enqueue_node;
    MPSCQueueNode<T>* _cur_dequeue_node;
};

template <typename T, typename Alloc>
MPSCQueue<T, Alloc>::~MPSCQueue() {
    while (DequeueImpl(nullptr));
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::Enqueue(typename add_const_reference<T>::type data) {
    EnqueueInternal(data);
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::Enqueue(T&& data) {
    EnqueueInternal(std::move(data));
}

template <typename T, typename Alloc>
template <typename U>
void MPSCQueue<T, Alloc>::EnqueueInternal(U&& data) {
    auto node = new (_alloc.Alloc()) MPSCQueueNode<T>;
    node->next.store(MPSCQueueNode<T>::UNCONNECTED, memory_order_relaxed);
    node->data_mem.Init(std::forward<U>(data));
    EnqueueImpl(node);
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::EnqueueImpl(MPSCQueueNode<T>* node) {
    MPSCQueueNode<T>* prev = _head.exchange(node, memory_order_release);
    if (prev) {
        node->next.store(prev, memory_order_release);
        return;
    }
    node->next.store(nullptr, memory_order_relaxed);
    _cur_enqueue_node.store(node, memory_order_release);
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::Dequeue(T& data) {
    return DequeueImpl(&data);
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::DequeueImpl(T* data) {
    MPSCQueueNode<T>* node;
    if (_cur_dequeue_node) {
        node = _cur_dequeue_node;
    } else {
        node = _cur_enqueue_node.exchange(nullptr, memory_order_acquire);
    }
    if (!node) {
        return false;
    }

    if (data) {
        auto mem = (T* const)node->data_mem.get();
        *data = std::move(*mem);
    }
    MPSCQueueNode<T>* old_node = node;
    if (!node->next.load(memory_order_relaxed)) {
        ReverseList(node);
    }
    _cur_dequeue_node = node->next.load(memory_order_relaxed);
    node->data_mem.Destroy();
    _alloc.Free(old_node);

    return true;
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::ReverseList(MPSCQueueNode<T>* old_head) {
    // Try to set _write_head to nullptr to mark that it is done.
    MPSCQueueNode<T>* new_head = old_head;
    MPSCQueueNode<T>* desired = nullptr;
    if (_head.compare_exchange_strong(
        new_head, desired, memory_order_acquire)) {
        // No one added new requests.
        return;
    }
    CHECK_NE(new_head, old_head);
    // Acquire the new head's initialization, including its UNCONNECTED link.

    // Someone added new requests.
    // Reverse the list until old_head.
    MPSCQueueNode<T>* tail = nullptr;
    MPSCQueueNode<T>* p = new_head;
    do {
        MPSCQueueNode<T>* saved_next;
        // The producer connects the link after publishing the node in _head.
        while ((saved_next = p->next.load(memory_order_acquire)) ==
               MPSCQueueNode<T>::UNCONNECTED) {
            sched_yield();
        }
        p->next.store(tail, memory_order_relaxed);
        tail = p;
        p = saved_next;
        CHECK(p);
    } while (p != old_head);

    // Link old list with new list.
    old_head->next.store(tail, memory_order_relaxed);
}

}

#endif // BUTIL_MPSC_QUEUE_H
