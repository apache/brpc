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

// Date: Wed Nov 27 12:59:20 CST 2013

#ifndef BUTIL_FLAT_MAP_INL_H
#define BUTIL_FLAT_MAP_INL_H

namespace butil {

inline uint32_t find_next_prime(uint32_t nbucket) {
    static const unsigned long prime_list[] = {
        29ul, 
        53ul,         97ul,         193ul,       389ul,       769ul,
        1543ul,       3079ul,       6151ul,      12289ul,     24593ul,
        49157ul,      98317ul,      196613ul,    393241ul,    786433ul,
        1572869ul,    3145739ul,    6291469ul,   12582917ul,  25165843ul,
        50331653ul,   100663319ul,  201326611ul, 402653189ul, 805306457ul,
        1610612741ul, 3221225473ul, 4294967291ul
    };
    const size_t nprimes = sizeof(prime_list) / sizeof(prime_list[0]);
    for (size_t i = 0; i < nprimes; i++) {
        if (nbucket <= prime_list[i]) {
            return prime_list[i];
        }
    }
    return nbucket;
}

// NOTE: find_power2(0) = 0
inline uint64_t find_power2(uint64_t b) {
    b -= 1;
    b |= (b >> 1);
    b |= (b >> 2);
    b |= (b >> 4);
    b |= (b >> 8);
    b |= (b >> 16);
    b |= (b >> 32);
    return b + 1;
}

// Using next prime is slower for 10ns on average (due to %). If quality of
// the hash code is good enough, primeness of nbucket is not important. We
// choose to trust the hash code (or user should use a better hash algorithm
// when the collisions are significant) and still stick to round-to-power-2
// solution right now.
inline size_t flatmap_round(size_t nbucket) {
#ifdef FLAT_MAP_ROUND_BUCKET_BY_USE_NEXT_PRIME    
    return find_next_prime(nbucket);
#else
    // the lowerbound fixes the corner case of nbucket=0 which results in coredump during seeking the map.
    return nbucket <= 8 ? 8 : find_power2(nbucket);
#endif
}

inline size_t flatmap_mod(size_t hash_code, size_t nbucket) {
#ifdef FLAT_MAP_ROUND_BUCKET_BY_USE_NEXT_PRIME
    return hash_code % nbucket;
#else
    return hash_code & (nbucket - 1);
#endif
}

// Iterate FlatMap
template <typename Map, typename Value> class FlatMapIterator {
public:
    typedef Value value_type;
    typedef Value& reference;
    typedef Value* pointer;
    typedef typename add_const<Value>::type ConstValue;
    typedef ConstValue& const_reference;
    typedef ConstValue* const_pointer;
    typedef std::forward_iterator_tag iterator_category;
    typedef ptrdiff_t difference_type;
    typedef typename remove_const<Value>::type NonConstValue;
    
    FlatMapIterator() : _node(NULL), _entry(NULL) {}    
    FlatMapIterator(const Map* map, size_t pos) {
        _entry = map->_buckets + pos;
        find_and_set_valid_node();
    }
    FlatMapIterator(const FlatMapIterator<Map, NonConstValue>& rhs)
        : _node(rhs._node), _entry(rhs._entry) {}
    ~FlatMapIterator() = default;  // required by style-checker
    
    // *this == rhs
    bool operator==(const FlatMapIterator& rhs) const
    { return _node == rhs._node; }

    // *this != rhs
    bool operator!=(const FlatMapIterator& rhs) const
    { return _node != rhs._node; }
        
    // ++ it
    FlatMapIterator& operator++() {
        if (NULL == _node->next) {
            ++_entry;
            find_and_set_valid_node();
        } else {
            _node = _node->next;
        }
        return *this;
    }

    // it ++
    FlatMapIterator operator++(int) {
        FlatMapIterator tmp = *this;
        this->operator++();
        return tmp;
    }

    reference operator*() { return _node->element().value_ref(); }
    pointer operator->() { return &_node->element().value_ref(); }
    const_reference operator*() const { return _node->element().value_ref(); }
    const_pointer operator->() const { return &_node->element().value_ref(); }

private:
friend class FlatMapIterator<Map, ConstValue>;
friend class FlatMap<typename Map::key_type, typename Map::mapped_type,
                     typename Map::hasher, typename Map::key_equal, 
                     false, typename Map::allocator_type>;

    void find_and_set_valid_node() {
        for (; !_entry->is_valid(); ++_entry);
        _node = _entry;
    }
  
    typename Map::Bucket* _node;
    typename Map::Bucket* _entry;
};

// Iterate SparseFlatMap
template <typename Map, typename Value> class SparseFlatMapIterator {
public:
    typedef Value value_type;
    typedef Value& reference;
    typedef Value* pointer;
    typedef typename add_const<Value>::type ConstValue;
    typedef ConstValue& const_reference;
    typedef ConstValue* const_pointer;
    typedef std::forward_iterator_tag iterator_category;
    typedef ptrdiff_t difference_type;
    typedef typename remove_const<Value>::type NonConstValue;
    
    SparseFlatMapIterator() : _node(NULL), _pos(0), _map(NULL) {}
    SparseFlatMapIterator(const Map* map, size_t pos) {
        _map = map;
        _pos = pos;
        find_and_set_valid_node();
    }
    SparseFlatMapIterator(const SparseFlatMapIterator<Map, NonConstValue>& rhs)
        : _node(rhs._node), _pos(rhs._pos), _map(rhs._map)
    {}
    ~SparseFlatMapIterator() = default;  // required by style-checker
    
    // *this == rhs
    bool operator==(const SparseFlatMapIterator& rhs) const
    { return _node == rhs._node; }

    // *this != rhs
    bool operator!=(const SparseFlatMapIterator& rhs) const
    { return _node != rhs._node; }
        
    // ++ it
    SparseFlatMapIterator& operator++() {
        if (NULL == _node->next) {
            ++_pos;
            find_and_set_valid_node();
        } else {
            _node = _node->next;
        }
        return *this;
    }

    // it ++
    SparseFlatMapIterator operator++(int) {
        SparseFlatMapIterator tmp = *this;
        this->operator++();
        return tmp;
    }

    reference operator*() { return _node->element().value_ref(); }
    pointer operator->() { return &_node->element().value_ref(); }
    const_reference operator*() const { return _node->element().value_ref(); }
    const_pointer operator->() const { return &_node->element().value_ref(); }

private:
friend class SparseFlatMapIterator<Map, ConstValue>;
    
    void find_and_set_valid_node() {
        if (!_map->_buckets[_pos].is_valid()) {
            _pos = bit_array_first1(_map->_thumbnail, _pos + 1, _map->_nbucket);
        }
        _node = _map->_buckets + _pos;
    }

    typename Map::Bucket* _node;
    size_t _pos;
    const Map* _map;
};

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
FlatMap<_K, _T, _H, _E, _S, _A, _M>::FlatMap(const hasher& hashfn,
                                             const key_equal& eql,
                                             const allocator_type& alloc)
    : _size(0)
    , _nbucket(DEFAULT_NBUCKET)
    , _buckets((Bucket*)(&_default_buckets))
    , _thumbnail(_S ? _default_thumbnail : NULL)
    , _load_factor(80)
    , _is_default_load_factor(true)
    , _hashfn(hashfn)
    , _eql(eql)
    , _pool(alloc) {
    init_buckets_and_thumbnail(_buckets, _thumbnail, _nbucket);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
FlatMap<_K, _T, _H, _E, _S, _A, _M>::FlatMap(const FlatMap& rhs)
    : FlatMap(rhs._hashfn, rhs._eql, rhs.get_allocator()) {
    init_buckets_and_thumbnail(_buckets, _thumbnail, _nbucket);
    if (!rhs.empty()) {
        operator=(rhs);
    }
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
FlatMap<_K, _T, _H, _E, _S, _A, _M>::~FlatMap() {
    clear();
    if (!is_default_buckets()) {
        get_allocator().Free(_buckets);
        _buckets = NULL;
        bit_array_free(_thumbnail);
        _thumbnail = NULL;
    }
    _nbucket = 0;
    _load_factor = 0;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
FlatMap<_K, _T, _H, _E, _S, _A, _M>&
FlatMap<_K, _T, _H, _E, _S, _A, _M>::operator=(
    const FlatMap<_K, _T, _H, _E, _S, _A, _M>& rhs) {
    if (this == &rhs) {
        return *this;
    }

    clear();
    if (rhs.empty()) {
        return *this;
    }
    // NOTE: assignment only changes _load_factor when it is default.
    init_load_factor(rhs._load_factor);
    if (is_too_crowded(rhs._size)) {
        optional<NewBucketsInfo> info =
            new_buckets_and_thumbnail(rhs._size, rhs._nbucket);
        if (info.has_value()) {
            _nbucket = info->nbucket;
            if (!is_default_buckets()) {
                get_allocator().Free(_buckets);
                if (_S) {
                    bit_array_free(_thumbnail);
                }
            }
            _buckets = info->buckets;
            _thumbnail = info->thumbnail;
        }
        // Failed new of buckets or thumbnail is OK.
        // Use old buckets and thumbnail even if map will be crowded.
    }
    if (_nbucket == rhs._nbucket) {
        // For equivalent _nbucket, walking through _buckets instead of using
        // iterators is more efficient.
        for (size_t i = 0; i < rhs._nbucket; ++i) {
            if (rhs._buckets[i].is_valid()) {
                if (_S) {
                    bit_array_set(_thumbnail, i);
                }
                new (&_buckets[i]) Bucket(rhs._buckets[i]);
                Bucket* p1 = &_buckets[i];
                Bucket* p2 = rhs._buckets[i].next;
                while (p2) {
                    p1->next = new (_pool.get()) Bucket(*p2);
                    p1 = p1->next;
                    p2 = p2->next;
                }
            }
        }
        _buckets[rhs._nbucket].next = NULL;
        _size = rhs._size;
    } else {
        for (const_iterator it = rhs.begin(); it != rhs.end(); ++it) {
            operator[](Element::first_ref_from_value(*it)) =
                Element::second_ref_from_value(*it);
        }
    }
    return *this;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
int FlatMap<_K, _T, _H, _E, _S, _A, _M>::init(size_t nbucket, u_int load_factor) {
    if (nbucket <= _nbucket || load_factor < 10 || load_factor > 100 ||
        !_is_default_load_factor || !empty() || !is_default_buckets()) {
        return 0;
    }

    init_load_factor(load_factor);
    return resize(nbucket) ? 0 : -1;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
void FlatMap<_K, _T, _H, _E, _S, _A, _M>::swap(
    FlatMap<_K, _T, _H, _E, _S, _A, _M>& rhs) {
    if (!is_default_buckets() && !rhs.is_default_buckets()) {
        std::swap(rhs._buckets, _buckets);
        std::swap(rhs._thumbnail, _thumbnail);
    } else {
        for (size_t i = 0; i < DEFAULT_NBUCKET; ++i) {
            _default_buckets[i].swap(rhs._default_buckets[i]);
        }
        if (_S) {
            for (size_t i = 0; i < default_nthumbnail; ++i) {
                std::swap(_default_thumbnail[i], rhs._default_thumbnail[i]);
            }
        }
        if (!is_default_buckets() && rhs.is_default_buckets()) {
            rhs._buckets = _buckets;
            rhs._thumbnail = _thumbnail;
            _buckets = _default_buckets;
            _thumbnail = _default_thumbnail;
        } else if (is_default_buckets() && !rhs.is_default_buckets()) {
            _buckets = rhs._buckets;
            _thumbnail = rhs._thumbnail;
            rhs._buckets = rhs._default_buckets;
            rhs._thumbnail = rhs._thumbnail;
        } // else both are default buckets which has been swapped, so no need to swap `_buckets'.
    }

    std::swap(rhs._size, _size);
    std::swap(rhs._nbucket, _nbucket);
    std::swap(rhs._is_default_load_factor, _is_default_load_factor);
    std::swap(rhs._load_factor, _load_factor);
    std::swap(rhs._hashfn, _hashfn);
    std::swap(rhs._eql, _eql);
    rhs._pool.swap(_pool);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
_T* FlatMap<_K, _T, _H, _E, _S, _A, _M>::insert(
    const key_type& key, const mapped_type& value) {
    mapped_type *p = &operator[](key);
    *p = value;
    return p;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
_T* FlatMap<_K, _T, _H, _E, _S, _A, _M>::insert(
    const std::pair<key_type, mapped_type>& kv) {
    return insert(kv.first, kv.second);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template <typename K2, bool Multi>
typename std::enable_if<!Multi, size_t >::type
FlatMap<_K, _T, _H, _E, _S, _A, _M>::erase(const K2& key, _T* old_value) {
    // TODO: Do we need auto collapsing here?
    const size_t index = flatmap_mod(_hashfn(key), _nbucket);
    Bucket& first_node = _buckets[index];
    if (!first_node.is_valid()) {
        return 0;
    }
    if (_eql(first_node.element().first_ref(), key)) {
        if (old_value) {
            *old_value = first_node.element().second_movable_ref();
        }
        if (first_node.next == NULL) {
            first_node.destroy_element();
            first_node.set_invalid();
            if (_S) {
                bit_array_unset(_thumbnail, index);
            }
        } else {
            // A seemingly correct solution is to copy the memory of *p to
            // first_node directly like this:
            //   first_node.destroy_element();
            //   first_node = *p;
            // It works at most of the time, but is wrong generally.
            // If _T references self inside like this:
            //   Value {
            //     Value() : num(0), num_ptr(&num) {}
            //     int num;
            //     int* num_ptr;
            //   };
            // After copying, num_ptr will be invalid.
            // Calling operator= is the price that we have to pay.
            Bucket* p = first_node.next;
            first_node.next = p->next;
            const_cast<_K&>(first_node.element().first_ref()) =
                p->element().first_ref();
            first_node.element().second_ref() = p->element().second_movable_ref();
            p->destroy_element();
            _pool.back(p);
        }
        --_size;
        return 1UL;
    }
    Bucket *p = first_node.next;
    Bucket *last_p = &first_node;
    while (p) {
        if (_eql(p->element().first_ref(), key)) {
            if (old_value) {
                *old_value = p->element().second_movable_ref();
            }
            last_p->next = p->next;
            p->destroy_element();
            _pool.back(p);
            --_size;
            return 1UL;
        }
        last_p = p;
        p = p->next;
    }
    return 0;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template <typename K2, bool Multi>
typename std::enable_if<Multi, size_t >::type
FlatMap<_K, _T, _H, _E, _S, _A, _M>::erase(
    const K2& key, std::vector<mapped_type>* old_values) {
    // TODO: Do we need auto collapsing here?
    const size_t index = flatmap_mod(_hashfn(key), _nbucket);
    Bucket& first_node = _buckets[index];
    if (!first_node.is_valid()) {
        return 0;
    }

    Bucket* new_head = NULL;
    Bucket* new_tail = NULL;
    Bucket* p = &first_node;
    size_t total = _size;
    while (NULL != p) {
        if (_eql(p->element().first_ref(), key)) {
            if (NULL != old_values) {
                old_values->push_back(p->element().second_movable_ref());
            }
            Bucket* temp = p;
            p = p->next;
            temp->destroy_element();
            if (temp != &first_node) {
                _pool.back(temp);
            }
            --_size;
        } else {
            if (NULL == new_head) {
                new_head = p;
                new_tail = p;
            } else {
                new_tail->next = p;
                new_tail = new_tail->next;
            }
            p = p->next;
        }
    }
    if (NULL != new_tail) {
        new_tail->next = NULL;
    }
    if (NULL == new_head) {
        // Erase all element.
        first_node.set_invalid();
        if (_S) {
            bit_array_unset(_thumbnail, index);
        }
    } else if (new_head != &first_node) {
        // The First node has been erased, need to move new head node as first node.
        first_node.next = new_head->next;
        const_cast<_K&>(first_node.element().first_ref()) =
            new_head->element().first_ref();
        first_node.element().second_ref() = new_head->element().second_movable_ref();
        new_head->destroy_element();
        _pool.back(new_head);
    }
    return total - _size;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
void FlatMap<_K, _T, _H, _E, _S, _A, _M>::clear() {
    if (0 == _size) {
        return;
    }
    _size = 0;
    if (NULL != _buckets) {
        for (size_t i = 0; i < _nbucket; ++i) {
            Bucket& first_node = _buckets[i];
            if (first_node.is_valid()) {
                first_node.destroy_element();
                Bucket* p = first_node.next;
                while (p) {
                    Bucket* next_p = p->next;
                    p->destroy_element();
                    _pool.back(p);
                    p = next_p;
                }
                first_node.set_invalid();
            }
        }
    }
    if (NULL != _thumbnail) {
        bit_array_clear(_thumbnail, _nbucket);
    }
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
void FlatMap<_K, _T, _H, _E, _S, _A, _M>::clear_and_reset_pool() {
    clear();
    _pool.reset();
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template <typename K2>
_T* FlatMap<_K, _T, _H, _E, _S, _A, _M>::seek(const K2& key) const {
    Bucket& first_node = _buckets[flatmap_mod(_hashfn(key), _nbucket)];
    if (!first_node.is_valid()) {
        return NULL;
    }
    if (_eql(first_node.element().first_ref(), key)) {
        return &first_node.element().second_ref();
    }
    Bucket *p = first_node.next;
    while (p) {
        if (_eql(p->element().first_ref(), key)) {
            return &p->element().second_ref();
        }
        p = p->next;
    }
    return NULL;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template <typename K2> std::vector<_T*>
FlatMap<_K, _T, _H, _E, _S, _A, _M>::seek_all(const K2& key) const {
    std::vector<_T*> v;
    Bucket& first_node = _buckets[flatmap_mod(_hashfn(key), _nbucket)];
    if (!first_node.is_valid()) {
        return v;
    }
    if (_eql(first_node.element().first_ref(), key)) {
        v.push_back(&first_node.element().second_ref());
    }
    Bucket *p = first_node.next;
    while (p) {
        if (_eql(p->element().first_ref(), key)) {
            v.push_back(&p->element().second_ref());
        }
        p = p->next;
    }
    return v;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template<bool Multi>
typename std::enable_if<!Multi, _T&>::type
FlatMap<_K, _T, _H, _E, _S, _A, _M>::operator[](const key_type& key) {
    const size_t index = flatmap_mod(_hashfn(key), _nbucket);
    Bucket& first_node = _buckets[index];
    // LOG(INFO) << "index=" << index;
    if (!first_node.is_valid()) {
        ++_size;
        if (_S) {
            bit_array_set(_thumbnail, index);
        }
        new (&first_node) Bucket(key);
        return first_node.element().second_ref();
    }
    Bucket *p = &first_node;
    while (true) {
        if (_eql(p->element().first_ref(), key)) {
            return p->element().second_ref();
        }
        if (NULL == p->next) {
            if (is_too_crowded(_size) && resize(_nbucket + 1)) {
                return operator[](key);
            }
            // Fail to resize is OK.
            ++_size;
            Bucket* newp = new (_pool.get()) Bucket(key);
            p->next = newp;
            return newp->element().second_ref();
        }
        p = p->next;
    }
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
template<bool Multi>
typename std::enable_if<Multi, _T&>::type
FlatMap<_K, _T, _H, _E, _S, _A, _M>::operator[](const key_type& key) {
    const size_t index = flatmap_mod(_hashfn(key), _nbucket);
    Bucket& first_node = _buckets[index];
    if (!first_node.is_valid()) {
        ++_size;
        if (_S) {
            bit_array_set(_thumbnail, index);
        }
        new (&first_node) Bucket(key);
        return first_node.element().second_ref();
    }
    if (is_too_crowded(_size)) {
        Bucket *p = &first_node;
        bool need_scale = false;
        while (NULL != p) {
            // Increase the capacity of bucket when
            // hash collision occur and map is crowded.
            if (!_eql(p->element().first_ref(), key)) {
                need_scale = true;
                break;
            }
            p = p->next;
        }
        if (need_scale && resize(_nbucket + 1)) {
            return operator[](key);
        }
        // Failed resize is OK.
    }
    ++_size;
    Bucket* newp = new (_pool.get()) Bucket(key);
    newp->next = first_node.next;
    first_node.next = newp;
    return newp->element().second_ref();
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
void FlatMap<_K, _T, _H, _E, _S, _A, _M>::save_iterator(
    const const_iterator& it, PositionHint* hint) const {
    hint->nbucket = _nbucket;
    hint->offset = it._entry - _buckets;
    if (it != end()) {
        hint->at_entry = (it._entry == it._node);
        hint->key = it->first;
    } else {
        hint->at_entry = false;
        hint->key = key_type();
    }
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::const_iterator
FlatMap<_K, _T, _H, _E, _S, _A, _M>::restore_iterator(
    const PositionHint& hint) const {
    if (hint.nbucket != _nbucket)  // resized
        return begin(); // restart

    if (hint.offset >= _nbucket) // invalid hint, stop the iteration
        return end();

    Bucket& first_node = _buckets[hint.offset];
    if (hint.at_entry) {
        return const_iterator(this, hint.offset);
    }
    if (!first_node.is_valid()) {
        // All elements hashed to the entry were removed, try next entry.
        return const_iterator(this, hint.offset + 1);
    }
    Bucket *p = &first_node;
    do {
        if (_eql(p->element().first_ref(), hint.key)) {
            const_iterator it;
            it._node = p;
            it._entry = &first_node;
            return it;
        }
        p = p->next;
    } while (p);
    // Last element that we iterated (and saved in PositionHint) was removed,
    // don't know which element to start, just restart at the beginning of
    // the entry. Some elements in the entry may be revisited, which
    // shouldn't be often.
    return const_iterator(this, hint.offset);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
optional<typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::NewBucketsInfo>
FlatMap<_K, _T, _H, _E, _S, _A, _M>::new_buckets_and_thumbnail(size_t size,
                                                               size_t new_nbucket) {
    do {
        new_nbucket = flatmap_round(new_nbucket);
    } while (is_too_crowded(size, new_nbucket, _load_factor));
    if (_nbucket == new_nbucket) {
        return nullopt;
    }
    // Note: need an extra bucket to let iterator know where buckets end.
    auto buckets = (Bucket*)get_allocator().Alloc(
        sizeof(Bucket) * (new_nbucket + 1/*note*/));
    auto guard = MakeScopeGuard([buckets, this]() {
        get_allocator().Free(buckets);
    });
    if (NULL == buckets) {
        LOG(FATAL) << "Fail to new Buckets";
        return nullopt;
    }

    uint64_t* thumbnail = NULL;
    if (_S) {
        thumbnail = bit_array_malloc(new_nbucket);
        if (NULL == thumbnail) {
            LOG(FATAL) << "Fail to new thumbnail";
            return nullopt;
        }
    }

    guard.dismiss();
    init_buckets_and_thumbnail(buckets, thumbnail, new_nbucket);
    return NewBucketsInfo{buckets, thumbnail, new_nbucket};
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
bool FlatMap<_K, _T, _H, _E, _S, _A, _M>::resize(size_t nbucket) {
    optional<NewBucketsInfo> info = new_buckets_and_thumbnail(_size, nbucket);
    if (!info.has_value()) {
        return false;
    }

    for (iterator it = begin(); it != end(); ++it) {
        const key_type& key = Element::first_ref_from_value(*it);
        const size_t index = flatmap_mod(_hashfn(key), info->nbucket);
        Bucket& first_node = info->buckets[index];
        if (!first_node.is_valid()) {
            if (_S) {
                bit_array_set(info->thumbnail, index);
            }
            new (&first_node) Bucket(key);
            first_node.element().second_ref() =
                Element::second_movable_ref_from_value(*it);
        } else {
            Bucket* newp = new (_pool.get()) Bucket(key);
            newp->element().second_ref() =
                Element::second_movable_ref_from_value(*it);
            newp->next = first_node.next;
            first_node.next = newp;
        }
    }
    size_t saved_size = _size;
    clear();
    if (!is_default_buckets()) {
        get_allocator().Free(_buckets);
        if (_S) {
            bit_array_free(_thumbnail);
        }
    }
    _nbucket = info->nbucket;
    _buckets = info->buckets;
    _thumbnail = info->thumbnail;
    _size = saved_size;

    return true;
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
BucketInfo FlatMap<_K, _T, _H, _E, _S, _A, _M>::bucket_info() const {
    size_t max_n = 0;
    size_t nentry = 0;
    for (size_t i = 0; i < _nbucket; ++i) {
        if (_buckets[i].is_valid()) {
            size_t n = 1;
            for (Bucket* p = _buckets[i].next; p; p = p->next, ++n);
            max_n = std::max(max_n, n);
            ++nentry;
        }
    }
    return { max_n, size() / (double)nentry };
}

inline std::ostream& operator<<(std::ostream& os, const BucketInfo& info) {
    return os << "{maxb=" << info.longest_length
              << " avgb=" << info.average_length << '}';
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::iterator
FlatMap<_K, _T, _H, _E, _S, _A, _M>::begin() {
    return iterator(this, 0);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::iterator
FlatMap<_K, _T, _H, _E, _S, _A, _M>::end() {
    return iterator(this, _nbucket);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::const_iterator
FlatMap<_K, _T, _H, _E, _S, _A, _M>::begin() const {
    return const_iterator(this, 0);
}

template <typename _K, typename _T, typename _H, typename _E,
          bool _S, typename _A, bool _M>
typename FlatMap<_K, _T, _H, _E, _S, _A, _M>::const_iterator
FlatMap<_K, _T, _H, _E, _S, _A, _M>::end() const {
    return const_iterator(this, _nbucket);
}

}  // namespace butil

#endif  //BUTIL_FLAT_MAP_INL_H
