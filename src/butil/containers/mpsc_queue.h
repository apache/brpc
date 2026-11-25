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

#if defined(UNIT_TEST)
// Pause after publishing the head but before connecting the new node.
inline atomic<void (*)(const void*)>& MPSCQueueEnqueueHookForTest() {
    static atomic<void (*)(const void*)> hook{nullptr};
    return hook;
}
#endif

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

    // Construct the element in place before publishing the node.
    template <typename... Args>
    void Emplace(Args&&... args);

    // Dequeue data from the queue.
    bool Dequeue(T& data);

    // Never wait for a producer to finish linking a published node. False
    // means empty or temporarily unavailable, and leaves `data` unchanged.
    // A caller that sleeps after false needs a notification after publication
    // completes. Only the single consumer may call either dequeue method.
    bool TryDequeue(T& data);

private:
    // With `wait` disabled, check the entire segment before changing links.
    bool ReverseList(MPSCQueueNode<T>* old_head, bool wait);

    void EnqueueImpl(MPSCQueueNode<T>* node);
    bool DequeueImpl(T* data, bool wait);

    Alloc _alloc;
    atomic<MPSCQueueNode<T>*> _head;
    atomic<MPSCQueueNode<T>*> _cur_enqueue_node;
    MPSCQueueNode<T>* _cur_dequeue_node;
};

template <typename T, typename Alloc>
MPSCQueue<T, Alloc>::~MPSCQueue() {
    while (DequeueImpl(nullptr, true));
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::Enqueue(typename add_const_reference<T>::type data) {
    Emplace(data);
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::Enqueue(T&& data) {
    Emplace(std::move(data));
}

template <typename T, typename Alloc>
template <typename... Args>
void MPSCQueue<T, Alloc>::Emplace(Args&&... args) {
    auto* node = new (_alloc.Alloc()) MPSCQueueNode<T>;
    node->next.store(MPSCQueueNode<T>::UNCONNECTED, memory_order_relaxed);
    node->data_mem.Init(std::forward<Args>(args)...);
    EnqueueImpl(node);
}

template <typename T, typename Alloc>
void MPSCQueue<T, Alloc>::EnqueueImpl(MPSCQueueNode<T>* node) {
    MPSCQueueNode<T>* prev = _head.exchange(node, memory_order_release);
#if defined(UNIT_TEST)
    auto hook = MPSCQueueEnqueueHookForTest().load(memory_order_acquire);
    if (hook != nullptr) {
        hook(this);
    }
#endif
    if (prev) {
        node->next.store(prev, memory_order_release);
        return;
    }
    node->next.store(nullptr, memory_order_relaxed);
    _cur_enqueue_node.store(node, memory_order_release);
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::Dequeue(T& data) {
    return DequeueImpl(&data, true);
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::TryDequeue(T& data) {
    return DequeueImpl(&data, false);
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::DequeueImpl(T* data, bool wait) {
    MPSCQueueNode<T>* node;
    if (_cur_dequeue_node) {
        node = _cur_dequeue_node;
    } else {
        node = _cur_enqueue_node.exchange(nullptr, memory_order_acquire);
    }
    if (!node) {
        return false;
    }

    if (node->next.load(memory_order_relaxed) == nullptr &&
        !ReverseList(node, wait)) {
        // The entry may already have been removed from `_cur_enqueue_node`.
        // Retain it for the next attempt without moving its value or changing
        // any links. Producers still append to the same published head.
        _cur_dequeue_node = node;
        return false;
    }
    if (data != nullptr) {
        *data = std::move(*node->data_mem.get());
    }
    _cur_dequeue_node = node->next.load(memory_order_relaxed);
    node->data_mem.Destroy();
    _alloc.Free(node);

    return true;
}

template <typename T, typename Alloc>
bool MPSCQueue<T, Alloc>::ReverseList(MPSCQueueNode<T>* old_head, bool wait) {
    // Detach the last node if no producer has extended `_head`.
    MPSCQueueNode<T>* new_head = old_head;
    MPSCQueueNode<T>* desired = nullptr;
    if (_head.compare_exchange_strong(
        new_head, desired, memory_order_acquire)) {
        // No one added new requests.
        return true;
    }
    CHECK_NE(new_head, old_head);
    // Acquire the new head's initialization, including its UNCONNECTED link.

    if (!wait) {
        // Inspect exactly the snapshot returned by the failed CAS. Producers
        // may extend `_head`, but never change a connected link in this
        // segment. Do not reload `_head` between this check and reversal.
        auto* p = new_head;
        while (p != old_head) {
            auto* next = p->next.load(memory_order_acquire);
            if (next == MPSCQueueNode<T>::UNCONNECTED) {
                return false;
            }
            CHECK(next != nullptr);
            p = next;
        }
    }

    // Someone added new requests.
    // Reverse the list until old_head.
    MPSCQueueNode<T>* tail = nullptr;
    MPSCQueueNode<T>* p = new_head;
    do {
        MPSCQueueNode<T>* saved_next;
        // The producer connects the link after publishing the node in _head.
        // `TryDequeue` checked this immutable segment above and never enters
        // this wait. `Dequeue` retains its original single-pass reversal.
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
    return true;
}

}

#endif // BUTIL_MPSC_QUEUE_H
