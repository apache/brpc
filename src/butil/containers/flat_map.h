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

// This closed addressing hash-map puts first linked node in bucket array
// directly to save an extra memory indirection. As a result, this map yields
// close performance to raw array on nearly all operations, probably being the
// fastest hashmap for small-sized key/value ever.
//
// Performance comparisons between several maps:
// [ value = 8 bytes ]
// Sequentially inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/300/290/2010/210/230
// Sequentially erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/1700/150/160/170/250
// Sequentially inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 16/15/360/342/206/195/219
// Sequentially erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/18/178/159/149/159/149
// Sequentially inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/15/415/410/200/192/235
// Sequentially erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 14/17/201/181/151/181/154
// [ value = 32 bytes ]
// Sequentially inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/280/280/250/200/230
// Sequentially erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/2070/150/160/160/160
// Sequentially inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 17/17/330/329/207/185/212
// Sequentially erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/17/172/163/146/157/148
// Sequentially inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 17/17/405/406/197/185/215
// Sequentially erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/16/206/188/158/168/159
// [ value = 128 bytes ]
// Sequentially inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/30/290/290/420/220/250
// Sequentially erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/180/150/160/160/160
// Sequentially inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 22/25/352/349/213/193/222
// Sequentially erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/17/170/165/160/171/157
// Sequentially inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 21/24/416/422/210/206/242
// Sequentially erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 16/16/213/190/163/171/159
// [ value = 8 bytes ]
// Randomly inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/290/260/250/220/220
// Randomly erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/240/220/170/170/180
// Randomly inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 16/15/315/309/206/191/215
// Randomly erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/17/258/240/155/193/156
// Randomly inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/16/378/363/208/191/210
// Randomly erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 14/16/311/290/162/187/169
// [ value = 32 bytes ]
// Randomly inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/280/270/240/230/220
// Randomly erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 10/20/250/220/170/180/160
// Randomly inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 18/18/310/304/209/192/209
// Randomly erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/16/255/247/155/175/152
// Randomly inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 17/17/381/367/209/197/214
// Randomly erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/17/310/296/163/188/168
// [ value = 128 bytes ]
// Randomly inserting 100 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 30/40/300/290/280/230/230
// Randomly erasing 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/20/230/220/160/180/170
// Randomly inserting 1000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 29/33/327/329/219/197/220
// Randomly erasing 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 14/16/257/247/159/182/156
// Randomly inserting 10000 into FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 35/39/398/400/220/213/246
// Randomly erasing 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 35/36/330/319/221/224/200
// [ value = 8 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 10/20/140/130/60/70/50
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/172/161/77/54/46
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/16/216/211/73/56/51
// [ value = 32 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 10/20/130/130/70/60/50
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/174/163/73/54/49
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/218/217/75/58/52
// [ value = 128 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/140/130/80/50/60
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/173/171/73/55/49
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 26/22/238/252/107/89/91
// [ value = 8 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/140/130/70/50/60
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/14/180/160/68/57/47
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/221/210/72/57/51
// [ value = 32 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 20/10/140/130/70/60/50
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/167/160/69/53/50
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 15/14/224/219/75/59/52
// [ value = 128 bytes ]
// Seeking 100 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 10/10/140/130/80/50/60
// Seeking 1000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 13/13/170/167/70/54/52
// Seeking 10000 from FlatMap/MultiFlatMap/std::map/butil::PooledMap/std::unordered_map/std::unordered_multimap/butil::hash_map takes 26/22/238/240/85/70/67

#ifndef BUTIL_FLAT_MAP_H
#define BUTIL_FLAT_MAP_H

#include <stdint.h>
#include <cstddef>
#include <functional>
#include <iostream>                               // std::ostream
#include <type_traits>                            // std::aligned_storage
#include "butil/type_traits.h"
#include "butil/logging.h"
#include "butil/find_cstr.h"
#include "butil/single_threaded_pool.h"            // SingleThreadedPool
#include "butil/containers/hash_tables.h"          // hash<>
#include "butil/bit_array.h"                       // bit_array_*
#include "butil/strings/string_piece.h"            // StringPiece
#include "butil/memory/scope_guard.h"
#include "butil/containers/optional.h"

