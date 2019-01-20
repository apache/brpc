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

#include "butil/containers/sync_ptr.h"

#include <sys/types.h>
#include <linux/unistd.h>

#include <butil/time/time.h>                     // butil::TimeTicks

namespace butil {

SyncPtrHolder::~SyncPtrHolder() {
    if (_ctrl) {
        _ctrl->ReleaseHolder(this);
    }
}

const void* SyncPtrHolder::ptr() const {
    return _ptr.load(std::memory_order_acquire);
}

void SyncPtrHolder::reset(const void* p) {
    _ptr.store(p, std::memory_order_release);
}

SyncPtrRetireList::~SyncPtrRetireList() {
    if (_ctrl) {
        push_all_to_ctrl();
        _ctrl->ReleaseRetireList(this);
    }
}

void SyncPtrRetireList::push_back(LinkNode* ptr) {
    _retire_list.push(ptr);
    if (++_count >= kBatchNumber) { push_all_to_ctrl(); }
}

void SyncPtrRetireList::push_all_to_ctrl() {
    LinkedList retired = _retire_list.pop_all();
    if (!retired.empty() && _ctrl) {
        _ctrl->PushRetireList(&retired, _count, true);
        _count = 0;
    }
}

// static
SyncPtrCtrl* SyncPtrCtrl::GetInstance() {
    return Singleton<SyncPtrCtrl>::get();
}

SyncPtrCtrl::SyncPtrCtrl()
    : _retired_count(0), _reclaimer_count(0),
      _sync_time(butil::TimeTicks::Now().ToInternalValue()) {
    _holders.reserve(64); /**/
    _retire_lists.reserve(64); /**/
    int64_t next = (butil::TimeTicks::FromInternalValue(_sync_time) +
        butil::TimeDelta::FromSeconds(kMaxClaimSeconds)).ToInternalValue();
    _sync_time.store(next, std::memory_order_relaxed);
}
SyncPtrCtrl::~SyncPtrCtrl() {
    CleanUp();
    {
        std::unique_lock<std::mutex> lock(_holder_lock);
        for (auto& h : _holders) {
            h->_ctrl = nullptr; // atomic?
        }
    }
    {
        std::unique_lock<std::mutex> lock(_rl_lock);
        for (auto& rl : _retire_lists) {
            rl->_ctrl = nullptr; // atomic?
        }
    }
}

SyncPtrHolder* SyncPtrCtrl::AcquireHolder() {
    static thread_local SyncPtrHolder* holder = nullptr;
    return !!holder ? holder : GetAndSetHolder(holder);
}

SyncPtrRetireList* SyncPtrCtrl::AcquireRetireList() {
    static thread_local SyncPtrRetireList* retire_list = nullptr;
    return !!retire_list ? retire_list : GetAndSetRetireList(retire_list);
}

void SyncPtrCtrl::CleanUp() {
    {
        std::unique_lock<std::mutex> lock(_rl_lock);
        for (auto& rl : _retire_lists) {
            rl->push_all_to_ctrl();
        }
    }
    TryReclaim();
    while (_reclaimer_count.load(std::memory_order_acquire) > 0) {}
}

SyncPtrHolder* SyncPtrCtrl::GetAndSetHolder(SyncPtrHolder*& holder) {
    static thread_local SyncPtrHolder h(this);
    holder = &h;
    std::unique_lock<std::mutex> lock(_holder_lock);
    _holders.push_back(holder);
    return holder;
}

SyncPtrRetireList* SyncPtrCtrl::GetAndSetRetireList(SyncPtrRetireList*& rl) {
    static thread_local SyncPtrRetireList r(this);
    rl = &r;
    std::unique_lock<std::mutex> lock(_rl_lock);
    _retire_lists.push_back(rl);
    return rl;
}

void SyncPtrCtrl::ReleaseHolder(SyncPtrHolder* holder) {
    std::unique_lock<std::mutex> lock(_holder_lock);
    _holders.erase(std::remove_if(_holders.begin(), _holders.end(),
        [holder] (const SyncPtrHolder* h) {
        if (h == holder) { return true; }
        return false;
    }), _holders.end());
}

void SyncPtrCtrl::ReleaseRetireList(SyncPtrRetireList* rl) {
    std::unique_lock<std::mutex> lock(_rl_lock);
    _retire_lists.erase(std::remove_if(_retire_lists.begin(), _retire_lists.end(),
        [rl] (const SyncPtrRetireList* r) {
        if (r == rl) { return true; }
        return false;
    }), _retire_lists.end());
}

void SyncPtrCtrl::Retire(LinkNode* ptr) {
    SyncPtrRetireList* rl = AcquireRetireList();
    rl->push_back(ptr);
}

void SyncPtrCtrl::PushRetireList(LinkedList* retire_list,
                                 uint32_t count,
                                 bool check) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    while (true) {
        LinkNode* r = _batcher.head();
        retire_list->tail()->set_next(r);
        if (_batcher.cas_head(r, retire_list->head())) {
            break;
        }
    }
    _retired_count.fetch_add(count, std::memory_order_release);
    if (check) { CheckToReclaim(false); }
}

void SyncPtrCtrl::CheckToReclaim(bool force) {
    if (TryTimedCleanup()) { return; }
    TryThreshold();
}

bool SyncPtrCtrl::TryTimedCleanup() {
    int64_t now = butil::TimeTicks::Now().ToInternalValue();
    int64_t sync_time = _sync_time.load(std::memory_order_relaxed);
    int64_t next = (butil::TimeTicks::FromInternalValue(sync_time) +
        butil::TimeDelta::FromSeconds(kMaxClaimSeconds)).ToInternalValue();
    if (now > sync_time && _sync_time.compare_exchange_strong(
                sync_time, next, std::memory_order_relaxed)) {
        TryReclaim();
        return true;
    }
    return false;
}

bool SyncPtrCtrl::TryThreshold() {
    auto count = _retired_count.load(std::memory_order_acquire);
    if (count >= kMaxClaimNumber) {
        TryReclaim();
        return true;
    }
    return false;
}

void SyncPtrCtrl::TryReclaim() {
    _reclaimer_count.fetch_add(1, std::memory_order_acquire);
    while (true) {
        LinkedList retires = _batcher.pop_all();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::unordered_set<const void*> hashset;
        {
            std::unique_lock<std::mutex> lock(_holder_lock);
            for (const auto& holder : _holders) {
                hashset.insert(holder->ptr());
            }
        }
        if (Reclaim(retires, hashset)) {
            break;
        }
    }
    _reclaimer_count.fetch_sub(1, std::memory_order_release);
}

bool SyncPtrCtrl::Reclaim(const LinkedList& retires,
                          const std::unordered_set<const void*>& hashset) {
    auto p = retires.head();
    LinkedList under_using;
    size_t under_using_count = 0;

    while (p) {
        auto next = p->next();
        const void* ptr = static_cast<void*>(p);
        if (hashset.count(ptr) == 0) {
            SyncPtrNode* node = static_cast<SyncPtrNode*>(p);
            node->_reclaimer(node);
        } else {
            under_using.push(p);
            ++under_using_count;
        }
        p = next;
    }
    bool done = _batcher.head() == nullptr;
    if (under_using_count > 0) {
        PushRetireList(&under_using, under_using_count, false);
    }
    return done;
}

} // namespace butil
