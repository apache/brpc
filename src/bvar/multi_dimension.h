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

// Date: 2021/11/17 10:57:43

#ifndef BVAR_MULTI_DIMENSION_H
#define BVAR_MULTI_DIMENSION_H

#include <memory>
#include <type_traits>
#include "butil/logging.h"                           // LOG
#include "butil/macros.h"                            // BAIDU_CASSERT
#include "butil/scoped_lock.h"                       // BAIDU_SCOPE_LOCK
#include "butil/containers/doubly_buffered_data.h"   // DBD
#include "butil/containers/flat_map.h"               // butil::FlatMap
#include "butil/strings/string_piece.h"
#include "bvar/mvariable.h"

namespace bvar {

// KeyType requirements:
// 1. KeyType must be a container type with iterator, e.g. std::vector, std::list, std::set.
// 2. KeyType::value_type must be std::string.
// 3. KeyType::size() returns the number of labels.
// 4. KeyType::push_back() adds a label to the end of the container.
//
// If `Shared' is false, `get_stats' returns a raw pointer,
// `delete_stats' and `clear_stats' are not thread safe.
// If `Shared' is true, `get_stats` returns a shared_ptr,
// `delete_stats' and `clear_stats' are thread safe.
// Note: The shared mode may be less performant than the non-shared mode.
template <typename T, typename KeyType = std::list<std::string>, bool Shared = false>
class MultiDimension : public MVariable<KeyType> {
    typedef std::shared_ptr<T> shared_value_type;
public:
    enum STATS_OP {
        READ_ONLY,
        READ_OR_INSERT,
    };

    typedef KeyType key_type;
    typedef T value_type;
    typedef typename std::conditional<Shared, shared_value_type, T*>::type value_ptr_type;
    typedef MVariable<key_type> Base;

    struct KeyHash {
        template <typename K>
        size_t operator() (const K& key) const {
            size_t hash_value = 0;
            for (auto& k : key) {
                hash_value += BUTIL_HASH_NAMESPACE::hash<butil::StringPiece>()(
                    butil::StringPiece(k));
            }
            return hash_value;
        }
    };

    struct KeyEqualTo {
        template <typename K>
        bool operator()(const key_type& k1, const K& k2) const {
            return k1.size() == k2.size() &&
                   std::equal(k1.cbegin(), k1.cend(), k2.cbegin());
        }
    };
    
    typedef value_ptr_type op_value_type;
    typedef butil::FlatMap<key_type, op_value_type, KeyHash, KeyEqualTo> MetricMap;

    typedef typename MetricMap::const_iterator MetricMapConstIterator;
    typedef butil::DoublyBufferedData<MetricMap> MetricMapDBD;
    typedef typename MetricMapDBD::ScopedPtr MetricMapScopedPtr;
    
    explicit MultiDimension(const key_type& labels);
    
    MultiDimension(const butil::StringPiece& name,
                   const key_type& labels);
    
    MultiDimension(const butil::StringPiece& prefix,
                   const butil::StringPiece& name,
                   const key_type& labels);

    ~MultiDimension() override;

    // Implement this method to print the variable into ostream.
    void describe(std::ostream& os) override;

    // Dump real bvar pointer
    size_t dump(Dumper* dumper, const DumpOptions* options) override {
        return dump_impl(dumper, options);
    }

    // Get real bvar pointer object
    // Return real bvar pointer on success, NULL otherwise.
    // K requirements:
    // 1. K must be a container type with iterator,
    //    e.g. std::vector, std::list, std::set, std::array.
    // 2. K::value_type must be able to convert to std::string and butil::StringPiece
    //    through operator std::string() function and operator butil::StringPiece() function.
    // 3. K::value_type must be able to compare with std::string.
    //
    // Returns a shared_ptr if `Shared' is true, otherwise returns a raw pointer.
    template <typename K = key_type>
    value_ptr_type get_stats(const K& labels_value) {
        return get_stats_impl(labels_value, READ_OR_INSERT);
    }

