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
// SyncMap inspired from sync.map

#ifndef BUTIL_SYNC_MAP_H
#define BUTIL_SYNC_MAP_H

#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <unordered_map>

#include <butil/containers/hash_tables.h>        // butil::hash_map
#include <butil/synchronization/lock.h>          // butil::Lock
#include <butil/time/time.h>                     // butil::TimeDelta
#include <butil/memory/ref_counted.h>            // butil::RefCountedThreadSafe

#include "butil/containers/sync_list.h"
#include "butil/containers/sync_ptr.h"

namespace butil {

// Try use sync_map in these following scenarios:
//
// (1) When the entry for a given key is only ever written once but read
//     many times, as in caches that only grow. **MROW**
// (2) When multiple threads read, write, and overwrite entries for
//     disjoint sets of keys.
//
// NOTE: Objects stored in sync_map MUST be copyable.
//
template <class Key, class Value,
         typename Hasher=BUTIL_HASH_NAMESPACE::hash<Key>>
class sync_map {
private:
    struct ValueWrapper : public SyncObject<ValueWrapper> {
        Value value;
        ValueWrapper(const Value& v) : value(v) {}
        ~ValueWrapper() = default;
    };

    struct Entry : public butil::RefCountedThreadSafe<Entry> {
    public:
        static const void* const Deleted;

        std::atomic<ValueWrapper*> p;
        sync_map* m;

        // Stores a value if the entry has not been marked into |Deleted|.
        // If the entry was |Deleted|, returns false and leaves the entry
        // unchanged.
        bool try_store(const Value& value);

        void store(const Value& value);

        // Loads a value into |value| if the entry has not been marked
        // into |Deleted| and not null.
        // Returns true if the entry was not |Deleted|.
        bool load(Value* value);

        // Mark the entry into |Deleted|.
        void mark_delete();

        // Returns true if the entry was |Deleted|.
        bool deleted() { return p.load(std::memory_order_acquire) == Deleted; }

        Entry() : p(nullptr) { }
        explicit Entry(const Value& value, sync_map* map)
            : p(new ValueWrapper(value)), m(map) { }
    private:
        friend class butil::RefCountedThreadSafe<Entry>;
        ~Entry() {
            ValueWrapper* v = p.load(std::memory_order_acquire);
            if (!!v && v != Deleted) { v->Retire(); }
        }
    };

public:
    typedef std::function<bool (const Key&, const Value&)> RangeCallback;

    using NormalMap = std::unordered_map<Key, Entry*, Hasher>;
    //using NormalMap = butil::hash_map<Key, Entry*>;
    //using NormalMap = std::map<Key, Entry*>;

    using key_type = typename NormalMap::key_type; // Key
    using value_type = typename NormalMap::value_type; // Entry
    using iterator = typename NormalMap::iterator;
    using const_iterator = typename NormalMap::const_iterator;

    sync_map();
    sync_map(const sync_map&) = delete;
    sync_map(const sync_map&&);
    ~sync_map();

    sync_map& operator=(const sync_map&) = default;
    sync_map& operator=(sync_map&&) = default;

    template<class K, class T>
    void insert(const K& key, const T& value);
    bool seek(const key_type& key, Value* value);

    // Range calls callback sequentially for each key and value in the map.
    // If callback returns false, range stops the iteration.
    //
    // Range will read a snapshot of the sync_map, and if the value
    // for any key stored in read cache is inserted or deleted concurrently,
    // Range may reflect. Range will be O(N).
    void range(const RangeCallback& callback);

    void erase(const key_type& key);

    // Will scan the read cache or dirty cache.
    size_t size();

private:
    // Read cached missed and dirty cache hitted.
    void missed();

    // Sync data from read cache to dirty cache.
    // It is only associated with the dirty cache was empty.
    void sync_read_to_dirty();

    // Read only cache.
    struct ReadOnly : public butil::RefCountedThreadSafe<ReadOnly> {
    public:
        NormalMap* m; // internal storage, |ReadOnly| owns.
        bool amended; // true if the dirty map contains some key not in ReadOnly.
        ReadOnly(NormalMap* m, bool amended) : m(m), amended(amended) { }
    private:
        friend class butil::RefCountedThreadSafe<ReadOnly>;
        ~ReadOnly() {
            for (auto& i : *m) {
                if (i.second) {
                    i.second->Release();
                    i.second = nullptr;
                }
            }
            m->clear();
            delete m;
        }
    };