namespace butil {

template <typename _Map, typename _Element> class FlatMapIterator;
template <typename _Map, typename _Element> class SparseFlatMapIterator;
template <typename K, typename T> class FlatMapElement;
struct FlatMapVoid {}; // Replace void which is not constructible.
template <typename K> struct DefaultHasher;
template <typename K> struct DefaultEqualTo;

struct BucketInfo {
    size_t longest_length;
    double average_length;
};

#ifndef BRPC_FLATMAP_DEFAULT_NBUCKET
#ifdef FLAT_MAP_ROUND_BUCKET_BY_USE_NEXT_PRIME
#define BRPC_FLATMAP_DEFAULT_NBUCKET 29U
#else
#define BRPC_FLATMAP_DEFAULT_NBUCKET 16U
#endif
#endif // BRPC_FLATMAP_DEFAULT_NBUCKET

// NOTE: Objects stored in FlatMap MUST be copyable.
template <typename _K, typename _T,
          // Compute hash code from key.
          // Use src/butil/third_party/murmurhash3 to make better distributions.
          typename _Hash = DefaultHasher<_K>,
          // Test equivalence between stored-key and passed-key.
          // stored-key is always on LHS, passed-key is always on RHS.
          typename _Equal = DefaultEqualTo<_K>,
          bool _Sparse = false,
          typename _Alloc = PtAllocator,
          // If `_Multi' is true, allow containing multiple copies of each key value.
          bool _Multi = false>
class FlatMap {
public:
    typedef _K key_type;
    typedef _T mapped_type;
    typedef _Alloc allocator_type;
    typedef FlatMapElement<_K, _T> Element;
    typedef typename Element::value_type value_type;
    typedef typename conditional<
        _Sparse, SparseFlatMapIterator<FlatMap, value_type>,
        FlatMapIterator<FlatMap, value_type> >::type iterator;
    typedef typename conditional<
        _Sparse, SparseFlatMapIterator<FlatMap, const value_type>,
        FlatMapIterator<FlatMap, const value_type> >::type const_iterator;
    typedef _Hash hasher;
    typedef _Equal key_equal;
    static constexpr size_t DEFAULT_NBUCKET = BRPC_FLATMAP_DEFAULT_NBUCKET;

    struct PositionHint {
        size_t nbucket;
        size_t offset;
        bool at_entry;
        key_type key;
    };

    explicit FlatMap(const hasher& hashfn = hasher(),
                     const key_equal& eql = key_equal(),
                     const allocator_type& alloc = allocator_type());
    FlatMap(const FlatMap& rhs);
    ~FlatMap();

    FlatMap& operator=(const FlatMap& rhs);
    void swap(FlatMap & rhs);

    // FlatMap will be automatically initialized with small FlatMap optimization,
    // so this function only needs to be call when a large initial number of
    // buckets or non-default `load_factor' is required.
    // Returns 0 on success, -1 on error, but FlatMap can still be used normally.
    // `nbucket' is the initial number of buckets. `load_factor' is the
    // maximum value of size()*100/nbucket, if the value is reached, nbucket
    // will be doubled and all items stored will be rehashed which is costly.
    // Choosing proper values for these 2 parameters reduces costs.
    int init(size_t nbucket, u_int load_factor = 80);

    // Insert a pair of |key| and |value|. If size()*100/bucket_count() is
    // more than load_factor(), a resize() will be done.
    // Returns address of the inserted value, NULL on error.
    mapped_type* insert(const key_type& key, const mapped_type& value);

