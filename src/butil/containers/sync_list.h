// Copyright (c) 2018 Baidu, Inc.
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

// Author: Li,Shuo (lishuo02@baidu.com)


#ifndef BUTIL_SYNC_LIST_H
#define BUTIL_SYNC_LIST_H

#include <atomic>

#include <butil/logging.h>                       // DCHECK

namespace butil {

class LinkNode {
public:
    LinkNode() : _previous(nullptr), _next(nullptr) {}
    virtual ~LinkNode() {}
    void set_next(LinkNode* node) {
        _next = node;
    }

    LinkNode* next() {
        return _next;
    }

    LinkNode* previous() {
        return _previous;
    }
private:
    LinkNode* _previous;
    LinkNode* _next;
};

class LinkedList {
public:
    LinkedList() : _head(nullptr), _tail(nullptr) {}

    explicit LinkedList(LinkNode* head, LinkNode* tail)
        : _head(head), _tail(tail) {}

    ~LinkedList() {}

    LinkNode* head() const {
        return _head;
    }

    LinkNode* tail() const {
        return _tail;
    }

    bool empty() const {
        return head() == nullptr;
    }

    void push(LinkNode* node) {
        node->set_next(nullptr);
        if (_tail) {
            _tail->set_next(node);
        } else {
            _head = node;
        }
        _tail = node;
    }

    void splice(LinkedList& l) {
        if (head() == nullptr) {
            _head = l.head();
        } else {
            _tail->set_next(l.head());
        }
        _tail = l.tail();
        l.clear();
    }

    void clear() {
        _head = nullptr;
        _tail = nullptr;
    }

private:
    LinkNode* _head;
    LinkNode* _tail;
};

class sync_list {
public:
    sync_list() : _head(nullptr), _tail(nullptr) {}

    sync_list(sync_list&& other) {
        _head.store(other.head(), std::memory_order_relaxed);
        _tail.store(other.tail(), std::memory_order_relaxed);
        other._head.store(nullptr, std::memory_order_relaxed);
        other._tail.store(nullptr, std::memory_order_relaxed);
    }

    sync_list& operator=(sync_list&& other) {
        _head.store(other.head(), std::memory_order_relaxed);
        _tail.store(other.tail(), std::memory_order_relaxed);
        other._head.store(nullptr, std::memory_order_relaxed);
        other._tail.store(nullptr, std::memory_order_relaxed);
        return *this;
    }

    ~sync_list() {
        DCHECK(head() == nullptr);
        DCHECK(tail() == nullptr);
    }

    void push(LinkNode* node) {
        bool done = false;
        while (!done) {
            if (tail()) {
                done = push_in_non_empty_list(node);
            } else {
                done = push_in_empty_list(node);
            }
        }
    }

    LinkedList pop_all() noexcept {
        auto h = exchange_head();
        auto t = (h != nullptr) ? exchange_tail() : nullptr;
        return LinkedList(h, t);
    }

    bool empty() const noexcept {
        return head() == nullptr;
    }

    LinkNode* head() const noexcept {
        return _head.load(std::memory_order_acquire);
    }

    LinkNode* tail() const noexcept {
        return _tail.load(std::memory_order_acquire);
    }

    void set_head(LinkNode* node) noexcept {
        _head.store(node, std::memory_order_release);
    }

    bool cas_head(LinkNode* expected, LinkNode* node) noexcept {
        return _head.compare_exchange_weak(
                expected, node, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    bool cas_tail(LinkNode* expected, LinkNode* node) noexcept {
        return _tail.compare_exchange_weak(
                expected, node, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    LinkNode* exchange_head() noexcept {
        return _head.exchange(nullptr, std::memory_order_acq_rel);
    }

    LinkNode* exchange_tail() noexcept {
        return _tail.exchange(nullptr, std::memory_order_acq_rel);
    }

private:
    bool push_in_non_empty_list(LinkNode* node) noexcept {
        auto h = head();
        if (h) {
            node->set_next(h); // LinkNode must support set_next
            if (cas_head(h, node)) {
                return true;
            }
        }
        return false;
    }

    bool push_in_empty_list(LinkNode* node) noexcept {
        LinkNode* t = nullptr;
        node->set_next(nullptr); // LinkNode must support set_next
        if (cas_tail(t, node)) {
            set_head(node);
            return true;
        }
        return false;
    }

    std::atomic<LinkNode*> _head;
    std::atomic<LinkNode*> _tail;
}; // sync_list

} // namespace butil

#endif