    // `delete_stats' and `clear_stats' are thread safe
    // if `Shared' is true, otherwise not.
    // Remove stat so those not count and dump
    template <typename K = key_type>
    void delete_stats(const K& labels_value);

    // Remove all stat
    void clear_stats();

    // True if bvar pointer exists
    template <typename K = key_type>
    bool has_stats(const K& labels_value);

    // Get number of stats
    size_t count_stats();

    // Put name of all stats label into `names'
    void list_stats(std::vector<key_type>* names);

    void set_max_stats_count(size_t max_stats_count) {
        _max_stats_count = std::max(max_stats_count, max_stats_count);
    }
    
#ifdef UNIT_TEST
    // Get real bvar pointer object 
    // Return real bvar pointer if labels_name exist, NULL otherwise.
    // CAUTION!!! Just For Debug!!!
    template <typename K = key_type>
    value_ptr_type get_stats_read_only(const K& labels_value) {
        return get_stats_impl(labels_value);
    }

    // Get real bvar pointer object 
    // Return real bvar pointer if labels_name exist, otherwise(not exist) create bvar pointer.
    // CAUTION!!! Just For Debug!!!
    template <typename K = key_type>
    value_ptr_type get_stats_read_or_insert(const K& labels_value, bool* do_write = NULL) {
        return get_stats_impl(labels_value, READ_OR_INSERT, do_write);
    }
#endif

private:
    template <typename K>
    value_ptr_type get_stats_impl(const K& labels_value);

    template <typename K>
    value_ptr_type get_stats_impl(
        const K& labels_value, STATS_OP stats_op, bool* do_write = NULL);

    template <typename K>
    static typename std::enable_if<butil::is_same<K, key_type>::value>::type
    insert_metrics_map(MetricMap& bg, const K& labels_value, op_value_type metric) {
        bg.insert(labels_value, metric);
    }

    template <typename K>
    static typename std::enable_if<!butil::is_same<K, key_type>::value>::type
    insert_metrics_map(MetricMap& bg, const K& labels_value, op_value_type metric) {
        // key_type::value_type must be able to convert to std::string.
        key_type labels_value_str(labels_value.cbegin(), labels_value.cend());
        bg.insert(labels_value_str, metric);
    }

    template <typename U = T>
    typename std::enable_if<!butil::is_same<LatencyRecorder, U>::value, size_t>::type
    dump_impl(Dumper* dumper, const DumpOptions* options);

    template <typename U = T>
    typename std::enable_if<butil::is_same<LatencyRecorder, U>::value, size_t>::type
    dump_impl(Dumper* dumper, const DumpOptions* options);

    void make_dump_key(std::ostream& os, const key_type& labels_value,
                       const std::string& suffix = "",  int quantile = 0);

    void make_labels_kvpair_string(
        std::ostream& os, const key_type& labels_value, int quantile);


    template <typename K>
    bool is_valid_lables_value(const K& labels_value) const;
    
    // Remove all stats so those not count and dump
    void delete_stats();
    
    static size_t init_flatmap(MetricMap& bg);

    // If Shared is true, return std::shared_ptr, otherwise return raw pointer.
    template <bool S = Shared>
    typename std::enable_if<S, value_ptr_type>::type  new_value() {
        return std::make_shared<value_type>();
    }
    template <bool S = Shared>
    typename std::enable_if<!S, value_ptr_type>::type  new_value() {
        return new value_type();
    }

    // If Shared is true, reset std::shared_ptr, otherwise delete raw pointer.
    template <bool S = Shared>
    typename std::enable_if<S>::type delete_value(value_ptr_type& v) {
        v.reset();
    }
    template <bool S = Shared>
    typename std::enable_if<!S>::type delete_value(value_ptr_type& v) {
        delete v;
    }

    size_t _max_stats_count;
    MetricMapDBD _metric_map;
};

} // namespace bvar

#include "bvar/multi_dimension_inl.h"

#endif // BVAR_MULTI_DIMENSION_H