    // Insert a pair of {key, value}. If size()*100/bucket_count() is
    // more than load_factor(), a resize() will be done.
    // Returns address of the inserted value, NULL on error.
    mapped_type* insert(const std::pair<key_type, mapped_type>& kv);

    // For `_Multi=false'. (Default)
    // Remove |key| and all associated value
    // Returns: 1 on erased, 0 otherwise.
    template <typename K2, bool Multi = _Multi>
    typename std::enable_if<!Multi, size_t >::type
    erase(const K2& key, mapped_type* old_value = NULL);
    // For `_Multi=true'.
    // Returns: num of value on erased, 0 otherwise.
    template <typename K2, bool Multi = _Multi>
    typename std::enable_if<Multi, size_t >::type
    erase(const K2& key, std::vector<mapped_type>* old_values = NULL);

    // Remove all items. Allocated spaces are NOT returned by system.
    void clear();

    // Remove all items and return all allocated spaces to system.
    void clear_and_reset_pool();

    // Search for the value associated with |key|.
    // If `_Multi=false', Search for any of multiple values associated with |key|.
    // Returns: address of the value.
    template <typename K2> mapped_type* seek(const K2& key) const;
    template <typename K2> std::vector<mapped_type*> seek_all(const K2& key) const;

    // For `_Multi=false'. (Default)
    // Get the value associated with |key|. If |key| does not exist,
    // insert with a default-constructed value. If size()*100/bucket_count()
    // is more than load_factor, a resize will be done.
    // Returns reference of the value
    template<bool Multi = _Multi>
    typename std::enable_if<!Multi, mapped_type&>::type operator[](const key_type& key);

    // Resize this map. This is optional because resizing will be triggered by
    // insert() or operator[] if there're too many items.
    // Returns successful or not.
    bool resize(size_t nbucket);

    // Iterators
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;

    // Iterate FlatMap inconsistently in more-than-one passes. This is used
    // in multi-threaded environment to divide the critical sections of
    // iterating big maps into smaller ones. "inconsistently" means that:
    //  * elements added during iteration may be missed.
    //  * some elements may be iterated more than once.
    //  * iteration is restarted at beginning when the map is resized.
    // Example: (copying all keys in multi-threaded environment)
    //   LOCK;
    //   size_t n = 0;
    //   for (Map::const_iterator it = map.begin(); it != map.end(); ++it) {
    //     if (++n >= 256/*max iterated one pass*/) {
    //       Map::PositionHint hint;
    //       map.save_iterator(it, &hint);
    //       n = 0;
    //       UNLOCK;
    //       LOCK;
    //       it = map.restore_iterator(hint);
    //       if (it == map.begin()) { // resized
    //         keys->clear();
    //       }
    //       if (it == map.end()) {
    //         break;
    //       }
    //     }
    //     keys->push_back(it->first);
    //   }
    //   UNLOCK;
    void save_iterator(const const_iterator&, PositionHint*) const;
    const_iterator restore_iterator(const PositionHint&) const;

    // Always returns true.
    bool initialized() const { return _buckets != NULL; }

    bool empty() const { return _size == 0; }
    size_t size() const { return _size; }
    size_t bucket_count() const { return _nbucket; }
    u_int load_factor () const { return _load_factor; }

    // Returns #nodes of longest bucket in this map. This scans all buckets.
    BucketInfo bucket_info() const;

    struct Bucket {
        Bucket() : next((Bucket*)-1UL) {}
        explicit Bucket(const _K& k) : next(NULL) {
            element_space_.Init(k);
        }
        Bucket(const Bucket& other) : next(NULL) {
            element_space_.Init(other.element());
        }

        bool is_valid() const { return next != (const Bucket*)-1UL; }
        void set_invalid() { next = (Bucket*)-1UL; }
        // NOTE: Only be called when is_valid() is true.
        Element& element() { return *element_space_; }
        const Element& element() const { return *element_space_; }
        void destroy_element() { element_space_.Destroy(); }

