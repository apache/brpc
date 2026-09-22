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

#ifndef BVAR_MULTI_DIMENSION_INL_H
#define BVAR_MULTI_DIMENSION_INL_H

#include <gflags/gflags_declare.h>
#include "butil/compiler_specific.h"

namespace bvar {

DECLARE_uint32(max_multi_dimension_stats_count);

static const std::string ALLOW_UNUSED METRIC_TYPE_COUNTER = "counter";
static const std::string ALLOW_UNUSED METRIC_TYPE_SUMMARY = "summary";
static const std::string ALLOW_UNUSED METRIC_TYPE_HISTOGRAM = "histogram";
static const std::string ALLOW_UNUSED METRIC_TYPE_GAUGE = "gauge";

template <typename T, typename KeyType, bool Shared>
template <typename... Args,
          std::enable_if_t<std::is_constructible<
              T, const typename std::decay<Args>::type&...>::value, int>>
MultiDimension<T, KeyType, Shared>::MultiDimension(const key_type& labels,
                                                   Args&&... args)
    : Base(labels)
    , _label_names_valid(are_label_names_valid(labels))
    , _max_stats_count(FLAGS_max_multi_dimension_stats_count)
    , _new_value(make_value_factory(std::forward<Args>(args)...)) {
    _metric_map.Modify(init_flatmap);
}

template <typename T, typename KeyType, bool Shared>
template <typename... Args,
          std::enable_if_t<std::is_constructible<
              T, const typename std::decay<Args>::type&...>::value, int>>
MultiDimension<T, KeyType, Shared>::MultiDimension(const butil::StringPiece& name,
                                                   const key_type& labels,
                                                   Args&&... args)
    : MultiDimension(labels, std::forward<Args>(args)...) {
    this->expose(name);
}

template <typename T, typename KeyType, bool Shared>
template <typename... Args,
          std::enable_if_t<std::is_constructible<
              T, const typename std::decay<Args>::type&...>::value, int>>
MultiDimension<T, KeyType, Shared>::MultiDimension(const butil::StringPiece& prefix,
                                                   const butil::StringPiece& name,
                                                   const key_type& labels,
                                                   Args&&... args)
    : MultiDimension(labels, std::forward<Args>(args)...) {
    this->expose_as(prefix, name);
}

template <typename T, typename KeyType, bool Shared>
MultiDimension<T, KeyType, Shared>::~MultiDimension() {
    this->hide();
    delete_stats();
}

template <typename T, typename KeyType, bool Shared>
size_t MultiDimension<T, KeyType, Shared>::init_flatmap(MetricMap& bg) {
    // size = 1 << 13
    CHECK_EQ(0, bg.init(8192, 80));
    return 1;
}

template <typename T, typename KeyType, bool Shared>
size_t MultiDimension<T, KeyType, Shared>::count_stats() {
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return 0;
    }
    return metric_map_ptr->size();
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
void MultiDimension<T, KeyType, Shared>::delete_stats(const K& labels_value) {
    if (is_valid_lables_value(labels_value)) {
        // Because there are two copies(foreground and background) in DBD,
        // we need to use an empty tmp_metric, get the deleted value of
        // second copy into tmp_metric, which can prevent the bvar object
        // from being deleted twice.
        op_value_type tmp_metric = nullptr;
        auto erase_fn = [&labels_value, &tmp_metric](MetricMap& bg) {
            return bg.erase(labels_value, &tmp_metric);
        };
        _metric_map.Modify(erase_fn);
        if (tmp_metric) {
            delete_value(tmp_metric);
        }
    }
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::delete_stats() {
    // Because there are two copies(foreground and background) in DBD, we need to use an empty tmp_map,
    // swap two copies with empty, and get the value of second copy into tmp_map,
    // then traversal tmp_map and delete bvar object,
    // which can prevent the bvar object from being deleted twice.
    MetricMap tmp_map;
    CHECK_EQ(0, tmp_map.init(8192, 80));
    auto clear_fn = [&tmp_map](MetricMap& map) -> size_t {
        if (!tmp_map.empty()) {
            tmp_map.clear();
        }
        tmp_map.swap(map);
        return 1;
    };
    int ret = _metric_map.Modify(clear_fn);
    CHECK_EQ(1, ret);
    for (auto& kv : tmp_map) {
        delete_value(kv.second);
    }
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::list_stats(std::vector<key_type>* names) {
    if (names == nullptr) {
        return;
    }
    names->clear();
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return;
    }
    names->reserve(metric_map_ptr->size());
    for (auto it = metric_map_ptr->begin(); it != metric_map_ptr->end(); ++it) {
        names->emplace_back(it->first);
    }
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
typename MultiDimension<T, KeyType, Shared>::value_ptr_type
MultiDimension<T, KeyType, Shared>::get_stats_impl(const K& labels_value) {
    if (!is_valid_lables_value(labels_value)) {
        return nullptr;
    }
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return nullptr;
    }

    auto it = metric_map_ptr->seek(labels_value);
    if (nullptr == it) {
        return nullptr;
    }
    return (*it);
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
typename MultiDimension<T, KeyType, Shared>::value_ptr_type
MultiDimension<T, KeyType, Shared>::get_stats_impl(
    const K& labels_value, STATS_OP stats_op, bool* do_write) {
    if (!is_valid_lables_value(labels_value)) {
        return nullptr;
    }
    {
        MetricMapScopedPtr metric_map_ptr;
        if (0 != _metric_map.Read(&metric_map_ptr)) {
            LOG(ERROR) << "Fail to read dbd";
            return nullptr;
        }

        auto it = metric_map_ptr->seek(labels_value);
        if (nullptr != it) {
            return (*it);
        } else if (READ_ONLY == stats_op) {
            return nullptr;
        }

        if (metric_map_ptr->size() > _max_stats_count) {
            LOG(ERROR) << "Too many stats seen, overflow detected, max stats count="
                       << _max_stats_count;
            return nullptr;
        }
    }

    // Because DBD has two copies(foreground and background) MetricMap, both copies need to be modified,
    // In order to avoid new duplicate bvar object, need use cache_metric to cache the new bvar object,
    // In this way, when modifying the second copy, can directly use the cache_metric bvar object.
    op_value_type cache_metric = nullptr;
    auto insert_fn = [this, &labels_value, &cache_metric, &do_write](MetricMap& bg) {
        auto bg_metric = bg.seek(labels_value);
        if (nullptr != bg_metric) {
            cache_metric = *bg_metric;
            return 0;
        }
        if (do_write) {
            *do_write = true;
        }

        if (cache_metric == nullptr) {
            cache_metric = _new_value();
        }
        insert_metrics_map(bg, labels_value, cache_metric);
        return 1;
    };
    _metric_map.Modify(insert_fn);
    return cache_metric;
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::clear_stats() {
    delete_stats();
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
bool MultiDimension<T, KeyType, Shared>::has_stats(const K& labels_value) {
    return get_stats_impl(labels_value) != nullptr;
}

template <typename T, typename KeyType, bool Shared>
template <typename U>
std::enable_if_t<!detail::IsCompositeMetric<U>::value, size_t>
MultiDimension<T, KeyType, Shared>::dump_impl(Dumper* dumper, const DumpOptions* options) {
    std::vector<key_type> label_names;
    list_stats(&label_names);
    if (label_names.empty() || !dumper->dump_comment(this->name(), METRIC_TYPE_GAUGE)) {
        return 0;
    }
    size_t n = 0;
    std::string key;
    for (auto& label_name : label_names) {
        value_ptr_type bvar = get_stats_impl(label_name);
        if (nullptr == bvar) {
            continue;
        }
        std::ostringstream oss;
        bvar->describe(oss, options->quote_string);
        make_dump_key(&key, label_name);
        // A false asks to stop dumping, as Dumper::dump() does.
        if (!dumper->dump_mvar(key, oss.str())) {
            break;
        }
        n++;
    }
    return n;
}

namespace detail {
// Forwards to another Dumper and counts the metrics that went through, which is
// how MultiDimension answers with the number of dumped metrics rather than the
// number of times it called dump_samples().
class CountingDumper : public Dumper {
public:
    explicit CountingDumper(Dumper* dumper) : _dumper(dumper), _count(0) {}

    // Only what the wrapped dumper accepted is counted: a false is a request to
    // stop, that metric did not make it out.
    bool dump(const std::string& name, const butil::StringPiece& desc) override {
        if (!_dumper->dump(name, desc)) {
            return false;
        }
        ++_count;
        return true;
    }
    bool dump_mvar(const std::string& name, const butil::StringPiece& desc) override {
        if (!_dumper->dump_mvar(name, desc)) {
            return false;
        }
        ++_count;
        return true;
    }
    // A comment describes a family, it is not a metric of its own.
    bool dump_comment(const std::string& name, const std::string& type) override {
        return _dumper->dump_comment(name, type);
    }

    size_t count() const { return _count; }

private:
    Dumper* _dumper;
    size_t _count;
};
}  // namespace detail

template <typename T, typename KeyType, bool Shared>
template <typename U>
std::enable_if_t<detail::IsCompositeMetric<U>::value, size_t>
MultiDimension<T, KeyType, Shared>::dump_impl(Dumper* dumper, const DumpOptions*) {
    std::vector<key_type> label_names;
    list_stats(&label_names);
    if (label_names.empty()) {
        return 0;
    }
    const std::vector<MetricFamily>& families = U::list_metric_families();
    detail::CountingDumper counting_dumper(dumper);
    std::string family_name;
    std::string labels;
    // Families outside, label sets inside.
    for (size_t f = 0; f < families.size(); ++f) {
        // suffix is nullable, as collect_metric_family_names() knows.
        family_name.assign(this->name());
        if (families[f].suffix != nullptr) {
            family_name.append(families[f].suffix);
        }
        // One TYPE line per family, ahead of all its samples.
        if (!counting_dumper.dump_comment(family_name, families[f].type)) {
            break;
        }
        for (const auto& label_name : label_names) {
            value_ptr_type bvar = get_stats_impl(label_name);
            if (bvar == nullptr) {
                continue;
            }
            labels.clear();
            append_labels_kvpair_body(&labels, label_name);
            // A false asks to stop dumping, as Dumper::dump() does. Going on
            // would write samples under a family whose TYPE line the dumper
            // has already given up on.
            if (!bvar->dump_samples(&counting_dumper, f, family_name, labels)) {
                return counting_dumper.count();
            }
        }
    }
    return counting_dumper.count();
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::make_dump_key(
    std::string* key, const key_type& labels_value) {
    key->assign(this->name());
    key->push_back('{');
    append_labels_kvpair_body(key, labels_value);
    key->push_back('}');
}

template <typename T, typename KeyType, bool Shared>
bool MultiDimension<T, KeyType, Shared>::append_labels_kvpair_body(
    std::string* key, const key_type& labels_value) {
    auto label_key = this->_labels.cbegin();
    auto label_value = labels_value.cbegin();
    bool has_label = false;
    for (; label_key != this->_labels.cend() && label_value != labels_value.cend();
        label_key++, label_value++) {
        if (has_label) {
            key->push_back(',');
        }
        key->append(label_key->data(), label_key->size());
        key->append("=\"");
        key->append(label_value->data(), label_value->size());
        key->push_back('"');
        has_label = true;
    }
    return has_label;
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
bool MultiDimension<T, KeyType, Shared>::is_valid_lables_value(const K& labels_value) const {
    if (this->count_labels() != labels_value.size()) {
        LOG(ERROR) << "Invalid labels count" << this->count_labels()
                   << " != " << labels_value.size();
        return false;
    }
    return true;
}

template <typename T, typename KeyType, bool Shared>
template <typename U>
std::enable_if_t<detail::IsCompositeMetric<U>::value, std::string>
MultiDimension<T, KeyType, Shared>::find_reserved_label(const key_type& labels) {
    const std::vector<MetricFamily>& families = U::list_metric_families();
    for (const auto& label : labels) {
        for (const auto& family : families) {
            for (const auto& reserved : family.reserved_labels) {
                if (label == reserved) {
                    return label;
                }
            }
        }
    }
    return std::string();
}

namespace detail {
// `a, b, c`. Built for a log message only, never on the recording path.
template <typename KeyType>
std::string join_label_names(const KeyType& labels) {
    std::string joined;
    for (auto& label : labels) {
        if (!joined.empty()) {
            joined.append(", ");
        }
        joined.append(label);
    }
    return joined;
}
}  // namespace detail

template <typename T, typename KeyType, bool Shared>
bool MultiDimension<T, KeyType, Shared>::are_label_names_valid(const key_type& labels) {
    std::string reserved = find_reserved_label(labels);
    if (reserved.empty()) {
        return true;
    }
    // Recording keeps working, only exposing does not: a sample would carry
    // the same label twice, which is not valid prometheus text. Rename the
    // outer label and the metric comes back.
    LOG(ERROR) << "MultiDimension with labels[" << detail::join_label_names(labels)
               << "] uses the label name `" << reserved
               << "` reserved by its composite metric, it cannot be exposed";
    return false;
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::describe(std::ostream& os) {
    os << "{\"name\" : \"" << this->name() << "\", \"labels\" : [";
    char comma[3] = {'\0', ' ', '\0'};
    for (auto& label : this->_labels) {
        os << comma << "\"" << label << "\"";
        comma[0] = ',';
    }
    os << "], \"stats_count\" : " << count_stats() <<  "}";
}

} // namespace bvar

#endif // BVAR_MULTI_DIMENSION_INL_H
