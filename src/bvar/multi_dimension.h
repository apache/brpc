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

#include <functional>
#include <memory>
#include <type_traits>
#include "butil/logging.h"                           // LOG
#include "butil/macros.h"                            // BAIDU_CASSERT
#include "butil/scoped_lock.h"                       // BAIDU_SCOPE_LOCK
#include "butil/containers/doubly_buffered_data.h"   // DBD
#include "butil/containers/flat_map.h"               // butil::FlatMap
#include "butil/strings/string_piece.h"
#include "bvar/variable.h"                           // Dumper, IsCompositeMetric
#include "bvar/mvariable.h"

namespace bvar {

namespace detail {

template <typename ValuePtr>
auto hide_if_supported(ValuePtr& value, int) -> decltype(value->hide(), void()) {
    value->hide();
}

template <typename ValuePtr>
void hide_if_supported(ValuePtr&, ...) {}

}  // namespace detail

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
    
    // `args` are copied and supplied as const references to each value's
    // constructor. Only overloads that can construct T this way participate.
    // With no args, T must be default-constructible. A Histogram is the
    // typical one, its buckets are fixed at construction:
    //   bvar::MultiDimension<bvar::Histogram> h(
    //       "rpc_latency", {"method"},
    //       bvar::Histogram::BucketSchema({10, 50, 100, 500, 1000}));
    // They are copied once into the MultiDimension, nothing needs to outlive
    // the call.
    template <typename... Args,
              std::enable_if_t<std::is_constructible<
                  T, const typename std::decay<Args>::type&...>::value, int> = 0>
    explicit MultiDimension(const key_type& labels, Args&&... args);

    template <typename... Args,
              std::enable_if_t<std::is_constructible<
                  T, const typename std::decay<Args>::type&...>::value, int> = 0>
    MultiDimension(const butil::StringPiece& name,
                   const key_type& labels, Args&&... args);

    template <typename... Args,
              std::enable_if_t<std::is_constructible<
                  T, const typename std::decay<Args>::type&...>::value, int> = 0>
    MultiDimension(const butil::StringPiece& prefix,
                   const butil::StringPiece& name,
                   const key_type& labels, Args&&... args);

    ~MultiDimension() override;

    // Implement this method to print the variable into ostream.
    void describe(std::ostream& os) override;

    // Dump real bvar pointer
    size_t dump(Dumper* dumper, const DumpOptions* options) override {
        return dump_impl(dumper, options);
    }

    // Get real bvar pointer object
    // Return real bvar pointer on success, nullptr otherwise.
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
    // Return real bvar pointer if labels_name exist, nullptr otherwise.
    // CAUTION!!! Just For Debug!!!
    template <typename K = key_type>
    value_ptr_type get_stats_read_only(const K& labels_value) {
        return get_stats_impl(labels_value);
    }

    // Get real bvar pointer object 
    // Return real bvar pointer if labels_name exist, otherwise(not exist) create bvar pointer.
    // CAUTION!!! Just For Debug!!!
    template <typename K = key_type>
    value_ptr_type get_stats_read_or_insert(const K& labels_value, bool* do_write = nullptr) {
        return get_stats_impl(labels_value, READ_OR_INSERT, do_write);
    }
#endif

private:
    int expose_impl(const butil::StringPiece& prefix,
                    const butil::StringPiece& name) override {
        return _label_names_valid ? Base::expose_impl(prefix, name) : -1;
    }

    std::vector<std::string> collect_prometheus_names() const override {
        return collect_prometheus_names_impl<T>();
    }
    template <typename U>
    std::enable_if_t<!detail::IsCompositeMetric<U>::value, std::vector<std::string> >
    collect_prometheus_names_impl() const {
        return {this->name()};
    }
    template <typename U>
    std::enable_if_t<detail::IsCompositeMetric<U>::value, std::vector<std::string> >
    collect_prometheus_names_impl() const {
        return detail::collect_metric_family_names(this->name(), U::list_metric_families());
    }

    template <typename K>
    value_ptr_type get_stats_impl(const K& labels_value);