        void swap(Bucket& rhs) {
            if (!is_valid() && !rhs.is_valid()) {
                return;
            } else if (is_valid() && !rhs.is_valid()) {
                rhs.element_space_.Init(std::move(element()));
                destroy_element();
            } else if (!is_valid() && rhs.is_valid()) {
                element_space_.Init(std::move(rhs.element()));
                rhs.destroy_element();
            } else {
                element().swap(rhs.element());
            }
            std::swap(next, rhs.next);
        }

        Bucket* next;

    private:
        ManualConstructor<Element> element_space_;
    };

private:
template <typename _Map, typename _Element> friend class FlatMapIterator;
template <typename _Map, typename _Element> friend class SparseFlatMapIterator;

    struct NewBucketsInfo {
        NewBucketsInfo()
            : buckets(NULL), thumbnail(NULL), nbucket(0) {}
        NewBucketsInfo(Bucket* b, uint64_t* t, size_t n)
            : buckets(b), thumbnail(t), nbucket(n) {}

        Bucket* buckets;
        uint64_t* thumbnail;
        size_t nbucket;
    };

    optional<NewBucketsInfo> new_buckets_and_thumbnail(size_t size, size_t new_nbucket);

    // For `_Multi=true'.
    // Insert a new default-constructed associated with |key| always.
    // If size()*100/bucket_count() is more than load_factor,
    // a resize will be done.
    // Returns reference of the value
    template<bool Multi = _Multi>
    typename std::enable_if<Multi, mapped_type&>::type operator[](const key_type& key);

    allocator_type& get_allocator() { return _pool.get_allocator(); }
    allocator_type get_allocator() const { return _pool.get_allocator(); }

    // True if buckets need to be resized before holding `size' elements.
    bool is_too_crowded(size_t size) const {
        return is_too_crowded(size, _nbucket, _load_factor);
    }
    static bool is_too_crowded(size_t size, size_t nbucket, u_int load_factor) {
        return size * 100 >= nbucket * load_factor;
    }

    void init_load_factor(u_int load_factor) {
        if (_is_default_load_factor) {
            _is_default_load_factor = false;
            _load_factor = load_factor;
        }
    }

    // True if using default buckets.
    bool is_default_buckets() const {
        return _buckets == (Bucket*)(&_default_buckets);
    }

    static void init_buckets_and_thumbnail(Bucket* buckets,
                                           uint64_t* thumbnail,
                                           size_t nbucket) {
        for (size_t i = 0; i < nbucket; ++i) {
            buckets[i].set_invalid();
        }
        buckets[nbucket].next = NULL;
        if (_Sparse) {
            bit_array_clear(thumbnail, nbucket);
        }
    }

    static const size_t default_nthumbnail = BIT_ARRAY_LEN(DEFAULT_NBUCKET);
    // Note: need an extra bucket to let iterator know where buckets end.
    // Small map optimization.
    Bucket _default_buckets[DEFAULT_NBUCKET + 1];
    uint64_t _default_thumbnail[default_nthumbnail];
    size_t _size;
    size_t _nbucket;
    Bucket* _buckets;
    uint64_t* _thumbnail;
    u_int _load_factor;
    bool _is_default_load_factor;
    hasher _hashfn;
    key_equal _eql;
    SingleThreadedPool<sizeof(Bucket), 1024, 16, allocator_type> _pool;
};

template <typename _K, typename _T,
          typename _Hash = DefaultHasher<_K>,
          typename _Equal = DefaultEqualTo<_K>,
          bool _Sparse = false,
          typename _Alloc = PtAllocator>
using MultiFlatMap = FlatMap<
    _K, _T, _Hash, _Equal, _Sparse, _Alloc, true>;

template <typename _K,
          typename _Hash = DefaultHasher<_K>,
          typename _Equal = DefaultEqualTo<_K>,
          bool _Sparse = false,
          typename _Alloc = PtAllocator>
class FlatSet {
public:
    typedef FlatMap<_K, FlatMapVoid, _Hash, _Equal, _Sparse,
                    _Alloc, false> Map;
    typedef typename Map::key_type key_type;
    typedef typename Map::value_type value_type;
    typedef typename Map::Bucket Bucket;
    typedef typename Map::iterator iterator;
    typedef typename Map::const_iterator const_iterator;
    typedef typename Map::hasher hasher;
    typedef typename Map::key_equal key_equal;
    typedef typename Map::allocator_type allocator_type;
    
