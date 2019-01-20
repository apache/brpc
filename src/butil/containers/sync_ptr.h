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
//
// sync_ptr inspired from Hazard Pointer.
//
// example:
// class MyObject : public SyncObject<MyObject> {
// public:
//   ...
//   int Bar() { return _bar; }
// private:
//   int _bar;
// };
// std::atomic<MyObject*> s_obj;
//
// Thread 1:
// int get_bar() {
//   sync_ptr<MyObject> ptr(s_obj);
//   return ptr->Bar();
// }
//
// Thread 2:
// void update(MyObject* new_obj) {
//   MyObject* ptr = s_obj.exchange(new_obj);
//   ptr->Retire();
// }

#ifndef BUTIL_SYNC_POINTER_H
#define BUTIL_SYNC_POINTER_H

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <type_traits>

#include <butil/memory/singleton.h>               // DefaultSingletonTraits

#include "butil/containers/sync_list.h"           // sync_list

namespace butil {

class SyncPtrCtrl;

// tls
class SyncPtrHolder {
public:
    explicit SyncPtrHolder(SyncPtrCtrl* ctrl)
        : _ptr(nullptr), _ctrl(ctrl) {}
    ~SyncPtrHolder();

    const void* ptr() const;
    void reset(const void* p = nullptr);

private:
    friend class SyncPtrCtrl;
    std::atomic<const void*> _ptr;
    SyncPtrCtrl* _ctrl;
};

// tls
class SyncPtrRetireList {
public:
    explicit SyncPtrRetireList(SyncPtrCtrl* ctrl)
        : _ctrl(ctrl), _count(0) {}
    ~SyncPtrRetireList();

    void push_back(LinkNode* ptr);
private:
    friend class SyncPtrCtrl;
    void push_all_to_ctrl();

    static constexpr uint32_t kBatchNumber = 20;

    SyncPtrCtrl* _ctrl;
    uint32_t _count;
    sync_list _retire_list;
};

class SyncPtrCtrl {
public:
    static SyncPtrCtrl* GetInstance();

    void Retire(LinkNode* ptr);
    void CleanUp();

    ~SyncPtrCtrl();
private:
    friend class SyncPtrHolder;
    friend class SyncPtrRetireList;
    template <typename, typename>
    friend class sync_ptr;

    SyncPtrCtrl();
    friend struct DefaultSingletonTraits<SyncPtrCtrl>;

    static constexpr int64_t kMaxClaimSeconds = 5/*seconds*/;
    static constexpr int64_t kMaxClaimNumber = 1000;

    SyncPtrHolder* AcquireHolder();
    SyncPtrRetireList* AcquireRetireList();
    SyncPtrHolder* GetAndSetHolder(SyncPtrHolder*& holder);
    SyncPtrRetireList* GetAndSetRetireList(SyncPtrRetireList*& rl);

    void ReleaseHolder(SyncPtrHolder* holder);
    void ReleaseRetireList(SyncPtrRetireList* rl);

    void PushRetireList(LinkedList* retire_list,
                        uint32_t count,
                        bool check);
    void CheckToReclaim(bool force);

    bool TryTimedCleanup();
    bool TryThreshold();
    void TryReclaim();
    bool Reclaim(const LinkedList& retires,
                 const std::unordered_set<const void*>& hashset);

    std::mutex _holder_lock;
    std::vector<SyncPtrHolder*> _holders;

    std::mutex _rl_lock;
    std::vector<SyncPtrRetireList*> _retire_lists;

    sync_list _batcher;

    std::atomic<uint32_t> _retired_count;
    std::atomic<uint32_t> _reclaimer_count;

    std::atomic<int64_t> _sync_time;

    DISALLOW_COPY_AND_ASSIGN(SyncPtrCtrl);
};

template <typename T>
struct DefaultDeleter {
    constexpr DefaultDeleter() = default;

    template<typename U, typename = typename
        std::enable_if<std::is_convertible<U*, T*>::value>::type>
    DefaultDeleter(const DefaultDeleter<U>&) {}

    void operator()(T* ptr) const {
        static_assert(sizeof(T)>0, "can't delete pointer to incomplete type");
        delete ptr;
    }
};

class SyncPtrNode : public LinkNode {
public:
    using ReclaimFunction = void (*)(SyncPtrNode*);
    virtual ~SyncPtrNode() = default;
protected:
    friend class SyncPtrCtrl;
    SyncPtrNode() : _reclaimer(nullptr) {}
    ReclaimFunction _reclaimer;
};

template <typename T, typename D = DefaultDeleter<T>>
class SyncObject : public SyncPtrNode {
public:
    void Retire() {
        SyncPtrCtrl::GetInstance()->Retire(this);
    }

    virtual ~SyncObject() = default;
protected:
    friend class SyncPtrCtrl;

    SyncObject() {
        _reclaimer = [](SyncPtrNode* p) {
            auto u = static_cast<SyncObject<T, D>*>(p);
            auto t = static_cast<T*>(u);
            t->_deleter(t);
        };
    }
    D _deleter;
};

template <typename T, typename D = DefaultDeleter<T>>
class sync_ptr {
public:
    typedef D delete_type;
    sync_ptr(const std::atomic<T*>& src)
        : _holder(nullptr), _ptr(nullptr), _deleter(D()) {
        _holder = SyncPtrCtrl::GetInstance()->AcquireHolder();
        DCHECK(_holder);
        get_protected(src);
    }
    ~sync_ptr() { reset(); }

    T* get() const { return _ptr; }
    T* operator->() const { return get(); }

    void reset(const T* ptr = nullptr) {
        _holder->reset((void*)ptr);
    }

    sync_ptr(const sync_ptr&) = delete;
    sync_ptr& operator=(const sync_ptr&) = delete;

private:
    T* get_protected(const std::atomic<T*>& src) {
        _ptr = src.load(std::memory_order_relaxed);
        while (!try_protect(src)) {
            /* Keep trying */;
        }
        return _ptr;
    }

    bool try_protect(const std::atomic<T*>& src) {
        auto p = _ptr;
        reset(p);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        _ptr = src.load(std::memory_order_acquire);
        if (p != _ptr) {
            reset();
            return false;
        }
        return true;
    }

    const delete_type& get_deleter() const { return _deleter; }
    delete_type& get_deleter() { return _deleter; }

    SyncPtrHolder* _holder;
    T* _ptr;
    delete_type _deleter;
};

} // namespace butil

#endif