    mutable butil::Lock _lock;
    size_t _misses; // the number of seeking that missng read, hit dirty.
    std::atomic<ReadOnly*> _read; // read only cache.
    ReadOnly* _dirty; // dirty cache.
};

// |Deleted| needs a unique const void* to serve as the key, so create a const
// void* and use its own address as the unique pointer.
template <class Key, class Value, typename Hasher>
const void* const sync_map<Key, Value, Hasher>::Entry::Deleted =
            &sync_map<Key, Value, Hasher>::Entry::Deleted;

template <class Key, class Value, typename Hasher>
bool sync_map<Key, Value, Hasher>::Entry::try_store(const Value& value) {
    ValueWrapper* old = p.load(std::memory_order_acquire);
    if (!old || old == Deleted) { return false; }

    ValueWrapper* v = new ValueWrapper(value);
    while (!p.compare_exchange_strong(old, v, std::memory_order_seq_cst,
                                              std::memory_order_relaxed)) {
        if (!old || old == Deleted) {
            delete v;
            return false;
        }
    }
    if (!!old && old != Deleted) { old->Retire(); }
    return true;
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::Entry::store(const Value& value) {
    ValueWrapper* old = p.exchange(new ValueWrapper(value),
                                   std::memory_order_release);
    if (!!old && old != Deleted) { old->Retire(); }
}

template <class Key, class Value, typename Hasher>
bool sync_map<Key, Value, Hasher>::Entry::load(Value* value) {
    sync_ptr<ValueWrapper> v(p);
    if (v.get() == nullptr || v.get() == Deleted/*mark deleted*/) {
        return false;
    }
    *value = v->value;
    return true;
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::Entry::mark_delete() {
    ValueWrapper* v = p.exchange(
            static_cast<ValueWrapper*>(const_cast<void*>(Deleted)),
            std::memory_order_release);
    if (!!v && v != Deleted) { v->Retire(); }
}

template <class Key, class Value, typename Hasher>
sync_map<Key, Value, Hasher>::sync_map()
    : _misses(0), _dirty(new ReadOnly(new NormalMap, false)) {
    ReadOnly* read = new ReadOnly(new NormalMap, false);
    read->AddRef();
    _read.store(read, std::memory_order_relaxed);
    _dirty->AddRef();
}

template <class Key, class Value, typename Hasher>
sync_map<Key, Value, Hasher>::~sync_map() {
    butil::AutoLock lock(_lock);
    ReadOnly* read = _read.load(std::memory_order_relaxed);
    read->Release();
    _dirty->Release();
}

template <class Key, class Value, typename Hasher>
template<class K, class T>
void sync_map<Key, Value, Hasher>::insert(const K& key, const T& value) {
    ReadOnly* read = _read.load(std::memory_order_acquire);
    auto it = read->m->find(key);
    if (it != read->m->end() && !read->amended
            && it->second->try_store(value)) {
        return;
    }

    butil::AutoLock lock(_lock);
    read = _read.load(std::memory_order_relaxed);
    it = read->m->find(key);
    if (it != read->m->end()) { // Read cache has this key.
        it->second->store(value);
        return;
    }

    auto dit = _dirty->m->find(key);
    if (dit != _dirty->m->end()) { // Dirty cache has this key.
        dit->second->store(value);
    } else {
        // We're adding the first new key to the _dirty map.
        if (!read->amended) {
            // Copy the unpunged data from _read map to _dirty map.
            sync_read_to_dirty();
            // Make sure mark the read-only map as incomplete.
            read->amended = true;
        }
        // add a new key.
        Entry* entry = new Entry(value, this);
        entry->AddRef();
        _dirty->m->insert(std::make_pair(key, entry));
    }
}

template <class Key, class Value, typename Hasher>
bool sync_map<Key, Value, Hasher>::seek(const key_type& key, Value* value) {
    ReadOnly* read = _read.load(std::memory_order_acquire);
    auto it = read->m->find(key);
    if (it == read->m->end() && read->amended/*dirty*/) {
        // dirty contains some key.
        // Avoid reporting a spurious miss if _dirty got promoted while
        // we were blocked on _lock.
        // (If further loads of the same key will not miss, it's
        // not worth copying the dirty map for this key.)
        butil::AutoLock lock(_lock);
        read = _read.load(std::memory_order_relaxed);
        it = read->m->find(key);
        if (it == read->m->end() && read->amended/*dirty*/) {
            auto dit = _dirty->m->find(key);
            if (dit == _dirty->m->end()) { return false; }
            missed();
            return dit->second->load(value);
        }
    }
    if (it == read->m->end()) { return false; }
    return it->second->load(value);
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::erase(const key_type& key) {
    ReadOnly* read = _read.load(std::memory_order_acquire);
    auto it = read->m->find(key);
    if (it == read->m->end() && read->amended/*dirty*/) {
        butil::AutoLock lock(_lock);
        read = _read.load(std::memory_order_relaxed);
        it = read->m->find(key);
        if (it == read->m->end() && read->amended/*dirty*/) {
            auto dit = _dirty->m->find(key);
            if (dit != _dirty->m->end() && dit->second) {
                dit->second->Release();
                _dirty->m->erase(dit);
            }
        }
    }
    if (it != read->m->end()) {
        it->second->mark_delete();
    }
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::range(const RangeCallback& callback) {
    ReadOnly* read = _read.load(std::memory_order_acquire);

    if (read->amended) {
        butil::AutoLock lock(_lock);
        read = _dirty;
        read->amended = false;
        ReadOnly* old_read = _read.exchange(read, std::memory_order_release);
        _dirty = old_read;
        _misses = 0;
    }

    Value v;
    for (auto& i : *read->m) {
        if (!i.second->load(&v)) continue;
        if (!callback(i.first, v)) {
            break;
        }
    }
}

template <class Key, class Value, typename Hasher>
size_t sync_map<Key, Value, Hasher>::size() {
    size_t cnt = 0;
    auto l = [&cnt](const Key&, const Value&) -> bool {
        ++cnt;
        return true;
    };
    range(l);
    return cnt;
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::missed() {
    if (++_misses < _dirty->m->size()) { return; }
    ReadOnly* read = _dirty;
    read->amended = false;
    ReadOnly* old_read = _read.exchange(read, std::memory_order_release);
    _dirty = old_read;
    _misses = 0;
}

template <class Key, class Value, typename Hasher>
void sync_map<Key, Value, Hasher>::sync_read_to_dirty() {
    ReadOnly* read = _read.load(std::memory_order_relaxed);
    for (auto& it : *read->m) {
        if (!it.second->deleted()) {
            if (_dirty->m->find(it.first) == _dirty->m->end()) {
                it.second->AddRef();
                _dirty->m->insert(std::make_pair(it.first, it.second));
            }
        }
    }
}

} // namespace butil

#endif