    explicit FlatSet(const hasher& hashfn = hasher(),
                     const key_equal& eql = key_equal(),
                     const allocator_type& alloc = allocator_type())
        : _map(hashfn, eql, alloc) {}
    void swap(FlatSet & rhs) { _map.swap(rhs._map); }

    int init(size_t nbucket, u_int load_factor = 80)
    { return _map.init(nbucket, load_factor); }

    const void* insert(const key_type& key)
    { return _map.insert(key, FlatMapVoid()); }

    template <typename K2>
    size_t erase(const K2& key) { return _map.erase(key, NULL); }

    void clear() { return _map.clear(); }
    void clear_and_reset_pool() { return _map.clear_and_reset_pool(); }

    template <typename K2>
    const void* seek(const K2& key) const { return _map.seek(key); }

    bool resize(size_t nbucket) { return _map.resize(nbucket); }
    
    iterator begin() { return _map.begin(); }
    iterator end() { return _map.end(); }
    const_iterator begin() const { return _map.begin(); }
    const_iterator end() const { return _map.end(); }

    bool initialized() const { return _map.initialized(); }
    bool empty() const { return _map.empty(); }
    size_t size() const { return _map.size(); }
    size_t bucket_count() const { return _map.bucket_count(); }
    u_int load_factor () const { return _map.load_factor(); }
    BucketInfo bucket_info() const { return _map.bucket_info(); }

private:
    Map _map;
};

template <typename _K, typename _T,
          typename _Hash = DefaultHasher<_K>,
          typename _Equal = DefaultEqualTo<_K>,
          typename _Alloc = PtAllocator,
          bool _Multi = false>
class SparseFlatMap : public FlatMap<
    _K, _T, _Hash, _Equal, true, _Alloc, _Multi> {};

template <typename _K,
          typename _Hash = DefaultHasher<_K>,
          typename _Equal = DefaultEqualTo<_K>,
          typename _Alloc = PtAllocator>
class SparseFlatSet : public FlatSet<
    _K, _Hash, _Equal, true, _Alloc> {};

// Implement FlatMapElement
template <typename K, typename T>
class FlatMapElement {
public:
    typedef std::pair<const K, T> value_type;
    // NOTE: Have to initialize _value in this way which is treated by GCC
    // specially that _value is zeroized(POD) or constructed(non-POD). Other
    // methods do not work. For example, if we put _value into the std::pair
    // and do initialization by calling _pair(k, T()), _value will be copy
    // constructed from the defaultly constructed instance(not zeroized for
    // POD) which is wrong generally.
    explicit FlatMapElement(const K& k) : _key(k), _value(T()) {}
    //                                             ^^^^^^^^^^^

    FlatMapElement(const FlatMapElement& rhs)
        : _key(rhs._key), _value(rhs._value) {}

    FlatMapElement(FlatMapElement&& rhs) noexcept
        : _key(std::move(rhs._key)), _value(std::move(rhs._value)) {}

    const K& first_ref() const { return _key; }
    T& second_ref() { return _value; }
    T&& second_movable_ref() { return std::move(_value); }
    value_type& value_ref() { return *reinterpret_cast<value_type*>(this); }
    inline static const K& first_ref_from_value(const value_type& v)
    { return v.first; }
    inline static const T& second_ref_from_value(const value_type& v)
    { return v.second; }
    inline static T&& second_movable_ref_from_value(value_type& v)
    { return std::move(v.second); }