    template <typename K>
    value_ptr_type get_stats_impl(
        const K& labels_value, STATS_OP stats_op, bool* do_write = nullptr);

    template <typename K>
    static std::enable_if_t<butil::is_same<K, key_type>::value>
    insert_metrics_map(MetricMap& bg, const K& labels_value, op_value_type metric) {
        bg.insert(labels_value, metric);
    }

    template <typename K>
    static std::enable_if_t<!butil::is_same<K, key_type>::value>
    insert_metrics_map(MetricMap& bg, const K& labels_value, op_value_type metric) {
        // key_type::value_type must be able to convert to std::string.
        key_type labels_value_str(labels_value.cbegin(), labels_value.cend());
        bg.insert(labels_value_str, metric);
    }

    // One gauge per label set, its value from describe().
    template <typename U = T>
    std::enable_if_t<!detail::IsCompositeMetric<U>::value, size_t>
    dump_impl(Dumper* dumper, const DumpOptions* options);

    // T maps to several metrics and drives the dumper itself, see the contract
    // in bvar/variable.h.
    template <typename U = T>
    std::enable_if_t<detail::IsCompositeMetric<U>::value, size_t>
    dump_impl(Dumper* dumper, const DumpOptions* options);

    // Builds the name that one label set is dumped under into `key`, replacing
    // whatever it held: the exposed name followed by the labels in braces, as
    // in `foo{method="echo"}`. Takes a string rather than returning one so that
    // a dump can reuse the same buffer for all its label sets.
    void make_dump_key(std::string* key, const key_type& labels_value);

    // Appends the kvpairs alone, without the enclosing braces, to `key`.
    // A composite metric merges labels of its own (`le` for a Histogram,
    // `quantile` for a LatencyRecorder) into the same brace group and so
    // needs them unwrapped.
    // Returns true if at least one pair was appended.
    bool append_labels_kvpair_body(std::string* key, const key_type& labels_value);


    template <typename K>
    bool is_valid_lables_value(const K& labels_value) const;

    template <typename U = T>
    static std::enable_if_t<!detail::IsCompositeMetric<U>::value, bool>
    are_label_names_valid(const key_type& labels) {
        return true;
    }
    template <typename U = T>
    static std::enable_if_t<detail::IsCompositeMetric<U>::value, bool>
    are_label_names_valid(const key_type& labels);
    
    // Remove all stats so those not count and dump
    void delete_stats();
    
    static size_t init_flatmap(MetricMap& bg);

    // If Shared is true, return std::shared_ptr, otherwise return raw pointer.
    template <bool S = Shared, typename... Args>
    static std::enable_if_t<S, value_ptr_type>
    make_value(const Args&... args) {
        return std::make_shared<value_type>(args...);
    }
    template <bool S = Shared, typename... Args>
    static std::enable_if_t<!S, value_ptr_type>
    make_value(const Args&... args) {
        return new value_type(args...);
    }

    // Taken by value so that the arguments are copied into the closure.
    template <typename... Args>
    static std::function<value_ptr_type()> make_value_factory(Args... args) {
        return [args...]() {
            value_ptr_type value = make_value(args...);
            // Child metrics are exported through MultiDimension,
            // not on their own.
            detail::hide_if_supported(value, 0);
            return value;
        };
    }

    // If Shared is true, reset std::shared_ptr, otherwise delete raw pointer.
    template <bool S = Shared>
    std::enable_if_t<S> delete_value(value_ptr_type& v) {
        v.reset();
    }
    template <bool S = Shared>
    std::enable_if_t<!S> delete_value(value_ptr_type& v) {
        delete v;
    }

    bool _label_names_valid;
    size_t _max_stats_count;
    // make_value() bound to the arguments the MultiDimension was constructed with.
    // Called once per new label combination, never on the recording path.
    std::function<value_ptr_type()> _new_value;
    MetricMapDBD _metric_map;
};

} // namespace bvar

#include "bvar/multi_dimension_inl.h"

#endif // BVAR_MULTI_DIMENSION_H