    void swap(FlatMapElement& rhs) {
        std::swap(_key, rhs._key);
        std::swap(_value, rhs._value);
    }

private:
    K _key;
    T _value;
};

template <typename K>
class FlatMapElement<K, FlatMapVoid> {
public:
    typedef const K value_type;
    // See the comment in the above FlatMapElement.
    explicit FlatMapElement(const K& k) : _key(k) {}
    FlatMapElement(const FlatMapElement& rhs) : _key(rhs._key) {}
    FlatMapElement(FlatMapElement&& rhs) noexcept : _key(std::move(rhs._key)) {}

    const K& first_ref() const { return _key; }
    FlatMapVoid& second_ref() { return second_ref_from_value(_key); }
    FlatMapVoid& second_movable_ref() { return second_ref(); }
    value_type& value_ref() { return _key; }
    inline static const K& first_ref_from_value(value_type& v) { return v; }
    inline static FlatMapVoid& second_ref_from_value(value_type&) {
        static FlatMapVoid dummy;
        return dummy;
    }
    inline static const FlatMapVoid& second_movable_ref_from_value(value_type& v) {
        return second_ref_from_value(v);
    }
private:
    K _key;
};

// Implement DefaultHasher and DefaultEqualTo
template <typename K>
struct DefaultHasher : public BUTIL_HASH_NAMESPACE::hash<K> {};

template <>
struct DefaultHasher<std::string> {
    std::size_t operator()(const butil::StringPiece& s) const {
        std::size_t result = 0;
        for (butil::StringPiece::const_iterator
                 i = s.begin(); i != s.end(); ++i) {
            result = result * 101 + *i;
        }
        return result;
    }
    std::size_t operator()(const char* s) const {
        std::size_t result = 0;
        for (; *s; ++s) {
            result = result * 101 + *s;
        }
        return result;
    }
    std::size_t operator()(const std::string& s) const {
        std::size_t result = 0;
        for (std::string::const_iterator i = s.begin(); i != s.end(); ++i) {
            result = result * 101 + *i;
        }
        return result;        
    }
};

template <typename K>
struct DefaultEqualTo : public std::equal_to<K> {
};

template <>
struct DefaultEqualTo<std::string> {
    bool operator()(const std::string& s1, const std::string& s2) const
    { return s1 == s2; }
    bool operator()(const std::string& s1, const butil::StringPiece& s2) const
    { return s1 == s2; }
    bool operator()(const std::string& s1, const char* s2) const
    { return s1 == s2; }
};

// find_cstr and find_lowered_cstr
template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
const _T* find_cstr(const FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
                    const char* key) {
    return m.seek(key);
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
_T* find_cstr(FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
              const char* key) {
    return m.seek(key);
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
const _T* find_cstr(const FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
                    const char* key, size_t length) {
    return m.seek(butil::StringPiece(key, length));
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
_T* find_cstr(FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
              const char* key, size_t length) {
    return m.seek(butil::StringPiece(key, length));
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
const _T* find_lowered_cstr(
    const FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
    const char* key) {
    return m.seek(*tls_stringmap_temp.get_lowered_string(key));
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
_T* find_lowered_cstr(FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
                      const char* key) {
    return m.seek(*tls_stringmap_temp.get_lowered_string(key));
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
const _T* find_lowered_cstr(
    const FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
    const char* key, size_t length) {
    return m.seek(*tls_stringmap_temp.get_lowered_string(key, length));
}

template <typename _T, typename _Hash, typename _Equal, bool _Sparse>
_T* find_lowered_cstr(FlatMap<std::string, _T, _Hash, _Equal, _Sparse>& m,
                      const char* key, size_t length) {
    return m.seek(*tls_stringmap_temp.get_lowered_string(key, length));
}

}  // namespace butil

#include "butil/containers/flat_map_inl.h"

#endif  //BUTIL_FLAT_MAP_H
